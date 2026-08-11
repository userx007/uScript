#ifndef GRPC_DRIVER_HPP
#define GRPC_DRIVER_HPP

#include "ICommDriver.hpp"
#include "grpc_protocol.hpp"

// proto_utils.h must be included before any header that instantiates
// grpc::SerializationTraits<google::protobuf::Message> (sync_stream.h,
// client_unary_call.h) — it's the header that actually defines that
// specialization, and grpc++'s own headers don't reliably pull it in
// first on their own.
#include <grpcpp/impl/codegen/proto_utils.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/sync_stream.h>

#include <memory>
#include <mutex>
#include <string>

/**
 * @brief `ICommDriver` wrapper around a real, unmodified `grpc::Channel` —
 * the gRPC/protobuf analogue of MqttDriver (mqtt_driver.hpp), following the
 * exact same three-way split: GrpcProtocol (pure descriptor/JSON glue, no
 * I/O) / grpc++ itself (the real driver — HTTP/2 framing and the protobuf
 * wire format are never reimplemented here, only wrapped, exactly like
 * MqttDriver wraps TCPIP) / this class (protocol+driver glue, ICommDriver).
 *
 * -----------------------------------------------------------------------
 * GRPC.CMD's intermediary command language
 * -----------------------------------------------------------------------
 * send()'s dataSpan is tokenized the same way MqttDriver::m_TokenizeArgs()
 * does: first token is the verb, second token (for CALL) is the method
 * path ("package.Service/Method" or "package.Service.Method"), and
 * everything after that is rejoined with single spaces and treated as the
 * request body — real JSON, wrapped on the command line in the shared
 * grammar's `'...'` decorator so it can carry literal `"` characters; see
 * grpc_protocol.hpp's class doc comment for exactly why that works and
 * what it looks like on the command line, e.g.:
 *
 *     GRPC.CMD > 'CALL greeter.Greeter/SayHello {"name":"world"}' | R'.*'
 *
 * (that expected-response field is discussed further below, under
 * "Matching the response" — a plain exact `'...'` match is usable too but
 * has a caveat; `R'.*'` unconditionally captures the response and has none).
 *
 * A method whose request message has every field optional (or none at
 * all, e.g. `google.protobuf.Empty`) may omit the JSON body entirely:
 * `GRPC.CMD > 'CALL pkg.Health/Check'`.
 *
 * There is no "streaming `&`" special case here: `st` reaching
 * doDispatch()/send()/receive() only ever means *this one dispatch* is
 * running on its own jthread instead of the main script sequence (see
 * MqttPlugin::m_MQTT_CMD's own `(void)st;` — a plain CMD in this codebase
 * never loops on `st` itself; only CYCLIC does, and only inside
 * generic_send_cyclic(), see uCommandExec.hpp). So `GRPC.CMD <` — for any
 * RPC shape — delivers exactly one thing per dispatch, exactly like every
 * other plugin's `<`; a script wanting several messages off an open server
 * stream calls `GRPC.CMD <` that many times.
 *
 * -----------------------------------------------------------------------
 * Unary calls — one round trip, no gap between request and response
 * -----------------------------------------------------------------------
 * Unlike MQTT (topic subscribe now, message arrives later, possibly
 * delivered by an unrelated later dispatch) a unary gRPC call is a single
 * round trip with no meaningful gap between request and response, so —
 * deliberately differing from MqttDriver's send()/receive() split —
 * **send() performs the entire RPC synchronously** (request encode,
 * network round trip, response decode all happen inside send(), which is
 * also where both the Tx and Rx GUI comm-dump rows are emitted, since that
 * is genuinely when both cross the wire). The resulting response JSON is
 * stashed in thread-local storage exactly the way MqttDriver stashes
 * tl_bAwaitingAck/tl_pendingAckType/tl_pendingPacketId (see that class's
 * doc comment for why thread_local rather than a plain member: a unary
 * CALL and the receive() that reads its result are always the same
 * generic_cmd() dispatch, hence the same thread, so thread_local gives the
 * handoff a lock-free fast path). A `GRPC.CMD <` with no preceding CALL on
 * the same thread, and no stream active either (see below), returns
 * Status::INVALID_PARAM rather than blocking, since there is nothing
 * outstanding for it to wait on.
 *
 * -----------------------------------------------------------------------
 * Server streaming — one request, many responses
 * -----------------------------------------------------------------------
 * `CALL` on a method with `server_streaming()` (and not `client_streaming()`)
 * opens a `grpc::ClientReader<Message>` and sends the one request, but —
 * unlike unary — does *not* wait for any response; there usually isn't one
 * yet, and there may never be exactly one. Each subsequent `GRPC.CMD <`
 * pulls the next message off that same reader (`Read()`), converts it to
 * JSON, and delivers it, e.g.:
 *
 *     GRPC.CMD > 'CALL greeter.Greeter/SayHelloStream {"name":"world"}'
 *     resp1 ?= GRPC.CMD < R'.*'
 *     resp2 ?= GRPC.CMD < R'.*'
 *     resp3 ?= GRPC.CMD < R'.*'
 *
 * Because this state (which reader is open, for which method) is set up by
 * one dispatch and drained by later, possibly different, dispatches — the
 * opposite lifetime shape from the unary case above — it lives in regular
 * mutable member state guarded by m_streamMutex, not thread_local.
 *
 * When the server finishes sending, the *next* `GRPC.CMD <` calls
 * `Finish()` internally, reports a clean end of stream as
 * `Status::SUCCESS` with zero bytes (an empty response a script can check
 * for), and forgets the reader — a further `<` with nothing newly CALLed
 * goes back to the "nothing outstanding" INVALID_PARAM above. A stream
 * left mid-flight (never drained to completion) is abandoned and
 * cancelled the moment another CALL opens something new, or when this
 * driver itself is destroyed.
 *
 * There is deliberately no per-Read() timeout: the sync `ClientReader` API
 * has none to give it (only the whole call's `ClientContext` has a
 * deadline, and streams here are opened with none — see send()), so a
 * `GRPC.CMD <` against a live-but-quiet stream blocks until the next
 * message, the stream ends, or the connection itself fails. A bounded
 * per-read wait would need the async/CompletionQueue API instead of this
 * synchronous one; noted as a natural follow-up, not implemented here.
 *
 * -----------------------------------------------------------------------
 * Client streaming — many requests, one response
 * -----------------------------------------------------------------------
 * `CALL` on a method with `client_streaming()` (and not `server_streaming()`)
 * writes one message into a `grpc::ClientWriter<Message>`, opening it
 * first if this is the first CALL to that method since the writer was last
 * closed. Repeat CALL to send more messages; `FINISH` (a second verb,
 * needing no method path — it always applies to whatever client-stream is
 * currently open) half-closes the stream and delivers the server's single
 * response to the next `GRPC.CMD <`, e.g.:
 *
 *     GRPC.CMD > 'CALL greeter.Greeter/Accumulate {"name":"Alice"}'
 *     GRPC.CMD > 'CALL greeter.Greeter/Accumulate {"name":"Bob"}'
 *     GRPC.CMD > 'FINISH'
 *     resp ?= GRPC.CMD < R'.*'
 *
 * -----------------------------------------------------------------------
 * Bidirectional streaming — many requests, many responses
 * -----------------------------------------------------------------------
 * A method with both `client_streaming()` and `server_streaming()` set
 * uses `grpc::ClientReaderWriter<Message,Message>`. Unlike the sync
 * `ClientReader<R>` used for server-streaming above, its constructor takes
 * no initial request message — `Read()` and `Write()` are both separate,
 * explicit operations from the start, symmetric with client streaming:
 *
 *     GRPC.CMD > 'CALL echo.Echo/Chat {"text":"hi"}'   // opens the stream, writes one message
 *     resp1 ?= GRPC.CMD < R'.*'                         // read whatever the server sent back
 *     GRPC.CMD > 'CALL echo.Echo/Chat {"text":"again"}' // write another message (stream already open)
 *     resp2 ?= GRPC.CMD < R'.*'
 *     GRPC.CMD > 'FINISH'                               // half-close: no more writes from us
 *     resp3 ?= GRPC.CMD < R'.*'                          // keep reading until a clean end of stream
 *
 * `FINISH` here only calls `WritesDone()` (half-close), *not* `Finish()` —
 * unlike client-streaming's FINISH, there is no single final response
 * message to capture for a bidi call, so there is nothing to deliver from
 * FINISH itself. The stream stays open for reading: `GRPC.CMD <` keeps
 * calling `Read()` exactly as it does for server-streaming, and the first
 * `Read()` that returns false triggers the same `Finish()` + clean-end-of-
 * stream handling described under "Server streaming" above. Calling
 * `FINISH` again afterward, or `CALL`ing a *different* method, abandons
 * whatever is left of the stream (see `m_AbandonActiveStreamLocked()`).
 *
 * This reuses the exact same synchronous, one-operation-per-dispatch
 * architecture as server/client streaming — no `grpc::CompletionQueue`
 * needed. That was a real option worth checking before assuming otherwise:
 * grpc++'s *sync* `ClientReaderWriter` supports `Read()` and `Write()`
 * safely from different threads as long as two `Read()`s (or two
 * `Write()`s) never overlap — exactly what `m_streamMutex` already
 * guarantees here, since every dispatch takes it for the duration of its
 * one operation.
 *
 * -----------------------------------------------------------------------
 * CYCLIC — periodic unary polling
 * -----------------------------------------------------------------------
 * `GRPC.CYCLIC` (see GrpcPlugin::m_GRPC_CYCLIC) reuses
 * `ucmdexec::generic_send_cyclic()` exactly like every other plugin — each
 * entry's `val` is validated once and interpreted every tick through this
 * same `send()`/`receive()` pair, so a unary CALL (with or without a
 * trailing `| expected`) works as a periodic poll with no driver-side
 * changes at all.
 *
 * `parseCyclicArray()`'s top-level entry separator is a comma, and — this
 * matters for a JSON request body, which routinely contains one — it is
 * now quote-aware: it uses `ustring::tokenizeRespectingQuotes()` (see
 * uString.hpp) rather than a plain comma split, so a comma inside a
 * `'...'`-quoted `val` no longer splits that entry in two. This is why
 * GRPC.CMD/GRPC.CYCLIC wrap the whole `CALL ...` text in `'...'` in the
 * first place (see grpc_protocol.hpp's class doc comment) — that
 * requirement was already there for CMD's own field-boundary reasons, and
 * turns out to protect CYCLIC's entry list for free:
 *
 *     GRPC.CYCLIC 1000:> 'CALL greeter.Greeter/Greet2 {"name":"Zoe","greeting":"Hi"}' | R'.*'
 *
 * A request body should still go through `'...'` exactly as CALL always
 * requires; an entry that somehow reached parseCyclicArray() unquoted
 * (bypassing the shared grammar) would still be vulnerable to the old
 * plain-split problem — not a realistic path through GRPC.CMD/CYCLIC's own
 * grammar, but worth naming so the invariant this depends on is explicit.
 *
 * CALLing a streaming method or FINISH from inside a CYCLIC entry is
 * mechanically harmless (every tick just abandons and reopens/rewrites)
 * but isn't a sensible use of CYCLIC; there's no attempt here to specially
 * detect or block it.
 *
 * -----------------------------------------------------------------------
 * Matching the response
 * -----------------------------------------------------------------------
 * receive() only honours `ICommDriver::ReadOptions::mode ==
 * ReadMode::Exact` (any other mode is rejected with a clear
 * Status::INVALID_PARAM — see the check at the top of receive()), which
 * means the response can be checked or captured with a plain `'...'`
 * exact match or an `R'pattern'` regex, but *not* with `T'substring'`,
 * `X'hex-substring'`, `L'...'`, `S'...'` or `F'...'`: those all ask the
 * driver to scan/size a *live* byte stream (UntilDelimiter/UntilToken),
 * which is meaningful for UART/TCPIP but not here — a unary response is
 * already fully decoded before receive() is even called, and a streamed
 * message is delivered whole the moment `Read()` returns one; there is no
 * partial/in-progress byte stream at the ICommDriver level to scan either
 * way.
 *
 * Of the two that *are* supported, prefer `R'pattern'` (e.g. `R'.*'` to
 * capture unconditionally): a plain exact `'...'` match is usable but
 * inherits a framework-wide characteristic worth knowing about — an
 * expected `'...'`/STRING_RAW value is converted for comparison via
 * `ustring::stringToVector()` with its default `bTerminator=true` (see
 * `convertToData()` in uCommScriptCommandInterpreter.hpp), which appends
 * an implicit trailing NUL to the *expected* side only, so an exact match
 * only succeeds against a response the driver itself NUL-terminated on
 * the wire. That's true of NUL-terminated ASCII protocols but not of MQTT
 * payloads or, here, JSON text, so an exact `'...'` match against a real
 * response will not match even when every character is identical.
 * `R'pattern'` has no such caveat, and is the better choice regardless,
 * since protobuf's JSON serializer does not guarantee stable field
 * ordering to rely on for a whole-text exact match in the first place.
 *
 * -----------------------------------------------------------------------
 * Session lifetime
 * -----------------------------------------------------------------------
 * Like MqttDriver, one GrpcDriver (one grpc::Channel, one loaded
 * GrpcProtocol descriptor set) is opened on first use and kept alive for
 * as long as the plugin is loaded — see grpc_plugin.hpp's "Session
 * lifetime". grpc::Channel already manages connection establishment and
 * transparent reconnection internally (that's the entire point of the
 * abstraction), so is_open() intentionally reports *structural* readiness
 * (a channel object and a loaded descriptor set both exist) rather than
 * live TCP connectivity at this instant — second-guessing the channel's
 * own reconnect logic here would fight the library, not use it.
 */
