#ifndef DDS_DRIVER_HPP
#define DDS_DRIVER_HPP

#include "ICommDriver.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief The "driver side" of the DDS plugin — CONFIG storage translation,
 * the DDS.CMD intermediary command parsing, and per-topic pub/sub state —
 * so DdsPlugin (dds_plugin.hpp) can stay a thin, high-level shell, exactly
 * like MqttDriver is to MqttPlugin (see mqtt_driver.hpp's class doc
 * comment for the general shape this follows).
 *
 * Three-way split, per plugin architecture guideline #12:
 *   - **Protocol side**: `uDdsProtocol` (protocols/dds) — just the one
 *     generic IDL sample type every DDS.CMD topic is published/
 *     subscribed as, compiled via Cyclone DDS's own `idlc` — see
 *     protocols/dds/idl/ucmdexec_dds.idl's doc comment.
 *   - **Driver side**: this class. Wraps the Eclipse Cyclone DDS C API
 *     (`<dds/dds.h>`, target `CycloneDDS::ddsc`) — https://github.com/eclipse-cyclonedds/cyclonedds —
 *     instead of owning raw RTPS UDP sockets directly: one `dds_entity_t`
 *     participant per `DdsDriver`, one Cyclone topic+writer per PUBLISHed
 *     topic name, one Cyclone topic+reader per SUBSCRIBEd topic name.
 *     Cyclone's own DDSI-RTPS stack now supplies everything the old
 *     hand-rolled implementation used to do itself: SPDP/SEDP discovery,
 *     HEARTBEAT/ACKNACK reliability, DATA_FRAG fragmentation/reassembly,
 *     IPv4 *and* IPv6, and interop with any other real DDSI-RTPS
 *     implementation (OpenDDS, RTI Connext, FastDDS, ...) — not just
 *     against another instance of this plugin.
 *   - **Plugin side**: `DdsPlugin` — stores CONFIG (domain id, interface,
 *     participant name, ...), builds this class's Config from it, and
 *     supplies send()/receive() to `ucmdexec::generic_cmd()`/
 *     `generic_script()` as the `pfsend`/`pfrecv` override — see
 *     dds_plugin.cpp, mirroring mqtt_plugin.cpp exactly. This side is
 *     unchanged by the Cyclone DDS switch: CONFIG keys, DDS.CMD syntax,
 *     and the ini file format are all identical to before.
 *
 * Wire data model: every DDS.CMD topic — regardless of name — is
 * published/subscribed as the single generic IDL type
 * `ucmdexec_dds::GenericSample { string payload; }` (see
 * protocols/dds/idl/ucmdexec_dds.idl). This preserves the previous
 * driver's "unkeyed, topic-addressed-by-name-only, opaque string sample"
 * model (like an MQTT topic string) without depending on any
 * vendor-specific *per-topic* IDL code generation — one fixed type,
 * compiled once, covers every topic a script names at runtime. A
 * genuinely IDL-typed peer (e.g. a real NGVA subsystem publishing its own
 * generated type on the same topic name) will not type-match this driver
 * unless it also happens to use this exact `{ string payload; }` shape —
 * this is the same interop trade-off the previous implementation's class
 * doc comment documented for its own hand-rolled `{ string data; }` CDR
 * encoding, just now backed by a real DDS type system instead of manual
 * bytes.
 *
 * QoS mapping from Config (see open()'s doc comment for the full
 * per-field mapping onto Cyclone's XML domain configuration and QoS
 * API) — reliability, history depth and fragment size all carry over
 * from before; HEARTBEAT/ACKNACK timing, DATA_FRAG reassembly and SPDP/
 * SEDP handling are now entirely Cyclone's internal business rather than
 * something this class implements or tunes directly.
 */
