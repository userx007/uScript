#include "dds_driver.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"
#include "uString.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "DDS_DRV     |"

namespace
{
    constexpr uint16_t kPortBase        = 7400; // RTPS spec 9.6.1.1, "PB"
    constexpr uint16_t kDomainGain      = 250;   // "DG"
    constexpr uint16_t kParticipantGain = 2;     // "PG"
    constexpr uint16_t kOffsetSpdpMcast = 0;     // "d0"
    constexpr uint16_t kOffsetMetaUni   = 10;    // "d1"
    constexpr uint16_t kOffsetUserUni   = 11;    // "d3"
    constexpr const char* kSpdpMulticastGroup = "239.255.0.1";
    constexpr size_t kMaxDatagram = 9216; // generous — well past a 1500-MTU Ethernet frame, still bounded

    constexpr const char* kPluginNameForDump = "DDS";

    // Thread-local hand-off between a "DDS.CMD > SUBSCRIBE <topic>" send()
    // and the receive() call that follows it on the same '>'/'<' pair —
    // same rationale/shape as MqttDriver's tl_bAwaitingAck (mqtt_driver.cpp):
    // a standalone "DDS.CMD <" needs to know which topic's queue to wait
    // on, and thread_local keeps a background "DDS.CMD < &" polling loop
    // from racing a foreground command line's own bookkeeping.
    thread_local std::string tl_strActiveTopic;

    std::string guidPrefixHex(const DdsProtocol::GuidPrefix& p)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t b : p) oss << std::setw(2) << static_cast<int>(b);
        return oss.str();
    }

    bool setNonBlocking(int fd)
    {
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }
} // namespace

// ---------------------------------------------------------------------------
// Construction / lifetime
// ---------------------------------------------------------------------------
DdsDriver::DdsDriver(Config config)
    : m_config(std::move(config))
{
    if (m_config.strInstanceName.empty()) {
        m_config.strInstanceName = kPluginNameForDump;
    }

    // GuidPrefix: vendor-agnostic, derived from participant id + a
    // process-local random seed so two DdsDriver instances on the same
    // host (different domainId/participantId) never collide. Bytes 0-1
    // are a fixed "uScript" tag purely for readability in a packet trace.
    m_prefix[0] = 'u'; m_prefix[1] = 'S';
    m_prefix[2] = static_cast<uint8_t>(m_config.domainId >> 8);
    m_prefix[3] = static_cast<uint8_t>(m_config.domainId);
    m_prefix[4] = static_cast<uint8_t>(m_config.participantId >> 8);
    m_prefix[5] = static_cast<uint8_t>(m_config.participantId);
    std::random_device rd;
    for (size_t i = 6; i < m_prefix.size(); ++i) {
        m_prefix[i] = static_cast<uint8_t>(rd() & 0xFF);
    }
}

DdsDriver::~DdsDriver()
{
    close();
}

// ---------------------------------------------------------------------------
// open()/close()
// ---------------------------------------------------------------------------
namespace
{
    int openBoundUdpSocket(uint16_t port, const std::string& bindAddr, bool reuse)
    {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return -1;

        if (reuse) {
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, bindAddr.c_str(), &addr.sin_addr) != 1) {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }
        setNonBlocking(fd);
        return fd;
    }
}

