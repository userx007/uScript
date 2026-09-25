#ifndef DDS_TYPED_DRIVER_HPP
#define DDS_TYPED_DRIVER_HPP

#include "ICommDriver.hpp"
#include "ICommDumpProtocol.hpp"

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
 * @brief The typed counterpart to `DdsDriver` (dds_driver.hpp): instead of
 * one fixed generic `{ string payload; }` IDL sample for every topic,
 * this class dlopen()s customer-specific "type plugin" `.so`s built from
 * real `.idl` files (see `DdsTypePluginAbi.h`'s doc comment for the ABI
 * and `src/plugin/dds_typed_plugin/examples/customer1` for a complete,
 * runnable example) and publishes/subscribes each type's own real
 * struct on its own topic.
 *
 * The point of the split from `DdsTypeEntry`'s ABI: **this class never
 * includes a customer's generated header and never touches a single
 * field of any sample.** Everything it needs — the Cyclone topic
 * descriptor, how to allocate/free a correctly-shaped sample, and how to
 * turn a `DDS_TYPED.CMD > PUBLISH`'s text argument into a filled sample
 * (and a received sample back into text) — comes from the loaded
 * `DdsTypeEntry`. That is what lets a customer's data model be swapped
 * (or several loaded side by side) by dropping in a different `.so`,
 * with zero rebuild of this class, `dds_typed_plugin`, or anything else
 * in this codebase — the actual requirement this class exists to serve.
 *
 * Command surface (see `send()`/`receive()` below and
 * `dds_typed_plugin.hpp`'s class doc comment for the full
 * `DDS_TYPED.CMD` grammar):
 *   - `LOAD <path.so>`      — dlopen a customer type plugin, register its topics
 *   - `PUBLISH <topic> <text...>` — decode() text via that topic's loaded type, dds_write()
 *   - `SUBSCRIBE <topic>[,<topic>...] [<topic>[,<topic>...] ...]` / `UNSUBSCRIBE <topic>`
 *     — one `SUBSCRIBE` may name several topics at once (space- and/or
 *     comma-separated), each getting its own Cyclone reader running in
 *     parallel; see `receive()`'s doc comment for how they're drained.
 *   - `LIST`                — loaded customer plugins/types + discovered remote participants/endpoints
 *
 * Everything about domain/participant setup, QoS mapping, and the
 * builtin-topic-based `LIST` (participants/endpoints) is identical to
 * `DdsDriver` — see that class's doc comment for the Config field
 * rationale; it isn't repeated here.
 *
 * There is deliberately no `UNLOAD`: a `.so` that's still backing live
 * DDS entities (topics/writers/readers whose descriptor/alloc/free/
 * encode/decode function pointers point into it) cannot be safely
 * `dlclose()`d without first tearing all of those down, and this class
 * doesn't track which entities came from which loaded plugin closely
 * enough to do that automatically. Reconfigure via `DDS_TYPED.CONFIG`
 * (which resets the driver, same convention as every other plugin in
 * this codebase) if a customer plugin needs to change.
 */
class DdsTypedDriver : public ICommDriver {
    public:
        struct Config {
                uint32_t domainId        = 0;
                uint32_t participantId   = 0; // see DdsDriver::Config::participantId's doc comment — identical mapping
                bool useIpv6             = false;
                std::string ifaceAddress = "0.0.0.0";
                std::string multicastInterface;
                std::string spdpMulticastGroup;
                std::string participantName     = "uScript-DDS-Typed";
                uint8_t ttl                     = 1;
                uint32_t spdpPeriodMs           = 2000;
                uint32_t leaseDurationSec       = 20;
                bool reliable                   = false;
                uint32_t historyDepth           = 32;
                uint32_t fragmentThresholdBytes = 1300;
                std::string strInstanceName;
                // .so paths loaded automatically by open(), in order, before the
                // driver is handed back to the plugin — in addition to (not
                // instead of) whatever DDS_TYPED.CMD > LOAD calls happen later.
                std::vector<std::string> preloadPluginPaths;
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
                std::string name;
                double ageSec = 0.0;
        };