class DdsDriver : public ICommDriver {
    public:
        struct Config {
                uint32_t domainId        = 0;
                // Selects this participant's discovery port (RTPS "participant
                // index", RTPS spec 9.6.1.1) — maps to Discovery/ParticipantIndex
                // in the domain's Cyclone config (see open()'s doc comment). Give
                // co-located instances (e.g. several DDS:n plugin instances in one
                // process, or several processes on the same host/domain) distinct
                // values, same as before.
                uint32_t participantId   = 0;
                bool useIpv6             = false;     // -> General/Transport = udp6 vs udp
                std::string ifaceAddress = "0.0.0.0"; // "0.0.0.0"/"::" = let Cyclone auto-select; else -> General/Interfaces/NetworkInterface
                // Used as the NetworkInterface selector when ifaceAddress is left
                // at its "auto" default — IPv4: an interface IP; IPv6: an
                // interface *name* (e.g. "eth0") — same convention as before.
                std::string multicastInterface;
                // Empty = Cyclone's own family default (239.255.0.1 for IPv4;
                // Cyclone picks a fixed IPv6 group itself — see Cyclone's
                // Discovery/SPDPMulticastAddress docs — so, unlike the previous
                // hand-rolled driver, IPv6 no longer *requires* this to be set,
                // though it still must match a non-Cyclone peer's configuration).
                std::string spdpMulticastGroup;
                // Carried in the participant's standard USER_DATA QoS (visible to
                // any DDSI-RTPS peer, not just this plugin) and read back for
                // DDS.CMD > LIST — see listParticipants()'s doc comment.
                std::string participantName     = "uScript-DDS";
                uint8_t ttl                     = 1;     // -> General/MulticastTimeToLive
                uint32_t spdpPeriodMs           = 2000;  // -> Discovery/SPDPInterval
                uint32_t leaseDurationSec       = 20;    // -> Discovery/LeaseDuration
                bool reliable                   = false; // -> DDS_RELIABILITY_QOS on locally created writers/readers
                // No public per-entity QoS for this in Cyclone (it schedules
                // HEARTBEATs internally); kept only so an existing ini file /
                // CONFIG hb= argument doesn't start failing to parse. Accepted,
                // not applied — see open()'s doc comment.
                uint32_t heartbeatPeriodMs      = 500;
                uint32_t historyDepth           = 32;   // -> DDS_HISTORY_KEEP_LAST(historyDepth) QoS
                uint32_t fragmentThresholdBytes = 1300; // -> General/FragmentSize; 0 leaves Cyclone's own default
                std::string strInstanceName;
                // Safety cap on how many distinct topics may have a live local
                // reader at once (i.e. concurrently SUBSCRIBEd — see
                // m_EnsureLocalReader()'s doc comment). Cyclone DDS itself has
                // no fixed "max subscriptions" constant — a participant's
                // reader count is bounded only by process memory/handle
                // space — so this exists purely to stop a runaway script
                // (e.g. a SUBSCRIBE with a huge/typo'd topic list) from
                // silently creating an unbounded number of DDS entities.
                // 0 disables the cap.
                uint32_t maxSubscriptions = 64;
        };

        struct DiscoveredParticipantView {
                std::string guidHex;
                std::string name;    // from the peer's USER_DATA QoS, if it set one (empty otherwise — not every DDS vendor does)
                double ageSec = 0.0; // time since Cyclone's builtin-topic cache last refreshed this participant
        };

        struct DiscoveredEndpointView {
                std::string guidHex;
                std::string topic;
                std::string typeName;
                bool isWriter = false;
                bool reliable = false;
        };

        explicit DdsDriver(Config sConfig);
        ~DdsDriver() override;

        /// Applies Config to a Cyclone domain (creating it — or attaching to
        /// an already-created one, see the doc comment in the .cpp — with
        /// config.domainId), creates this driver's DDS participant plus the
        /// three built-in discovery readers DDS.CMD > LIST reads from.
        bool open();
        void close();

