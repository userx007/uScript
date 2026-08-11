#include "grpc_driver.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"

#include <grpcpp/impl/client_unary_call.h>
#include <grpcpp/impl/rpc_method.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "GRPC_DRV    |"

static constexpr const char* kPluginNameForDump = "GRPC";

// -----------------------------------------------------------------------
// send()->receive() handoff on the same thread — see class doc comment
// (grpc_driver.hpp) for why thread_local, not a plain member; the same
// reasoning as MqttDriver's tl_bAwaitingAck/tl_pendingAckType.
// -----------------------------------------------------------------------
namespace
{
    thread_local bool        tl_bResponsePending = false;
    thread_local std::string tl_strPendingResponseJson;
}

GrpcDriver::GrpcDriver(Config config)
    : m_config(std::move(config))
{
}

namespace
{
    bool readFileIntoString(const std::string& path, std::string& out)
    {
        if (path.empty()) {
            return true; // optional file — leaving `out` untouched is fine
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }
}

bool GrpcDriver::open()
{
    if (m_config.host.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured — GRPC.CONFIG h=<host> first"));
        return false;
    }
    if (m_config.protosetPath.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Descriptor set not configured — GRPC.CONFIG d=<path.protoset> first"));
        return false;
    }

    std::string err;
    if (!m_protocol.loadDescriptorSet(m_config.protosetPath, err)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to load descriptor set:"); LOG_STRING(err));
        return false;
    }
    m_bDescriptorsLoaded = true;

    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (m_config.useTls) {
        grpc::SslCredentialsOptions sslOpts;
        std::string caCert, clientCert, clientKey;
        if (!readFileIntoString(m_config.caCertPath, caCert) ||
            !readFileIntoString(m_config.clientCertPath, clientCert) ||
            !readFileIntoString(m_config.clientKeyPath, clientKey)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to read TLS cert/key file(s)"));
            return false;
        }
        sslOpts.pem_root_certs = caCert;       // empty = grpc++'s default trust roots
        sslOpts.pem_cert_chain = clientCert;   // empty = no client certificate offered
        sslOpts.pem_private_key = clientKey;
        creds = grpc::SslCredentials(sslOpts);
    } else {
        creds = grpc::InsecureChannelCredentials();
    }

    if (!m_config.authToken.empty()) {
        creds = grpc::CompositeChannelCredentials(
            creds, grpc::AccessTokenCredentials(m_config.authToken));
    }

    const std::string target = m_config.host + ":" + std::to_string(m_config.port);
    m_channel = grpc::CreateChannel(target, creds);
    if (!m_channel) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("grpc::CreateChannel failed for"); LOG_STRING(target));
        return false;
    }

    m_strIdentityLabel = std::string("grpc://") + target + (m_config.useTls ? " (tls)" : "");

    // Channels connect lazily by design; force one connection attempt now
    // so a bad host/port/TLS setup is reported by CONFIG/open() instead of
    // silently surfacing as a timeout on the first CALL.
    const auto deadline = std::chrono::system_clock::now() +
                           std::chrono::milliseconds(m_config.connectTimeoutMs);
    if (!m_channel->WaitForConnected(deadline)) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No connection yet to"); LOG_STRING(target);
            LOG_STRING("within connectTimeoutMs — channel will keep retrying in the background "
                       "(grpc++'s normal reconnect behaviour); continuing, first CALL may block "
                       "up to its own call timeout"));
    }

    return true;
}

bool GrpcDriver::is_open() const
{
    // Structural readiness, not live TCP connectivity — see class doc
    // comment's "Session lifetime".
    return m_channel != nullptr && m_bDescriptorsLoaded;
}

CommDetails GrpcDriver::describeConnection(std::string_view xtra_params) const
{
    (void)xtra_params; // no per-call addressing concept — every CALL targets the same channel
    return commdump_details(CommFamily::NET, m_strIdentityLabel);
}

ICommDriver::WriteResult GrpcDriver::tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                                 std::string_view xtra_params) const
{
    (void)u32WriteTimeout; (void)buffer; (void)xtra_params;
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write() is not supported — use GRPC.CMD > CALL ..."));
    return ICommDriver::WriteResult{ICommDriver::Status::OPERATION_FAILED, 0};
}