class GrpcDriver : public ICommDriver
{
public:
    struct Config {
        std::string host;
        uint16_t    port = 50051;

        bool        useTls = false;
        std::string caCertPath;      // empty = use the system/default trust roots
        std::string clientCertPath;  // empty = no client (mutual-TLS) certificate
        std::string clientKeyPath;

        std::string protosetPath;    // required: FileDescriptorSet, see grpc_protocol.hpp
        std::string authToken;       // optional: sent as "authorization: Bearer <token>" metadata

        uint32_t    callTimeoutMs = 5000;    // unary calls only — see "Server streaming" for why streams have no deadline
        uint32_t    connectTimeoutMs = 5000;
    };

    explicit GrpcDriver(Config config);
    ~GrpcDriver() override = default;

    /**
     * @brief Load the descriptor set and create the underlying
     *        grpc::Channel, then wait (bounded by connectTimeoutMs) for an
     *        initial connection attempt so a bad host/port/TLS
     *        configuration is reported now rather than on the first CALL.
     */
    bool open();

    bool is_open() const override;
    CommDetails describeConnection(std::string_view xtra_params = {}) const override;

    // Thin stubs — see class doc comment. Never used by GrpcPlugin, which
    // always goes through send()/receive() instead (there is no
    // "raw byte stream" concept underneath a gRPC channel to pass through
    // to, unlike MqttDriver's tout_read/tout_write which forward to the
    // real TCPIP driver they wrap).
    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;
    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    /** @brief "CALL <package.Service/Method> [json_request]" / "FINISH" — see class doc comment. */
    ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                   std::string_view xtra_params = {}) const;

    /** @brief Delivers a unary CALL's result, the next server-stream message, or a FINISHed client-stream's result. */
    ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                     const ICommDriver::ReadOptions& options,
                                     std::string_view xtra_params = {}) const;