bool DdsDriver::open()
{
    if (is_open()) return true;

    m_spdpMcastPort   = static_cast<uint16_t>(kPortBase + kDomainGain * m_config.domainId + kOffsetSpdpMcast);
    m_metaUnicastPort = static_cast<uint16_t>(kPortBase + kDomainGain * m_config.domainId + kOffsetMetaUni +
                                               kParticipantGain * m_config.participantId);
    m_userUnicastPort = static_cast<uint16_t>(kPortBase + kDomainGain * m_config.domainId + kOffsetUserUni +
                                               kParticipantGain * m_config.participantId);

    m_fdSpdpMcast   = openBoundUdpSocket(m_spdpMcastPort, m_config.ifaceAddress, /*reuse=*/true);
    m_fdMetaUnicast = openBoundUdpSocket(m_metaUnicastPort, m_config.ifaceAddress, /*reuse=*/false);
    m_fdUserUnicast = openBoundUdpSocket(m_userUnicastPort, m_config.ifaceAddress, /*reuse=*/false);

    if (m_fdSpdpMcast < 0 || m_fdMetaUnicast < 0 || m_fdUserUnicast < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to bind RTPS sockets (spdp="); LOG_UINT32(m_spdpMcastPort);
                  LOG_STRING(" meta="); LOG_UINT32(m_metaUnicastPort); LOG_STRING(" user="); LOG_UINT32(m_userUnicastPort);
                  LOG_STRING(") — port already in use? try a different DOMAIN/PARTICIPANT_ID"));
        close();
        return false;
    }

    // Join the domain's SPDP multicast group on the discovery socket.
    ip_mreq mreq{};
    ::inet_pton(AF_INET, kSpdpMulticastGroup, &mreq.imr_multiaddr);
    if (!m_config.multicastInterface.empty()) {
        ::inet_pton(AF_INET, m_config.multicastInterface.c_str(), &mreq.imr_interface);
    } else {
        mreq.imr_interface.s_addr = INADDR_ANY;
    }
    if (::setsockopt(m_fdSpdpMcast, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Could not join SPDP multicast group — discovery of remote "
                  "participants will not work, only unicast traffic to peers configured out-of-band"));
    }
    ::setsockopt(m_fdSpdpMcast, IPPROTO_IP, IP_MULTICAST_TTL, &m_config.ttl, sizeof(m_config.ttl));
    if (!m_config.multicastInterface.empty()) {
        in_addr ifAddr{};
        ::inet_pton(AF_INET, m_config.multicastInterface.c_str(), &ifAddr);
        ::setsockopt(m_fdSpdpMcast, IPPROTO_IP, IP_MULTICAST_IF, &ifAddr, sizeof(ifAddr));
    }

    m_strIdentityLabel = "DDS domain=" + std::to_string(m_config.domainId) +
                          " participant=" + std::to_string(m_config.participantId) +
                          " guid=" + guidPrefixHex(m_prefix);

    m_stopRequested = false;
    m_discoveryThread = std::thread(&DdsDriver::m_DiscoveryLoop, this);

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(m_strIdentityLabel.c_str());
              LOG_STRING(" spdp_mcast_port="); LOG_UINT32(m_spdpMcastPort);
              LOG_STRING(" meta_unicast_port="); LOG_UINT32(m_metaUnicastPort);
              LOG_STRING(" user_unicast_port="); LOG_UINT32(m_userUnicastPort));
    return true;
}

void DdsDriver::close()
{
    m_stopRequested = true;
    if (m_discoveryThread.joinable()) {
        m_discoveryThread.join();
    }
    if (m_fdSpdpMcast >= 0)   { ::close(m_fdSpdpMcast);   m_fdSpdpMcast = -1; }
    if (m_fdMetaUnicast >= 0) { ::close(m_fdMetaUnicast); m_fdMetaUnicast = -1; }
    if (m_fdUserUnicast >= 0) { ::close(m_fdUserUnicast); m_fdUserUnicast = -1; }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_participants.clear();
    m_remoteEndpoints.clear();
    m_localWriters.clear();
    m_localReaders.clear();
}

bool DdsDriver::is_open() const
{
    return m_fdSpdpMcast >= 0 && m_fdMetaUnicast >= 0 && m_fdUserUnicast >= 0;
}

CommDetails DdsDriver::describeConnection(std::string_view xtra_params) const
{
    return commdump_details(CommFamily::NET, xtra_params.empty() ? m_strIdentityLabel : xtra_params);
}

ICommDriver::WriteResult DdsDriver::tout_write(uint32_t, std::span<const uint8_t> buffer, std::string_view) const
{
    // Thin passthrough for interface completeness — see class doc comment.
    // Sends a raw datagram on the user-data socket to nothing in
    // particular (no default peer — RTPS is many-to-many); provided only
    // so DdsDriver satisfies ICommDriver outside of send()/receive().
    WriteResult r;
    r.status = is_open() ? ICommDriver::Status::OPERATION_FAILED : ICommDriver::Status::PORT_ACCESS;
    (void)buffer;
    return r;
}