ICommDriver::ReadResult GrpcDriver::tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                               const ICommDriver::ReadOptions& options,
                                               std::string_view xtra_params) const
{
    (void)u32ReadTimeout; (void)buffer; (void)options; (void)xtra_params;
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read() is not supported — use GRPC.CMD <"));
    return ICommDriver::ReadResult{ICommDriver::Status::OPERATION_FAILED, 0, false};
}

void GrpcDriver::m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens)
{
    outTokens.clear();

    // Strip a trailing NUL — same convention as MqttDriver::m_TokenizeArgs().
    size_t len = dataSpan.size();
    while (len > 0 && dataSpan[len - 1] == 0) {
        --len;
    }
    std::string text(reinterpret_cast<const char*>(dataSpan.data()), len);

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= n) break;
        size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        outTokens.push_back(text.substr(start, i - start));
    }
}

ICommDriver::WriteResult GrpcDriver::send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                           std::string_view xtra_params) const
{
    (void)u32WriteTimeout; // send() uses its own configured callTimeoutMs — see class doc comment
    ICommDriver::WriteResult result;

    tl_bResponsePending = false; // clear any state left by an earlier, unrelated unary CALL on this thread

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    std::vector<std::string> tokens;
    m_TokenizeArgs(dataSpan, tokens);
    if (tokens.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: CALL <package.Service/Method> [json_request] | FINISH"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    std::string verb = tokens[0];
    std::transform(verb.begin(), verb.end(), verb.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (verb == "FINISH") {
        return m_Finish(xtra_params);
    }

    if (verb != "CALL") {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown GRPC command (expected CALL or FINISH):"); LOG_STRING(tokens[0]));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (tokens.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: CALL <package.Service/Method> [json_request]"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }
    const std::string& methodPath = tokens[1];

    // Everything after the method path, rejoined with single spaces — the
    // JSON request body may itself contain spaces, so (like MQTT's PUBLISH
    // payload) it cannot be a separate whitespace-delimited token. By the
    // time this text reaches send() the shared grammar has already
    // stripped the outer `'...'` wrap documented in grpc_protocol.hpp's
    // class doc comment, so this is real JSON, unmodified.
    std::string jsonBody;
    for (size_t i = 2; i < tokens.size(); ++i) {
        if (i > 2) jsonBody += ' ';
        jsonBody += tokens[i];
    }

    std::string err;
    const auto* method = m_protocol.resolveMethod(methodPath, err);
    if (!method) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath); LOG_STRING(":"); LOG_STRING(err));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (method->server_streaming() && !method->client_streaming()) {
        return m_CallServerStreaming(method, methodPath, jsonBody, xtra_params);
    }
    if (method->client_streaming() && !method->server_streaming()) {
        return m_CallClientStreaming(method, methodPath, jsonBody, xtra_params);
    }
    if (method->client_streaming() && method->server_streaming()) {
        return m_CallBidiStreaming(method, methodPath, jsonBody, xtra_params);
    }
    return m_CallUnary(method, methodPath, jsonBody, xtra_params);
}