        struct DiscoveredEndpointView {
                std::string guidHex;
                std::string topic;
                std::string typeName;
                bool isWriter = false;
                bool reliable = false;
        };

        explicit DdsTypedDriver(Config config);
        ~DdsTypedDriver() override;

        bool open();
        void close();

        // ---- ICommDriver ---- (thin passthroughs — see DdsDriver's identical rationale)
        bool is_open() const override;
        CommDetails describeConnection(std::string_view xtra_params = {}) const override;
        ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                            std::string_view xtra_params = {},
                                            std::stop_token stop_tok     = {}) const override;
        ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                          const ICommDriver::ReadOptions &options,
                                          std::string_view xtra_params = {},
                                          std::stop_token stop_tok     = {}) const override;

        /// Parses one DDS_TYPED.CMD argument line — see class doc comment's
        /// command surface. Matches CommScriptCommandInterpreter<DdsTypedDriver>'s
        /// SendFunc signature exactly, same as DdsDriver::send().
        ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                      std::string_view xtra_params, std::stop_token stop_tok = {}) const;

        /// Blocks for one sample from the currently SUBSCRIBEd topic(s), fed
        /// by each topic's own Cyclone reader listener via that topic's
        /// loaded type's encode(). Three forms, selected by `xtra_params`
        /// (the `~ param` suffix on the script `<` line — see
        /// CommScriptCommandValidator's grammar doc comment):
        ///   - `xtra_params` names a topic (`DDS_TYPED.CMD < ~ <topic>`):
        ///     reads only that topic's queue — it must already be
        ///     SUBSCRIBEd. Always returns the raw `encode()`d text, exactly
        ///     as before this existed.
        ///   - `xtra_params` empty, exactly one topic currently SUBSCRIBEd:
        ///     reads that one topic's queue — identical to every prior
        ///     release, so existing single-topic scripts are unaffected.
        ///   - `xtra_params` empty, more than one topic currently
        ///     SUBSCRIBEd: multiplexed read across every one of them in
        ///     parallel — blocks until *any* has a sample, returns
        ///     `<topic>: <text>` so the caller can tell which topic it came
        ///     from (see m_MultiplexedReceive()'s doc comment for the
        ///     across-topic ordering caveat).
        /// Same stop_tok cancellation contract as before in every form
        /// (condition_variable_any native wait for the infinite case, a
        /// 200ms-slice retry loop otherwise).
        ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                        const ICommDriver::ReadOptions &options, std::string_view xtra_params,
                                        std::stop_token stop_tok = {}) const;

        std::vector<DiscoveredParticipantView> listParticipants() const;
        std::vector<DiscoveredEndpointView> listEndpoints() const;

    private:
        Config m_config;

        using DdsEntity                           = int32_t; // see DdsDriver.hpp's identical rationale for not including <dds/dds.h> here
        static constexpr DdsEntity kInvalidEntity = -1;

        DdsEntity m_domain                        = kInvalidEntity;
        DdsEntity m_participant                   = kInvalidEntity;
        DdsEntity m_biParticipantReader           = kInvalidEntity;
        DdsEntity m_biPublicationReader           = kInvalidEntity;
        DdsEntity m_biSubscriptionReader          = kInvalidEntity;

        std::string m_strIdentityLabel;
        std::string m_guidHex;

        // Opaque here on purpose (this header never includes DdsTypePluginAbi.h
        // or <dds/dds.h>) — the .cpp casts back to `const DdsTypeEntry*`.
        using OpaqueTypeEntry = const void *;

        struct LocalWriter {
                DdsEntity topic           = kInvalidEntity;
                DdsEntity writer          = kInvalidEntity;
                OpaqueTypeEntry typeEntry = nullptr;
        };

        struct LocalReader {
                DdsEntity topic           = kInvalidEntity;
                DdsEntity reader          = kInvalidEntity;
                OpaqueTypeEntry typeEntry = nullptr;
                // Back-pointer set by m_EnsureLocalReader() so the static
                // dds_lset_data_available() callback (which only gets this
                // LocalReader as its `arg`) can also poke the driver-level
                // "something arrived somewhere" signal — see m_anyDataCv.
                DdsTypedDriver *owner = nullptr;
                mutable std::mutex queueMutex;
                mutable std::condition_variable_any queueCv;
                std::deque<std::string> queue;
        };

        mutable std::mutex m_mutex;                                    // guards everything below — types/handles are populated only via LOAD, but PUBLISH/SUBSCRIBE/LIST all read them
        mutable std::vector<void *> m_loadedHandles;                   // dlopen() handles — kept open for this driver's lifetime, see class doc comment on UNLOAD
        mutable std::map<std::string, OpaqueTypeEntry> m_typesByTopic; // topic name -> DdsTypeEntry*, across every loaded plugin
        mutable std::map<std::string, LocalWriter> m_localWriters;
        mutable std::map<std::string, std::shared_ptr<LocalReader>> m_localReaders; // one entry per parallel SUBSCRIBE — see class doc comment

        mutable std::mutex m_activeTopicMutex;
        mutable std::string m_strActiveTopic; // only ever holds the `\x01LIST` sentinel now — see send()/receive(); topic selection itself is driven off m_localReaders + xtra_params, not this

        // Cross-reader "something arrived" signal for the multiplexed
        // (no-topic, 2+ subscriptions) form of receive() — see
        // m_MultiplexedReceive()'s doc comment. Deliberately separate from
        // each LocalReader's own queueMutex/queueCv (untouched, still used
        // for the single-topic and explicit-topic forms) and from m_mutex
        // (structural — LOAD/SUBSCRIBE/UNSUBSCRIBE — not a per-sample event).
        mutable std::mutex m_anyDataMutex;
        mutable std::condition_variable_any m_anyDataCv;
        // Bumped (relaxed — used only as a "did anything change" fence, the
        // actual sample data is read from each LocalReader's own queue under
        // its own queueMutex) every time any reader gets new data. Lets
        // m_MultiplexedReceive()'s wait tell "genuinely notified" apart from
        // "slice elapsed" without needing a predicate that inspects every
        // reader's queue while holding an unrelated mutex.
        mutable std::atomic<uint64_t> m_anyDataGeneration{0};

        std::string m_BuildDomainConfigXml() const; // identical field mapping to DdsDriver's — see that .cpp
        bool m_LoadPlugin(const std::string &path) const;
        DdsEntity m_EnsureLocalWriter(const std::string &topic) const;
        std::shared_ptr<LocalReader> m_EnsureLocalReader(const std::string &topic) const;

        bool m_Publish(const std::string &topic, const std::string &text) const;
        bool m_Subscribe(const std::string &topic) const;
        bool m_Unsubscribe(const std::string &topic) const;
        std::string m_BuildListText() const;

        /// Shared body of receive()'s multiplexed (xtra_params empty, 2+
        /// topics currently SUBSCRIBEd) form — see receive()'s doc comment.
        ICommDriver::ReadResult m_MultiplexedReceive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                                     std::stop_token stop_tok) const;

        /// Blocks on one reader's queue; see the .cpp definition's doc
        /// comment. Static (not const, no `this`) since it only ever
        /// touches the LocalReader passed in.
        static std::optional<std::string> m_WaitPopOne(LocalReader &reader, uint32_t u32ReadTimeout,
                                                        std::stop_token stop_tok);

        static void m_OnReaderDataAvailable(DdsEntity reader, void *arg);
};

#endif // DDS_TYPED_DRIVER_HPP