ICommDriver::ReadResult DdsDriver::tout_read(uint32_t, std::span<uint8_t>, const ICommDriver::ReadOptions&, std::string_view) const
{
    ReadResult r;
    r.status = ICommDriver::Status::OPERATION_FAILED;
    return r;
}

// ---------------------------------------------------------------------------
// Discovery thread
// ---------------------------------------------------------------------------
void DdsDriver::m_DiscoveryLoop()
{
    auto lastSpdp = std::chrono::steady_clock::time_point::min();

    std::vector<uint8_t> buf(kMaxDatagram);

    while (!m_stopRequested.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastSpdp >= std::chrono::milliseconds(m_config.spdpPeriodMs)) {
            m_SendSpdpAnnounce();
            lastSpdp = now;
        }

        pollfd fds[3] = {
            { m_fdSpdpMcast,   POLLIN, 0 },
            { m_fdMetaUnicast, POLLIN, 0 },
            { m_fdUserUnicast, POLLIN, 0 },
        };
        const int rc = ::poll(fds, 3, 100 /*ms*/);
        if (rc <= 0) continue;

        for (int i = 0; i < 2; ++i) { // meta sockets (spdp mcast + meta unicast)
            if (!(fds[i].revents & POLLIN)) continue;
            while (true) {
                const ssize_t n = ::recv(fds[i].fd, buf.data(), buf.size(), 0);
                if (n <= 0) break;
                m_HandleIncomingMeta(std::span<const uint8_t>(buf.data(), static_cast<size_t>(n)));
            }
        }
        if (fds[2].revents & POLLIN) {
            while (true) {
                const ssize_t n = ::recv(m_fdUserUnicast, buf.data(), buf.size(), 0);
                if (n <= 0) break;
                m_HandleIncomingUser(std::span<const uint8_t>(buf.data(), static_cast<size_t>(n)));
            }
        }

        // Lease expiry: drop participants (and, transitively, stop treating
        // their endpoints as matched) we haven't heard SPDP from recently.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_participants.begin(); it != m_participants.end(); ) {
                const auto ageSec = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastSeen).count();
                if (ageSec > static_cast<int64_t>(it->second.info.leaseDurationSec)) {
                    it = m_participants.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}

bool DdsDriver::m_SendDatagram(int fd, std::span<const uint8_t> buf, const DdsProtocol::Locator& dest)
{
    if (fd < 0 || !dest.valid()) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(dest.port));
    const std::string ip = dest.toIpString();
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return false;
    const ssize_t n = ::sendto(fd, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return n == static_cast<ssize_t>(buf.size());
}

void DdsDriver::m_SendSpdpAnnounce() const
{
    DdsProtocol::ParticipantInfo info;
    info.prefix = m_prefix;
    info.participantName = m_config.participantName;
    info.domainId = m_config.domainId;
    info.leaseDurationSec = m_config.leaseDurationSec;
    // Advertised locators use ifaceAddress when it's a concrete address;
    // "0.0.0.0" (listen-on-all) can't be dialled back by a peer, so fall
    // back to advertising the multicast-interface address (if configured)
    // in that case — a reasonable single-NIC default.
    const std::string advertiseIp = (m_config.ifaceAddress != "0.0.0.0") ? m_config.ifaceAddress
                                     : (!m_config.multicastInterface.empty() ? m_config.multicastInterface : "127.0.0.1");
    info.metaUnicastLocator = DdsProtocol::Locator::fromIpPort(advertiseIp, m_metaUnicastPort);
    info.userUnicastLocator = DdsProtocol::Locator::fromIpPort(advertiseIp, m_userUnicastPort);

    const auto payload = DdsProtocol::buildSpdpPayload(info);
    const auto dataSm = DdsProtocol::buildDataSubmessage(DdsProtocol::kEntityIdUnknown, DdsProtocol::kEntityIdSpdpAnnouncer,
                                                           1, payload, /*isPlCdr=*/true);
    const auto msg = DdsProtocol::buildMessage(m_prefix, { DdsProtocol::buildInfoTsSubmessageNow(), dataSm });

    DdsProtocol::Locator mcast = DdsProtocol::Locator::fromIpPort(kSpdpMulticastGroup, m_spdpMcastPort);
    m_SendDatagram(m_fdSpdpMcast, msg, mcast);
}