private:
    static void m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens);

    ICommDriver::WriteResult m_CallUnary(const google::protobuf::MethodDescriptor* method,
                                          const std::string& methodPath, const std::string& jsonBody,
                                          std::string_view xtra_params) const;
    ICommDriver::WriteResult m_CallServerStreaming(const google::protobuf::MethodDescriptor* method,
                                                    const std::string& methodPath, const std::string& jsonBody,
                                                    std::string_view xtra_params) const;
    ICommDriver::WriteResult m_CallClientStreaming(const google::protobuf::MethodDescriptor* method,
                                                    const std::string& methodPath, const std::string& jsonBody,
                                                    std::string_view xtra_params) const;
    ICommDriver::WriteResult m_CallBidiStreaming(const google::protobuf::MethodDescriptor* method,
                                                  const std::string& methodPath, const std::string& jsonBody,
                                                  std::string_view xtra_params) const;
    ICommDriver::WriteResult m_Finish(std::string_view xtra_params) const;

    // Caller must already hold m_streamMutex. Best-effort — cancels/closes
    // whatever of the reader/writer/context is currently set, ignoring the
    // resulting Status, and resets all of it to empty: any new CALL (of any
    // shape) starts from a clean slate rather than silently multiplexing
    // with leftover state from a previous, undrained stream. Only for a
    // stream NOT already Finish()ed by the caller — calling Finish() twice
    // on the same ClientReader/ClientWriter is a real grpc++ API-misuse
    // abort, not just a logical no-op, so a caller that already called
    // Finish()/WritesDone() itself (receive()'s "server stream ended"
    // branch, m_Finish()) must use m_ResetStreamStateLocked() instead.
    void m_AbandonActiveStreamLocked() const;

    // Caller must already hold m_streamMutex. Clears all stream-session
    // state without touching the reader/writer — for a caller that has
    // already itself called Finish()/WritesDone() (or never opened
    // anything) and just needs the bookkeeping reset.
    void m_ResetStreamStateLocked() const;

    Config m_config;
    std::string m_strIdentityLabel;
    std::shared_ptr<grpc::Channel> m_channel;
    GrpcProtocol m_protocol;
    bool m_bDescriptorsLoaded = false;

    // Streaming session state — see "Server streaming"/"Client streaming"
    // above for why this is regular (mutex-guarded) member state rather
    // than the thread_local used for the unary handoff.
    mutable std::mutex m_streamMutex;
    mutable std::unique_ptr<grpc::ClientContext> m_pStreamContext;
    mutable std::unique_ptr<grpc::ClientReader<google::protobuf::Message>> m_pServerStreamReader;
    mutable std::unique_ptr<grpc::ClientWriter<google::protobuf::Message>> m_pClientStreamWriter;
    mutable std::unique_ptr<grpc::ClientReaderWriter<google::protobuf::Message, google::protobuf::Message>> m_pBidiStream;
    mutable std::unique_ptr<google::protobuf::Message> m_pClientStreamResponse; // Finish() writes into this
    mutable const google::protobuf::MethodDescriptor* m_pActiveStreamMethod = nullptr; // needed to build fresh Read() targets
    mutable std::string m_strActiveStreamMethodPath; // "" when nothing is open
    mutable bool m_bStreamResponsePending = false;   // a FINISHed client-stream result awaiting receive()
    mutable std::string m_strStreamPendingResponseJson;
};

#endif // GRPC_DRIVER_HPP