ICommDriver::WriteResult GrpcDriver::m_CallUnary(const google::protobuf::MethodDescriptor* method,
                                                  const std::string& methodPath, const std::string& jsonBody,
                                                  std::string_view xtra_params) const
{
    ICommDriver::WriteResult result;
    std::string err;

    auto request = m_protocol.newRequestMessage(method);
    if (!m_protocol.parseJsonIntoMessage(jsonBody, *request, err)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath);
            LOG_STRING(": bad JSON request:"); LOG_STRING(err));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Tx,
                              reinterpret_cast<const uint8_t*>(jsonBody.data()),
                              static_cast<uint32_t>(jsonBody.size()));
    }

    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(m_config.callTimeoutMs));

    // methodPath may have used "package.Service/Method" or
    // "package.Service.Method"; the wire path is always "/Service/Method"
    // with the service's *fully-qualified* name — take that straight from
    // the resolved descriptor rather than re-deriving it from methodPath.
    const std::string wirePath = "/" + method->service()->full_name() + "/" + method->name();

    grpc::internal::RpcMethod rpcMethod(wirePath.c_str(), grpc::internal::RpcMethod::NORMAL_RPC);
    auto response = m_protocol.newResponseMessage(method);

    grpc::Status callStatus = grpc::internal::BlockingUnaryCall<google::protobuf::Message, google::protobuf::Message>(
        m_channel.get(), rpcMethod, &ctx, *request, response.get());

    if (!callStatus.ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath); LOG_STRING("failed:");
            LOG_STRING(callStatus.error_message()));
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    std::string responseJson;
    if (!m_protocol.messageToJson(*response, responseJson)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath);
            LOG_STRING(": failed to render response as JSON:"); LOG_STRING(responseJson));
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Rx,
                              reinterpret_cast<const uint8_t*>(responseJson.data()),
                              static_cast<uint32_t>(responseJson.size()));
    }

    tl_strPendingResponseJson = std::move(responseJson);
    tl_bResponsePending = true;

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = jsonBody.size();
    return result;
}

ICommDriver::WriteResult GrpcDriver::m_CallServerStreaming(const google::protobuf::MethodDescriptor* method,
                                                             const std::string& methodPath, const std::string& jsonBody,
                                                             std::string_view xtra_params) const
{
    ICommDriver::WriteResult result;
    std::string err;

    auto request = m_protocol.newRequestMessage(method);
    if (!m_protocol.parseJsonIntoMessage(jsonBody, *request, err)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath);
            LOG_STRING(": bad JSON request:"); LOG_STRING(err));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Tx,
                              reinterpret_cast<const uint8_t*>(jsonBody.data()),
                              static_cast<uint32_t>(jsonBody.size()));
    }

    const std::string wirePath = "/" + method->service()->full_name() + "/" + method->name();
    grpc::internal::RpcMethod rpcMethod(wirePath.c_str(), grpc::internal::RpcMethod::SERVER_STREAMING);

    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_AbandonActiveStreamLocked();

    // Deliberately no deadline — see class doc comment's "Server streaming".
    m_pStreamContext = std::make_unique<grpc::ClientContext>();
    m_pServerStreamReader.reset(grpc::internal::ClientReaderFactory<google::protobuf::Message>::Create(
        m_channel.get(), rpcMethod, m_pStreamContext.get(), *request));
    m_strActiveStreamMethodPath = methodPath;
    m_pActiveStreamMethod = method;

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = jsonBody.size();
    return result;
}

ICommDriver::WriteResult GrpcDriver::m_CallClientStreaming(const google::protobuf::MethodDescriptor* method,
                                                             const std::string& methodPath, const std::string& jsonBody,
                                                             std::string_view xtra_params) const
{
    ICommDriver::WriteResult result;
    std::string err;

    const std::string wirePath = "/" + method->service()->full_name() + "/" + method->name();

    std::lock_guard<std::mutex> lock(m_streamMutex);

    if (!m_pClientStreamWriter || m_strActiveStreamMethodPath != methodPath) {
        // First CALL to this method (or a different method was open — see
        // m_AbandonActiveStreamLocked()'s doc comment: any new CALL starts
        // from a clean slate): open a fresh client-streaming call.
        m_AbandonActiveStreamLocked();
        m_pStreamContext = std::make_unique<grpc::ClientContext>();
        m_pClientStreamResponse = m_protocol.newResponseMessage(method);
        grpc::internal::RpcMethod rpcMethod(wirePath.c_str(), grpc::internal::RpcMethod::CLIENT_STREAMING);
        m_pClientStreamWriter.reset(grpc::internal::ClientWriterFactory<google::protobuf::Message>::Create(
            m_channel.get(), rpcMethod, m_pStreamContext.get(), m_pClientStreamResponse.get()));
        m_strActiveStreamMethodPath = methodPath;
        m_pActiveStreamMethod = method;
    }

    auto request = m_protocol.newRequestMessage(method);
    if (!m_protocol.parseJsonIntoMessage(jsonBody, *request, err)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath);
            LOG_STRING(": bad JSON request:"); LOG_STRING(err));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Tx,
                              reinterpret_cast<const uint8_t*>(jsonBody.data()),
                              static_cast<uint32_t>(jsonBody.size()));
    }

    if (!m_pClientStreamWriter->Write(*request)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath);
            LOG_STRING(": Write() failed — the stream is already closing/closed"));
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = jsonBody.size();
    return result;
}