void DdsDriver::m_SendSedpAnnounce(const DdsProtocol::Locator& toMetaLocator) const
{
    std::vector<std::vector<uint8_t>> submessages;
    submessages.push_back(DdsProtocol::buildInfoTsSubmessageNow());

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int64_t sn = 1;
        for (const auto& [topic, writer] : m_localWriters) {
            DdsProtocol::Guid g{ m_prefix, writer.entityId };
            const auto pl = DdsProtocol::buildSedpPayload(g, m_prefix, topic, "octet_seq /*opaque payload*/", false);
            submessages.push_back(DdsProtocol::buildDataSubmessage(DdsProtocol::kEntityIdUnknown,
                                   DdsProtocol::kEntityIdSedpPubAnnouncer, sn++, pl, true));
        }
        for (const auto& [topic, readerPtr] : m_localReaders) {
            DdsProtocol::Guid g{ m_prefix, readerPtr->entityId };
            const auto pl = DdsProtocol::buildSedpPayload(g, m_prefix, topic, "octet_seq /*opaque payload*/", false);
            submessages.push_back(DdsProtocol::buildDataSubmessage(DdsProtocol::kEntityIdUnknown,
                                   DdsProtocol::kEntityIdSedpSubAnnouncer, sn++, pl, true));
        }
    }

    if (submessages.size() == 1) return; // nothing local registered yet — INFO_TS alone isn't worth sending

    const auto msg = DdsProtocol::buildMessage(m_prefix, submessages);
    m_SendDatagram(m_fdMetaUnicast, msg, toMetaLocator);
}

void DdsDriver::m_HandleIncomingMeta(std::span<const uint8_t> datagram) const
{
    DdsProtocol::GuidPrefix srcPrefix{};
    std::vector<DdsProtocol::DataSubmessage> samples;
    if (!DdsProtocol::parseMessage(datagram, srcPrefix, samples)) return;
    if (srcPrefix == m_prefix) return; // our own SPDP multicast loops back — ignore

    for (const auto& d : samples) {
        if (!d.isPlCdr) continue;

        if (d.writerId == DdsProtocol::kEntityIdSpdpAnnouncer) {
            DdsProtocol::ParticipantInfo info;
            if (DdsProtocol::parseSpdpPayload(d.serializedPayload, info)) {
                m_OnParticipantDiscovered(info);
            }
        } else if (d.writerId == DdsProtocol::kEntityIdSedpPubAnnouncer) {
            DdsProtocol::EndpointInfo ep;
            if (DdsProtocol::parseSedpPayload(d.serializedPayload, /*isWriterTopic=*/true, ep)) {
                m_OnEndpointDiscovered(ep);
            }
        } else if (d.writerId == DdsProtocol::kEntityIdSedpSubAnnouncer) {
            DdsProtocol::EndpointInfo ep;
            if (DdsProtocol::parseSedpPayload(d.serializedPayload, /*isWriterTopic=*/false, ep)) {
                m_OnEndpointDiscovered(ep);
            }
        }
    }
}

void DdsDriver::m_HandleIncomingUser(std::span<const uint8_t> datagram) const
{
    DdsProtocol::GuidPrefix srcPrefix{};
    std::vector<DdsProtocol::DataSubmessage> samples;
    if (!DdsProtocol::parseMessage(datagram, srcPrefix, samples)) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& d : samples) {
        if (d.isPlCdr) continue; // builtin traffic never arrives on the user socket, but be defensive
        if (d.writerId.back() != DdsProtocol::kUserWriterKind) continue;

        for (auto& [topic, readerPtr] : m_localReaders) {
            if (readerPtr->entityId != d.readerId) continue;
            std::string payload;
            if (!DdsProtocol::decodeUserPayload(d.serializedPayload, payload)) continue;

            std::lock_guard<std::mutex> qlock(readerPtr->queueMutex);
            readerPtr->queue.push_back(std::move(payload));
            if (readerPtr->queue.size() > 256) readerPtr->queue.pop_front(); // bound memory use
            readerPtr->queueCv.notify_all();
        }
    }
}

