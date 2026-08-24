#ifndef DDS_DRIVER_HPP
#define DDS_DRIVER_HPP

#include "ICommDriver.hpp"
#include "dds_protocol.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * @brief The "driver side" of the DDS plugin — everything DDSI-RTPS
 * transport- and discovery-specific lives here, so DdsPlugin
 * (dds_plugin.hpp) can stay a thin, high-level shell, exactly like
 * MqttDriver is to MqttPlugin (see mqtt_driver.hpp's class doc comment for
 * the general shape this follows).
 *
 * Three-way split, per plugin architecture guideline #12:
 *   - **Protocol side**: `DdsProtocol` (dds_protocol.hpp) — pure RTPS
 *     message/submessage/ParameterList encode-decode, no I/O.
 *   - **Driver side**: this class. Implements `ICommDriver` and owns
 *     three raw UDP sockets this driver manages directly (no existing
 *     driver in this codebase supports multicast group membership or
 *     binding to a specific local port, both of which RTPS discovery
 *     requires — see m_Open() for the port/socket layout, computed per
 *     RTPS spec §9.6.1.1's well-known port formula):
 *       - a **SPDP multicast** socket: joined to the domain's Simple
 *         Participant Discovery Protocol multicast group, used to
 *         announce this participant and discover others;
 *       - a **metatraffic unicast** socket: this participant's own
 *         discovery port, used to send/receive SEDP (endpoint/topic
 *         discovery) directly to/from already-discovered peers;
 *       - a **user-data unicast** socket: where actual topic samples are
 *         sent/received once two ends are matched via SEDP.
 *     A background thread (m_DiscoveryLoop()) owns all three sockets:
 *     periodic SPDP announcement, inbound SPDP/SEDP handling (feeding
 *     m_participants / m_discoveredReaders), and inbound user DATA
 *     dispatch into the matching local topic's receive queue.
 *   - **Plugin side**: `DdsPlugin` — stores CONFIG (domain id, interface,
 *     participant name, ...), builds this class's Config from it, and
 *     supplies send()/receive() to `ucmdexec::generic_cmd()`/
 *     `generic_script()` as the `pfsend`/`pfrecv` override — see
 *     dds_plugin.cpp, mirroring mqtt_plugin.cpp exactly.
 *
 * Scope: see dds_protocol.hpp's class doc comment for the wire-format
 * scope (sample-granularity reliability, no NACK_FRAG). This driver adds:
 *   - **Reliable QoS** (Config::reliable, or per SEDP-discovered peer
 *     reliability — this driver always offers reliable delivery when
 *     asked and degrades gracefully against a best-effort peer, since a
 *     best-effort reader simply never sends ACKNACK and a HEARTBEAT it
 *     ignores costs nothing). Each reliable `LocalWriter` keeps a bounded
 *     history cache (`Config::historyDepth` samples) and answers ACKNACK
 *     by resending whatever's still cached; each `LocalReader` tracks
 *     received sequence numbers per matched remote writer and answers
 *     that writer's periodic HEARTBEAT with an ACKNACK listing gaps.
 *   - **Fragmentation** (`Config::fragmentThresholdBytes`): a sample
 *     larger than the threshold is split into DATA_FRAG submessages (one
 *     fragment per submessage) and reassembled by the reader before it
 *     reaches the topic queue — see dds_protocol.hpp's class doc comment
 *     for the fragment-vs-sample reliability granularity trade-off this
 *     makes.
 *   - **IPv4 or IPv6** (`Config::useIpv6`) — single-stack per driver
 *     instance; all three sockets and the SPDP multicast group use
 *     whichever family is configured.
 *
 * This is enough to publish and subscribe real samples — reliably, and
 * past a single UDP datagram's practical size — against a stock OpenDDS
 * (or any DDSI-RTPS compliant) participant on the same Ethernet
 * segment/VLAN (IPv4) or link/site (IPv6), including matching by topic
 * name the way NGVA's Data Model — see the plugin's INFO text — expects,
 * without depending on any vendor-specific IDL code generation.
 */
class DdsDriver : public ICommDriver
{
public:
    struct Config {
        uint32_t domainId = 0;
        uint32_t participantId = 0;      // selects this participant's unicast metatraffic/user ports
        bool useIpv6 = false;            // single-stack per instance — selects the family for all 3 sockets
        std::string ifaceAddress = "0.0.0.0"; // local bind address for all three sockets ("::" if useIpv6)
        // IPv4: local interface IP used to join/send SPDP multicast (empty = kernel default).
        // IPv6: local interface NAME (e.g. "eth0") used the same way (empty = kernel default).
        std::string multicastInterface;
        // Empty = family default: "239.255.0.1" for IPv4 (the RTPS-spec
        // default). The DDSI-RTPS spec does not mandate a default IPv6
        // multicast group the way it does for IPv4 — vendors differ — so
        // for useIpv6=true this MUST be set to match whatever the peer
        // implementation is configured with, or discovery will not work.
        std::string spdpMulticastGroup;
        std::string participantName = "uScript-DDS";
        uint8_t  ttl = 1;
        uint32_t spdpPeriodMs = 2000;
        uint32_t leaseDurationSec = 20;
        bool     reliable = false;          // default reliability for locally created writers/readers
        uint32_t heartbeatPeriodMs = 500;   // reliable writers only
        uint32_t historyDepth = 32;         // reliable writer resend-cache depth, in samples
        uint32_t fragmentThresholdBytes = 1300; // samples larger than this are DATA_FRAG'd; 0 disables fragmentation
        std::string strInstanceName;
    };

    struct DiscoveredParticipantView {
        std::string guidHex;
        std::string name;
        std::string metaLocator;
        std::string userLocator;
        double      ageSec = 0.0;
    };
    struct DiscoveredEndpointView {
        std::string guidHex;
        std::string topic;
        std::string typeName;
        bool        isWriter = false;
    };

    explicit DdsDriver(Config config);
    ~DdsDriver() override;

    /// Computes the RTPS well-known ports for config.domainId/participantId,
    /// opens the three sockets described in the class doc comment, and
    /// starts the background discovery thread.
    bool open();
    void close();

    // ---- ICommDriver ----
    // See MqttDriver's identical rationale: CommScriptCommandInterpreter<DdsDriver>
    // requires DriverT to implement ICommDriver, and this plugin always
    // drives it through send()/receive() below instead — these three are
    // thin passthroughs onto the user-data socket for interface completeness.
    bool is_open() const override;
    CommDetails describeConnection(std::string_view xtra_params = {}) const override;
    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;
    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    /**
     * @brief The intermediary layer: parses one DDS.CMD argument line
     * (e.g. "PUBLISH sensors/temp 21.5", "SUBSCRIBE sensors/temp",
     * "UNSUBSCRIBE sensors/temp", "LIST"), performs the corresponding RTPS
     * action, and — for PUBLISH/SUBSCRIBE — reports it to the GUI
     * comm-dump panel. Matches `CommScriptCommandInterpreter<DdsDriver>::
     * SendFunc`'s exact signature — see mqtt_driver.hpp's send() doc
     * comment for why plugins need this instead of the interpreter's own
     * automatic dump.
     */
    ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                   std::string_view xtra_params) const;

    /**
     * @brief The other half: for a standalone "DDS.CMD <" (no preceding
     * SUBSCRIBE on this same '>'/'<' pair — see thread-local state's doc
     * comment in the .cpp), blocks on the most recently SUBSCRIBEd topic's
     * receive queue until a sample arrives or the timeout elapses. A
     * PUBLISH has nothing to wait for (best-effort, no ack) so a
     * "PUBLISHED" confirmation string is returned immediately instead —
     * mirrors MqttDriver::receive()'s ack-vs-standalone split.
     */
    ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                     const ICommDriver::ReadOptions& options, std::string_view xtra_params) const;

    /// For DDS.INFO / DDS.CMD > LIST — a human-readable snapshot of every
    /// discovered participant and endpoint. Thread-safe.
    std::vector<DiscoveredParticipantView> listParticipants() const;
    std::vector<DiscoveredEndpointView> listEndpoints() const;

    const DdsProtocol::GuidPrefix& guidPrefix() const { return m_prefix; }