        // ---- ICommDriver ----
        // See MqttDriver's identical rationale: CommScriptCommandInterpreter<DdsDriver>
        // requires DriverT to implement ICommDriver, and this plugin always
        // drives it through send()/receive() below instead — these three are
        // thin passthroughs for interface completeness.
        bool is_open() const override;
        CommDetails describeConnection(std::string_view xtra_params = {}) const override;
        ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                            std::string_view xtra_params = {},
                                            std::stop_token stop_tok     = {}) const override;
        ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                          const ICommDriver::ReadOptions &options,
                                          std::string_view xtra_params = {},
                                          std::stop_token stop_tok     = {}) const override;

        /**
         * @brief The intermediary layer: parses one DDS.CMD argument line
         * (e.g. "PUBLISH sensors/temp 21.5", "SUBSCRIBE sensors/temp[,other/topic...] [more...]",
         * "UNSUBSCRIBE sensors/temp", "LIST"), performs the corresponding
         * Cyclone DDS operation, and — for PUBLISH/SUBSCRIBE — reports it to
         * the GUI comm-dump panel. Matches `CommScriptCommandInterpreter<DdsDriver>::
         * SendFunc`'s exact signature — see mqtt_driver.hpp's send() doc
         * comment for why plugins need this instead of the interpreter's own
         * automatic dump. SUBSCRIBE accepts several topics in one call
         * (space- and/or comma-separated), each getting its own parallel
         * Cyclone reader — see receive()'s doc comment for how they're drained.
         */
        ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                      std::string_view xtra_params, std::stop_token stop_tok = {}) const;

        /**
         * @brief The other half of "DDS.CMD <". Three forms, selected by
         * `xtra_params` (the `~ param` suffix on the script `<` line — see
         * CommScriptCommandValidator's grammar doc comment):
         *   - `xtra_params` names a topic (`DDS.CMD < ~ <topic>`): reads
         *     only that topic's queue — it must already be SUBSCRIBEd.
         *     Always returns the raw payload, exactly as before this existed.
         *   - `xtra_params` empty, exactly one topic currently SUBSCRIBEd:
         *     reads that one topic's queue — identical to every prior
         *     release, so existing single-topic scripts are unaffected.
         *   - `xtra_params` empty, more than one topic currently
         *     SUBSCRIBEd: multiplexed read across every one of them in
         *     parallel — blocks until *any* has a sample, returns
         *     `<topic>: <payload>` so the caller can tell which topic it
         *     came from (see m_MultiplexedReceive()'s doc comment for the
         *     across-topic ordering caveat).
         * A PUBLISH has nothing to wait for (best-effort by default, no
         * synchronous ack even when reliable=true — Cyclone's ACKNACK
         * handshake happens asynchronously) so a "PUBLISHED" confirmation
         * string is returned immediately instead — mirrors MqttDriver::receive()'s
         * ack-vs-standalone split.
         *
         * @param stop_tok Cooperative cancellation token. 0 == infinite timeout uses
         *                 condition_variable_any::wait(lock, stop_token, pred) directly — a
         *                 clean native fit for an unbounded wait. A finite timeout has no
         *                 native stop_token-aware timed wait, so it falls back to a bounded
         *                 200ms-slice wait_for() retry loop, same shape used by every other
         *                 poll()-based driver in this codebase. A default-constructed token
         *                 preserves pre-existing behaviour exactly.
         */
        ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                        const ICommDriver::ReadOptions &options, std::string_view xtra_params,
                                        std::stop_token stop_tok = {}) const;

        /// For DDS.INFO / DDS.CMD > LIST — a human-readable snapshot of every
        /// discovered participant and endpoint, read straight from Cyclone's
        /// builtin discovery topics (DCPSParticipant / DCPSPublication /
        /// DCPSSubscription) rather than a hand-maintained map. Thread-safe.
        std::vector<DiscoveredParticipantView> listParticipants() const;
        std::vector<DiscoveredEndpointView> listEndpoints() const;

    private:
        Config m_config;

        // ---- Cyclone DDS entity handles ----
        // dds_entity_t is `int32_t` (see <dds/ddsc/dds_basic_types.h>); kept
        // as a plain alias here rather than pulling <dds/dds.h> into this
        // public header, the same way the previous socket-based driver kept
        // POSIX fds as plain `int` in its header and did all the actual
        // socket-API work in the .cpp only.
        using DdsEntity                           = int32_t;
        static constexpr DdsEntity kInvalidEntity = -1;

        DdsEntity m_domain                        = kInvalidEntity; // only >=0 if *this* open() call created it — see open()'s doc comment
        DdsEntity m_participant                   = kInvalidEntity;
        DdsEntity m_biParticipantReader           = kInvalidEntity; // DDS_BUILTIN_TOPIC_DCPSPARTICIPANT
        DdsEntity m_biPublicationReader           = kInvalidEntity; // DDS_BUILTIN_TOPIC_DCPSPUBLICATION
        DdsEntity m_biSubscriptionReader          = kInvalidEntity; // DDS_BUILTIN_TOPIC_DCPSSUBSCRIPTION

        std::string m_strIdentityLabel;
        std::string m_guidHex; // this participant's own GUID, hex — used to filter self out of listParticipants()

        struct LocalWriter {
                DdsEntity topic  = kInvalidEntity;
                DdsEntity writer = kInvalidEntity;
        };

        struct LocalReader {
                DdsEntity topic  = kInvalidEntity;
                DdsEntity reader = kInvalidEntity;
                // Back-pointer set by m_EnsureLocalReader() so the static
                // dds_lset_data_available() callback (which only gets this
                // LocalReader as its `arg`) can also poke the driver-level
                // "something arrived somewhere" signal — see m_anyDataCv.
                DdsDriver *owner = nullptr;
                mutable std::mutex queueMutex;
                mutable std::condition_variable_any queueCv;
                std::deque<std::string> queue;
        };

        // Guards the two maps' structure (insert/erase/lookup) — NOT a given
        // LocalReader's queue, which has its own mutex so a blocking receive()
        // on one topic never stalls PUBLISH/SUBSCRIBE/UNSUBSCRIBE on another.
        mutable std::mutex m_mutex;
        mutable std::map<std::string, LocalWriter> m_localWriters;                  // key: topic name
        mutable std::map<std::string, std::shared_ptr<LocalReader>> m_localReaders; // key: topic name — one entry per parallel SUBSCRIBE, see class doc comment

        mutable std::mutex m_activeTopicMutex;
        mutable std::string m_strActiveTopic; // only ever holds the `\x01LIST` sentinel now — see send()/receive(); topic selection itself is driven off m_localReaders + xtra_params, not this

        // Cross-reader "something arrived" signal for the multiplexed
        // (no-topic, 2+ subscriptions) form of receive() — see
        // m_MultiplexedReceive()'s doc comment. Deliberately separate from
        // each LocalReader's own queueMutex/queueCv (untouched, still used
        // for the single-topic and explicit-topic forms) and from m_mutex
        // (structural — SUBSCRIBE/UNSUBSCRIBE — not a per-sample event).
        mutable std::mutex m_anyDataMutex;
        mutable std::condition_variable_any m_anyDataCv;
        // Bumped (relaxed — used only as a "did anything change" fence, the
        // actual sample data is read from each LocalReader's own queue under
        // its own queueMutex) every time any reader gets new data. Lets
        // m_MultiplexedReceive()'s wait tell "genuinely notified" apart from
        // "slice elapsed" without needing a predicate that inspects every
        // reader's queue while holding an unrelated mutex.
        mutable std::atomic<uint64_t> m_anyDataGeneration{0};

        // ---- helpers (implemented in dds_driver.cpp) ----
        std::string m_BuildDomainConfigXml() const;
        DdsEntity m_EnsureLocalWriter(const std::string &strTopic) const;
        std::shared_ptr<LocalReader> m_EnsureLocalReader(const std::string &strTopic) const;

        bool m_Publish(const std::string &strTopic, const std::string &strPayload) const;
        bool m_Subscribe(const std::string &strTopic) const;
        bool m_Unsubscribe(const std::string &strTopic) const;
        std::string m_BuildListText() const;

        /// Shared body of receive()'s multiplexed (xtra_params empty, 2+
        /// topics currently SUBSCRIBEd) form — see receive()'s doc comment.
        ICommDriver::ReadResult m_MultiplexedReceive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                                     std::stop_token stop_tok) const;

        /// Blocks on one reader's queue; see the .cpp definition's doc
        /// comment. Static (not const, no `this`) since it only ever
        /// touches the LocalReader passed in.
        static std::optional<std::string> m_WaitPopOne(LocalReader &sReader, uint32_t u32ReadTimeout,
                                                        std::stop_token stop_tok);

        /// dds_on_data_available_fn callback (see <dds/ddsc/dds_public_listener.h>)
        /// registered on every local reader: drains whatever Cyclone just made
        /// available via dds_take() straight into that LocalReader's queue and
        /// wakes receive(). `arg` is the LocalReader* passed to dds_create_listener().
        static void m_OnReaderDataAvailable(DdsEntity reader, void *pvArg);
};

#endif // DDS_DRIVER_HPP