void DdsDriver::m_OnParticipantDiscovered(const DdsProtocol::ParticipantInfo& info) const
{
    bool isNew = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_participants.find(info.prefix);
        isNew = (it == m_participants.end());
        m_participants[info.prefix] = ParticipantEntry{ info, std::chrono::steady_clock::now() };
    }
    if (isNew) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Discovered participant"); LOG_STRING(guidPrefixHex(info.prefix).c_str());
                  LOG_STRING(info.participantName.empty() ? "(unnamed)" : info.participantName.c_str()));
        if (info.metaUnicastLocator.valid()) {
            m_SendSedpAnnounce(info.metaUnicastLocator);
        }
    }
}

void DdsDriver::m_OnEndpointDiscovered(const DdsProtocol::EndpointInfo& info) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // De-dup by guid.
    for (auto& e : m_remoteEndpoints) {
        if (e.info.guid == info.guid) { e.info = info; goto matched; }
    }
    m_remoteEndpoints.push_back(RemoteEndpointEntry{ info });

matched:
    if (!info.isWriter) return; // only remote READERS matter for matching one of OUR writers

    auto writerIt = m_localWriters.find(info.topicName);
    if (writerIt == m_localWriters.end()) return;

    auto partIt = m_participants.find(info.guid.prefix);
    if (partIt == m_participants.end() || !partIt->second.info.userUnicastLocator.valid()) return;

    const auto& loc = partIt->second.info.userUnicastLocator;
    for (const auto& existing : writerIt->second.matchedReaderLocators) {
        if (existing.port == loc.port && existing.address == loc.address) return; // already matched
    }
    writerIt->second.matchedReaderLocators.push_back(loc);
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Matched remote reader for topic"); LOG_STRING(info.topicName.c_str()));
}

// ---------------------------------------------------------------------------
// Local endpoint registration
// ---------------------------------------------------------------------------
DdsDriver::LocalWriter& DdsDriver::m_EnsureLocalWriter(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localWriters.find(topic);
    if (it != m_localWriters.end()) return it->second;
    LocalWriter w;
    w.entityId = DdsProtocol::makeUserEntityId(topic, DdsProtocol::kUserWriterKind);
    // A writer registered after we've already discovered readers for this
    // topic still needs to pick them up — scan already-known endpoints.
    for (const auto& e : m_remoteEndpoints) {
        if (!e.info.isWriter && e.info.topicName == topic) {
            auto partIt = m_participants.find(e.info.guid.prefix);
            if (partIt != m_participants.end() && partIt->second.info.userUnicastLocator.valid()) {
                w.matchedReaderLocators.push_back(partIt->second.info.userUnicastLocator);
            }
        }
    }
    return m_localWriters.emplace(topic, std::move(w)).first->second;
}

std::shared_ptr<DdsDriver::LocalReader> DdsDriver::m_EnsureLocalReader(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localReaders.find(topic);
    if (it != m_localReaders.end()) return it->second;
    auto reader = std::make_shared<LocalReader>();
    reader->entityId = DdsProtocol::makeUserEntityId(topic, DdsProtocol::kUserReaderKind);
    m_localReaders.emplace(topic, reader);
    return reader;
}