private:
    Config m_config;

    // ---- sockets (POSIX fds; -1 == closed). See open()'s doc comment for
    // what each one is used for. ----
    int m_fdSpdpMcast = -1;
    int m_fdMetaUnicast = -1;
    int m_fdUserUnicast = -1;

    uint16_t m_spdpMcastPort = 0;
    uint16_t m_metaUnicastPort = 0;
    uint16_t m_userUnicastPort = 0;
    std::string m_strIdentityLabel;

    DdsProtocol::GuidPrefix m_prefix{};

    std::thread m_discoveryThread;
    std::atomic<bool> m_stopRequested{false};

    // ---- discovery state (guarded by m_mutex) ----
    mutable std::mutex m_mutex;

    struct ParticipantEntry {
        DdsProtocol::ParticipantInfo info;
        std::chrono::steady_clock::time_point lastSeen;
    };
    mutable std::map<DdsProtocol::GuidPrefix, ParticipantEntry> m_participants;

    struct RemoteEndpointEntry {
        DdsProtocol::EndpointInfo info;
    };
    mutable std::vector<RemoteEndpointEntry> m_remoteEndpoints; // both writers and readers, discovered via SEDP

    struct LocalWriter {
        DdsProtocol::EntityId entityId{};
        int64_t seq = 1;
        bool reliable = false;
        std::vector<DdsProtocol::Locator> matchedReaderLocators;
        // Reliable-QoS resend cache: last Config::historyDepth (seq, CDR
        // payload) pairs, oldest evicted first. Empty for a best-effort writer.
        std::deque<std::pair<int64_t, std::vector<uint8_t>>> history;
        int32_t heartbeatCount = 0;
        std::chrono::steady_clock::time_point lastHeartbeatSent{};
    };
    mutable std::map<std::string, LocalWriter> m_localWriters; // key: topic name

    // Per-(local reader, remote writer) fragment reassembly + received-set
    // bookkeeping — see class doc comment's "Reliable QoS"/"Fragmentation".
    struct FragAssembly {
        uint32_t sampleSize = 0;
        uint32_t fragmentSize = 0;
        uint32_t totalFragments = 0;
        std::vector<uint8_t> buffer;
        std::vector<bool> haveFragment;
    };
    struct RemoteWriterRxState {
        std::set<int64_t> receivedSeqs;   // bounded — see m_TrimReceivedSet()
        std::map<int64_t, FragAssembly> inProgressFrags;
    };

    struct LocalReader {
        DdsProtocol::EntityId entityId{};
        bool reliable = false;
        mutable std::mutex queueMutex;
        mutable std::condition_variable queueCv;
        std::deque<std::string> queue;
        mutable std::map<DdsProtocol::Guid, RemoteWriterRxState> perWriterRx;
    };
    mutable std::map<std::string, std::shared_ptr<LocalReader>> m_localReaders; // key: topic name

    // ---- helpers (implemented in dds_driver.cpp) ----
    void m_DiscoveryLoop();
    void m_SendSpdpAnnounce() const;
    void m_SendSedpAnnounce(const DdsProtocol::Locator& toMetaLocator) const;
    void m_HandleIncomingMeta(std::span<const uint8_t> datagram) const;
    void m_HandleIncomingUser(std::span<const uint8_t> datagram, const DdsProtocol::Locator& fromAddr) const;
    void m_OnParticipantDiscovered(const DdsProtocol::ParticipantInfo& info) const;
    void m_OnEndpointDiscovered(const DdsProtocol::EndpointInfo& info) const;

    LocalWriter& m_EnsureLocalWriter(const std::string& topic) const;
    std::shared_ptr<LocalReader> m_EnsureLocalReader(const std::string& topic) const;

    bool m_Publish(const std::string& topic, const std::string& payload) const;
    bool m_Subscribe(const std::string& topic) const;
    bool m_Unsubscribe(const std::string& topic) const;
    std::string m_BuildListText() const;

    /// Sends one sample (already CDR-encoded) to every target locator,
    /// transparently splitting into DATA_FRAG submessages first if it's
    /// larger than Config::fragmentThresholdBytes — the single send path
    /// used by both a fresh PUBLISH and an ACKNACK-triggered resend, so
    /// the two can never disagree on how a given sample is put on the wire.
    void m_SendSampleToTargets(const DdsProtocol::EntityId& writerId, int64_t seq,
                                const std::vector<uint8_t>& cdrPayload,
                                const std::vector<DdsProtocol::Locator>& targets) const;

    /// Reliable-writer housekeeping, called once per discovery-loop tick:
    /// sends a HEARTBEAT (to every matched reader locator) for each local
    /// writer whose heartbeatPeriodMs has elapsed.
    void m_SendDueHeartbeats() const;

    void m_HandleAckNack(const DdsProtocol::AckNackSubmessage& a, const DdsProtocol::Locator& fromAddr) const;
    void m_HandleHeartbeat(const DdsProtocol::HeartbeatSubmessage& h, const DdsProtocol::Locator& fromAddr) const;
    void m_HandleDataFrag(const DdsProtocol::GuidPrefix& srcPrefix, const DdsProtocol::DataFragSubmessage& f) const;

    /// Bounds a RemoteWriterRxState::receivedSeqs set's memory use over a
    /// long session by dropping entries far below the current high-water
    /// mark — they're no longer useful for gap detection against a
    /// HEARTBEAT whose firstSeqNum has long since moved past them.
    static void m_TrimReceivedSet(std::set<int64_t>& receivedSeqs);

    static bool m_SendDatagram(int fd, std::span<const uint8_t> buf, const DdsProtocol::Locator& dest);
};

#endif // DDS_DRIVER_HPP