ICommDriver::WriteResult GrpcDriver::m_CallBidiStreaming(const google::protobuf::MethodDescriptor* method,
                                                           const std::string& methodPath, const std::string& jsonBody,
                                                           std::string_view xtra_params) const
{
    ICommDriver::WriteResult result;
    std::string err;

    const std::string wirePath = "/" + method->service()->full_name() + "/" + method->name();

    std::lock_guard<std::mutex> lock(m_streamMutex);

    if (!m_pBidiStream || m_strActiveStreamMethodPath != methodPath) {
        // First CALL to this method (or a different method/stream was open
        // — see m_AbandonActiveStreamLocked()'s doc comment): open a fresh
        // bidi call. Unlike ClientReader<R> (server streaming),
        // ClientReaderWriter<W,R>'s constructor takes no initial request —
        // Read()/Write() are both separate operations from the start, so
        // there is nothing to send yet at this point.
        m_AbandonActiveStreamLocked();
        m_pStreamContext = std::make_unique<grpc::ClientContext>(); // no deadline — see "Server streaming"
        grpc::internal::RpcMethod rpcMethod(wirePath.c_str(), grpc::internal::RpcMethod::BIDI_STREAMING);
        m_pBidiStream.reset(grpc::internal::ClientReaderWriterFactory<google::protobuf::Message, google::protobuf::Message>::Create(
            m_channel.get(), rpcMethod, m_pStreamContext.get()));
        m_strActiveStreamMethodPath = methodPath;
        m_pActiveStreamMethod = method;
    }

    auto request = m_protocol.newRequestMessage(method);
    if (!m_protocol.parseJsonIntoMessage(jsonBody, *request, err)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath);
            LOG_STRING(": bad JSON request:"); LOG_STRING(err));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Tx,
                              reinterpret_cast<const uint8_t*>(jsonBody.data()),
                              static_cast<uint32_t>(jsonBody.size()));
    }

    if (!m_pBidiStream->Write(*request)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CALL"); LOG_STRING(methodPath);
            LOG_STRING(": Write() failed — the stream is already closing/closed"));
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = jsonBody.size();
    return result;
}

ICommDriver::WriteResult GrpcDriver::m_Finish(std::string_view xtra_params) const
{
    ICommDriver::WriteResult result;

    std::lock_guard<std::mutex> lock(m_streamMutex);

    if (m_pBidiStream) {
        // Half-close only — see class doc comment's "Bidirectional
        // streaming": unlike client-streaming, there is no single final
        // response to capture here. The stream stays open for reading;
        // GRPC.CMD < keeps calling Read() (see receive()) until the first
        // one returns false, which is what actually calls Finish() and
        // reports the RPC's overall status.
        if (!m_pBidiStream->WritesDone()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FINISH"); LOG_STRING(m_strActiveStreamMethodPath);
                LOG_STRING(": WritesDone() failed"));
            result.status = ICommDriver::Status::OPERATION_FAILED;
            return result;
        }
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_written = 0;
        return result;
    }

    if (!m_pClientStreamWriter) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(
            "FINISH with no active client- or bidi-streaming CALL (CALL such a method first)"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    const std::string methodPath = m_strActiveStreamMethodPath;
    m_pClientStreamWriter->WritesDone();
    grpc::Status callStatus = m_pClientStreamWriter->Finish();

    if (!callStatus.ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FINISH"); LOG_STRING(methodPath); LOG_STRING("failed:");
            LOG_STRING(callStatus.error_message()));
        m_ResetStreamStateLocked(); // WritesDone()+Finish() already called above — don't call them again
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    std::string responseJson;
    if (!m_protocol.messageToJson(*m_pClientStreamResponse, responseJson)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FINISH"); LOG_STRING(methodPath);
            LOG_STRING(": failed to render response as JSON:"); LOG_STRING(responseJson));
        m_ResetStreamStateLocked();
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Rx,
                              reinterpret_cast<const uint8_t*>(responseJson.data()),
                              static_cast<uint32_t>(responseJson.size()));
    }

    m_strStreamPendingResponseJson = std::move(responseJson);
    m_bStreamResponsePending = true;
    m_ResetStreamStateLocked(); // the writer/context are spent — only the pending JSON above survives

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = 0;
    return result;
}