// ---------------------------------------------------------------------------
// Publish / Subscribe / Unsubscribe / List
// ---------------------------------------------------------------------------
bool DdsDriver::m_Publish(const std::string& topic, const std::string& payload) const
{
    auto& writer = m_EnsureLocalWriter(topic);
    // Announce (or re-announce) this publication so peers not yet matched
    // can discover it — cheap best-effort broadcast to every known peer's
    // meta locator; a peer that already matched us just ignores the repeat.
    std::vector<DdsProtocol::Locator> metaTargets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [prefix, entry] : m_participants) {
            (void)prefix;
            if (entry.info.metaUnicastLocator.valid()) metaTargets.push_back(entry.info.metaUnicastLocator);
        }
    }
    for (const auto& t : metaTargets) m_SendSedpAnnounce(t);

    const auto cdrPayload = DdsProtocol::encodeUserPayload(payload);
    int64_t seq;
    std::vector<DdsProtocol::Locator> targets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& w = m_localWriters.at(topic);
        seq = w.seq++;
        targets = w.matchedReaderLocators;
    }
    const auto dataSm = DdsProtocol::buildDataSubmessage(DdsProtocol::kEntityIdUnknown, writer.entityId, seq, cdrPayload, false);
    const auto msg = DdsProtocol::buildMessage(m_prefix, { DdsProtocol::buildInfoTsSubmessageNow(), dataSm });

    if (targets.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("PUBLISH"); LOG_STRING(topic.c_str());
                  LOG_STRING("— no matched subscribers yet (sample not delivered to anyone this time;"
                             " retry once a matching SUBSCRIBE has run elsewhere on the domain)"));
        return true; // best-effort: not an error, mirrors an MQTT publish with QoS 0 and no subscriber
    }

    bool anySent = false;
    for (const auto& t : targets) {
        anySent = m_SendDatagram(m_fdUserUnicast, msg, t) || anySent;
    }

    if (anySent && gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(topic), CommDir::Tx, msg.data(), static_cast<uint32_t>(msg.size()));
    }
    return anySent;
}

bool DdsDriver::m_Subscribe(const std::string& topic) const
{
    m_EnsureLocalReader(topic);

    std::vector<DdsProtocol::Locator> metaTargets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [prefix, entry] : m_participants) {
            (void)prefix;
            if (entry.info.metaUnicastLocator.valid()) metaTargets.push_back(entry.info.metaUnicastLocator);
        }
    }
    for (const auto& t : metaTargets) m_SendSedpAnnounce(t);
    return true;
}

bool DdsDriver::m_Unsubscribe(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_localReaders.erase(topic) > 0;
}

std::string DdsDriver::m_BuildListText() const
{
    std::ostringstream oss;
    std::lock_guard<std::mutex> lock(m_mutex);
    oss << "participants=" << m_participants.size();
    for (const auto& [prefix, entry] : m_participants) {
        oss << " | " << guidPrefixHex(prefix) << " '" << entry.info.participantName << "' "
            << entry.info.metaUnicastLocator.toIpString() << ":" << entry.info.metaUnicastLocator.port;
    }
    oss << " ; local_writers=" << m_localWriters.size();
    for (const auto& [topic, w] : m_localWriters) {
        oss << " " << topic << "(subs=" << w.matchedReaderLocators.size() << ")";
    }
    oss << " ; local_readers=" << m_localReaders.size();
    for (const auto& [topic, r] : m_localReaders) {
        (void)r;
        oss << " " << topic;
    }
    return oss.str();
}