void GrpcDriver::m_AbandonActiveStreamLocked() const
{
    if (m_pServerStreamReader) {
        m_pStreamContext->TryCancel();
        m_pServerStreamReader->Finish(); // best-effort; drains the call so the ClientContext can be destroyed cleanly
    }
    if (m_pClientStreamWriter) {
        m_pClientStreamWriter->WritesDone();
        m_pClientStreamWriter->Finish();
    }
    if (m_pBidiStream) {
        m_pStreamContext->TryCancel();
        m_pBidiStream->Finish(); // best-effort; WritesDone() isn't required before Finish()
    }
    m_ResetStreamStateLocked();
}

void GrpcDriver::m_ResetStreamStateLocked() const
{
    m_pServerStreamReader.reset();
    m_pClientStreamWriter.reset();
    m_pBidiStream.reset();
    m_pClientStreamResponse.reset();
    m_pStreamContext.reset();
    m_pActiveStreamMethod = nullptr;
    m_strActiveStreamMethodPath.clear();
}

ICommDriver::ReadResult GrpcDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                             const ICommDriver::ReadOptions& options,
                                             std::string_view xtra_params) const
{
    (void)u32ReadTimeout; // no per-read timeout on the sync API — see class doc comment's "Server streaming"
    ICommDriver::ReadResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (options.mode != ICommDriver::ReadMode::Exact) {
        // UntilDelimiter/UntilToken (L'...'/T'...'/X'...') expect the driver
        // to scan a *live* byte stream for a delimiter/token — a real,
        // meaningful thing to do for UART/TCPIP, not here (see class doc
        // comment's "Matching the response"). Use R'pattern' or an exact
        // '...' match instead.
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(
            "This receive mode needs a live byte-stream scan, which GrpcDriver doesn't do — "
            "use R'pattern' or an exact '...' match instead of T'...'/X'...'/L'...'/S'...'/F'...'"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    // Unary handoff (thread_local, same dispatch/thread as the CALL) takes
    // priority: it's only ever set immediately before this same-thread
    // receive() call, so if it's set, this receive() is unambiguously for
    // it — see class doc comment's "Unary calls".
    if (tl_bResponsePending) {
        tl_bResponsePending = false; // consume-once, same convention as MqttDriver::receive()
        // No trailing NUL is added — same convention as every other driver
        // in this codebase (e.g. MqttDriver::receive() for a PUBLISH
        // payload): dataSpan gets exactly the bytes received, nothing
        // appended. See class doc comment's "Matching the response".
        const size_t len = std::min(dataSpan.size(), tl_strPendingResponseJson.size());
        std::memcpy(dataSpan.data(), tl_strPendingResponseJson.data(), len);
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = len;
        return result;
    }

    std::lock_guard<std::mutex> lock(m_streamMutex);

    if (m_bStreamResponsePending) {
        // A FINISHed client-stream's response — see class doc comment's
        // "Client streaming".
        m_bStreamResponsePending = false;
        const size_t len = std::min(dataSpan.size(), m_strStreamPendingResponseJson.size());
        std::memcpy(dataSpan.data(), m_strStreamPendingResponseJson.data(), len);
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = len;
        return result;
    }

    if (m_pServerStreamReader) {
        // Blocks until the next message, end of stream, or an error — see
        // class doc comment's "Server streaming" for why there's no
        // per-read timeout here.
        auto response = m_protocol.newResponseMessage(m_pActiveStreamMethod);
        if (m_pServerStreamReader->Read(response.get())) {
            std::string responseJson;
            if (!m_protocol.messageToJson(*response, responseJson)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Server-stream"); LOG_STRING(m_strActiveStreamMethodPath);
                    LOG_STRING(": failed to render message as JSON:"); LOG_STRING(responseJson));
                result.status = ICommDriver::Status::PROTOCOL_ERROR;
                return result;
            }

            if (gui_mode_active()) {
                gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Rx,
                                      reinterpret_cast<const uint8_t*>(responseJson.data()),
                                      static_cast<uint32_t>(responseJson.size()));
            }

            // No trailing NUL — see class doc comment's "Matching the response".
            const size_t len = std::min(dataSpan.size(), responseJson.size());
            std::memcpy(dataSpan.data(), responseJson.data(), len);
            result.status = ICommDriver::Status::SUCCESS;
            result.bytes_read = len;
            return result;
        }

        // Read() returned false: the server finished sending (or the stream
        // errored) — either way, Finish() tells us which, and the stream is
        // done either way, so forget it regardless of the outcome.
        const std::string methodPath = m_strActiveStreamMethodPath;
        grpc::Status callStatus = m_pServerStreamReader->Finish();
        m_ResetStreamStateLocked(); // Finish() already called just above — don't call it again

        if (!callStatus.ok()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Server-stream"); LOG_STRING(methodPath); LOG_STRING("failed:");
                LOG_STRING(callStatus.error_message()));
            result.status = ICommDriver::Status::OPERATION_FAILED;
            return result;
        }

        // Clean end of stream: SUCCESS with zero bytes, a deliberately
        // empty-but-not-an-error result a script can check for — see class
        // doc comment's "Server streaming".
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Server-stream"); LOG_STRING(methodPath); LOG_STRING("ended"));
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = 0;
        return result;
    }

    if (m_pBidiStream) {
        // Same pattern as server-streaming above, just on the bidi stream —
        // see class doc comment's "Bidirectional streaming".
        auto response = m_protocol.newResponseMessage(m_pActiveStreamMethod);
        if (m_pBidiStream->Read(response.get())) {
            std::string responseJson;
            if (!m_protocol.messageToJson(*response, responseJson)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Bidi-stream"); LOG_STRING(m_strActiveStreamMethodPath);
                    LOG_STRING(": failed to render message as JSON:"); LOG_STRING(responseJson));
                result.status = ICommDriver::Status::PROTOCOL_ERROR;
                return result;
            }

            if (gui_mode_active()) {
                gui_notify_comm_dump(kPluginNameForDump, describeConnection(xtra_params), CommDir::Rx,
                                      reinterpret_cast<const uint8_t*>(responseJson.data()),
                                      static_cast<uint32_t>(responseJson.size()));
            }

            const size_t len = std::min(dataSpan.size(), responseJson.size());
            std::memcpy(dataSpan.data(), responseJson.data(), len);
            result.status = ICommDriver::Status::SUCCESS;
            result.bytes_read = len;
            return result;
        }

        // Read() returned false: the server closed its side (or the stream
        // errored) — either way, Finish() tells us which.
        const std::string methodPath = m_strActiveStreamMethodPath;
        grpc::Status callStatus = m_pBidiStream->Finish();
        m_ResetStreamStateLocked(); // Finish() already called just above — don't call it again

        if (!callStatus.ok()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Bidi-stream"); LOG_STRING(methodPath); LOG_STRING("failed:");
                LOG_STRING(callStatus.error_message()));
            result.status = ICommDriver::Status::OPERATION_FAILED;
            return result;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Bidi-stream"); LOG_STRING(methodPath); LOG_STRING("ended"));
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = 0;
        return result;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(
        "GRPC.CMD < with nothing outstanding (no preceding unary CALL, open server/bidi stream, or "
        "FINISHed client-stream on this driver)"));
    result.status = ICommDriver::Status::INVALID_PARAM;
    return result;
}