std::vector<DdsDriver::DiscoveredParticipantView> DdsDriver::listParticipants() const
{
    std::vector<DiscoveredParticipantView> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = std::chrono::steady_clock::now();
    for (const auto& [prefix, entry] : m_participants) {
        DiscoveredParticipantView v;
        v.guidHex = guidPrefixHex(prefix);
        v.name = entry.info.participantName;
        v.metaLocator = entry.info.metaUnicastLocator.toIpString() + ":" + std::to_string(entry.info.metaUnicastLocator.port);
        v.userLocator = entry.info.userUnicastLocator.toIpString() + ":" + std::to_string(entry.info.userUnicastLocator.port);
        v.ageSec = std::chrono::duration<double>(now - entry.lastSeen).count();
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<DdsDriver::DiscoveredEndpointView> DdsDriver::listEndpoints() const
{
    std::vector<DiscoveredEndpointView> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& e : m_remoteEndpoints) {
        DiscoveredEndpointView v;
        v.guidHex = guidPrefixHex(e.info.guid.prefix);
        v.topic = e.info.topicName;
        v.typeName = e.info.typeName;
        v.isWriter = e.info.isWriter;
        out.push_back(std::move(v));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Intermediary layer: DDS.CMD argument decomposition
// ---------------------------------------------------------------------------
namespace
{
    void tokenize(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens)
    {
        outTokens.clear();
        size_t len = dataSpan.size();
        while (len > 0 && dataSpan[len - 1] == 0) --len; // strip trailing NUL, same convention as MqttDriver
        std::string text(reinterpret_cast<const char*>(dataSpan.data()), len);
        text = ustring::trim(text);

        size_t i = 0;
        const size_t n = text.size();
        while (i < n) {
            while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (i >= n) break;
            const size_t start = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            outTokens.push_back(text.substr(start, i - start));
        }
    }
}

ICommDriver::WriteResult DdsDriver::send(uint32_t, std::span<const uint8_t> dataSpan, std::string_view xtra_params) const
{
    (void)xtra_params;
    WriteResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    std::vector<std::string> tokens;
    tokenize(dataSpan, tokens);
    if (tokens.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS.CMD > requires a command: PUBLISH, SUBSCRIBE, UNSUBSCRIBE or LIST"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    std::string cmdKeyword = tokens[0];
    std::transform(cmdKeyword.begin(), cmdKeyword.end(), cmdKeyword.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    bool ok = false;
    if (cmdKeyword == "PUBLISH") {
        if (tokens.size() < 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PUBLISH requires: <topic> [payload words...]"));
        } else {
            const std::string topic = tokens[1];
            std::string payload;
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (i > 2) payload += ' ';
                payload += tokens[i];
            }
            ok = m_Publish(topic, payload);
        }
    } else if (cmdKeyword == "SUBSCRIBE") {
        if (tokens.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE requires exactly: <topic>"));
        } else {
            tl_strActiveTopic = tokens[1];
            ok = m_Subscribe(tokens[1]);
        }
    } else if (cmdKeyword == "UNSUBSCRIBE") {
        if (tokens.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UNSUBSCRIBE requires exactly: <topic>"));
        } else {
            ok = m_Unsubscribe(tokens[1]);
            if (tl_strActiveTopic == tokens[1]) tl_strActiveTopic.clear();
        }
    } else if (cmdKeyword == "LIST") {
        ok = true; // text is produced in receive(); LIST is a "send now, read result next" pair like MQTT's INFO-ish commands
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown DDS command:"); LOG_STRING(tokens[0]));
    }

    if (cmdKeyword == "LIST") {
        tl_strActiveTopic = "\x01LIST"; // sentinel consumed by receive() below, never a legal topic name
    }

    result.status = ok ? ICommDriver::Status::SUCCESS : ICommDriver::Status::OPERATION_FAILED;
    result.bytes_written = ok ? dataSpan.size() : 0;
    return result;
}

ICommDriver::ReadResult DdsDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                            const ICommDriver::ReadOptions&, std::string_view xtra_params) const
{
    (void)xtra_params;
    ReadResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (tl_strActiveTopic == "\x01LIST") {
        const std::string text = m_BuildListText();
        const size_t len = std::min(dataSpan.size(), text.size());
        std::memcpy(dataSpan.data(), text.data(), len);
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = len;
        return result;
    }

    if (tl_strActiveTopic.empty()) {
        // No preceding SUBSCRIBE this call chain — nothing sensible to
        // wait on (mirrors MqttDriver's standalone receive, but this
        // driver has no single implicit topic the way MQTT's session has
        // an implicit connection: DDS.CMD < always needs a prior SUBSCRIBE
        // on the same thread to know which topic to block on).
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS.CMD < with no prior SUBSCRIBE on this command chain"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    auto reader = m_EnsureLocalReader(tl_strActiveTopic);
    std::unique_lock<std::mutex> qlock(reader->queueMutex);
    const bool got = reader->queueCv.wait_for(qlock, std::chrono::milliseconds(u32ReadTimeout),
                                               [&] { return !reader->queue.empty(); });
    if (!got) {
        result.status = ICommDriver::Status::READ_TIMEOUT;
        return result;
    }

    const std::string payload = std::move(reader->queue.front());
    reader->queue.pop_front();
    qlock.unlock();

    const size_t len = std::min(dataSpan.size(), payload.size());
    std::memcpy(dataSpan.data(), payload.data(), len);
    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;

    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(tl_strActiveTopic), CommDir::Rx,
                              reinterpret_cast<const uint8_t*>(payload.data()), static_cast<uint32_t>(payload.size()));
    }
    return result;
}
