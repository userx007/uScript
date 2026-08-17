#include "grpc_plugin.hpp"
#include "private/grpc_setup.hpp"
#include "uCommandExec.hpp"

#include <sstream>
#include <filesystem>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "GRPC PLUGIN |"

extern "C"
{
    EXPORTED GrpcPlugin* pluginEntry() { return new GrpcPlugin(); }
    EXPORTED void pluginExit(GrpcPlugin *ptrPlugin) { delete ptrPlugin; }
}

bool GrpcPlugin::doInit(void *pvUserData)
{
    (void)pvUserData;
    m_bIsInitialized = true;
    return true;
}

void GrpcPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled = false;
    m_strResultData.clear();
    m_pDriver.reset();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool GrpcPlugin::setParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;
    if (generic_setparams<GrpcPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
        if (m_LocalSetParams(psSetParams)) {
            bRetVal = true;
        }
    }
    return bRetVal;
}

void GrpcPlugin::getParams(PluginDataGet *psGetParams) const
{
    generic_getparams<GrpcPlugin>(this, psGetParams);
}

bool GrpcPlugin::doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
{
    return generic_dispatch<GrpcPlugin>(this, strCmd, strParams, st);
}

// -----------------------------------------------------------------------
// Driver factory — see class doc comment's "Session lifetime"
// -----------------------------------------------------------------------

std::shared_ptr<GrpcDriver> GrpcPlugin::m_OpenDriver(void) const
{
    if (m_pDriver && m_pDriver->is_open()) {
        return m_pDriver;
    }

    if (m_strHost.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured — GRPC.CONFIG h=<host> first"));
        return nullptr;
    }
    if (m_strDescriptorSetPath.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Descriptor set not configured — GRPC.CONFIG d=<path.protoset> first"));
        return nullptr;
    }

    GrpcDriver::Config cfg;
    cfg.host             = m_strHost;
    cfg.port              = m_u16Port;
    cfg.useTls             = m_bUseTls;
    cfg.caCertPath         = m_strTlsCaPath;
    cfg.clientCertPath     = m_strTlsCertPath;
    cfg.clientKeyPath      = m_strTlsKeyPath;
    cfg.authToken          = m_strAuthToken;
    cfg.callTimeoutMs      = m_u32CallTimeout;
    cfg.connectTimeoutMs   = m_u32ConnectTimeout;

    // Resolved against ARTEFACTS_PATH exactly like SCRIPT resolves
    // scriptpathname (see ucmdexec::generic_script()) — a relative d= value
    // is looked up next to the plugin's configured artefacts directory
    // rather than the process' current working directory.
    if (!m_strArtefactsPath.empty() && !std::filesystem::path(m_strDescriptorSetPath).is_absolute()) {
        ufile::buildFilePath(m_strArtefactsPath, m_strDescriptorSetPath, cfg.protosetPath);
    } else {
        cfg.protosetPath = m_strDescriptorSetPath;
    }

    if (!ufile::fileExistsAndNotEmpty(cfg.protosetPath)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Descriptor set not found or empty:"); LOG_STRING(cfg.protosetPath));
        return nullptr;
    }

    auto driver = std::make_shared<GrpcDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GrpcDriver open failed"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

// -----------------------------------------------------------------------
// Top-level commands
// -----------------------------------------------------------------------

bool GrpcPlugin::m_GRPC_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << GRPC_PLUGIN_NAME " v" << m_strVersion
        << " host=" << m_strHost
        << " port=" << m_u16Port
        << " tls=" << (m_bUseTls ? "true" : "false")
        << " descriptorSet=" << (m_strDescriptorSetPath.empty() ? "none" : m_strDescriptorSetPath)
        << " auth=" << (m_strAuthToken.empty() ? "none" : "bearer token");
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(GRPC_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: gRPC calls (unary, server-streaming, client-streaming, bidi) against an arbitrary "
                                     "service, resolved at runtime from a compiled descriptor set (no per-service recompilation)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: GrpcProtocol (descriptors/JSON, no I/O) / grpc++ (real driver, "
                                     "undecorated) / GrpcDriver (protocol+driver glue, ICommDriver) / this plugin (CONFIG + wiring only)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the server, TLS, descriptor set and timeout parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : h=host p=port d=descriptorset.protoset [t=tls] [ca=capath] [crt=certpath] [key=keypath]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [auth=bearer_token] [ct=call_tout_ms] [xt=connect_tout_ms] [rt=read_tout] [rb=read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : GRPC.CONFIG h=localhost p=50051 d=service.protoset"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : d= is produced ahead of time with:"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           protoc --descriptor_set_out=service.protoset --include_imports service.proto"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         a relative d= path is resolved against ARTEFACTS_PATH, same as SCRIPT."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one gRPC call/step, on the plugin's single persistent channel (opened on first use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > 'CALL <package.Service/Method> [json_request]' | > 'FINISH' | < [R'pattern' or 'exact']"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage, unary            : GRPC.CMD > 'CALL greeter.Greeter/SayHello {\"name\":\"world\"}' | R'.*'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage, server-streaming : GRPC.CMD > 'CALL greeter.Greeter/SayHelloStream {\"name\":\"world\"}'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          r1 ?= GRPC.CMD < R'.*'   // repeat < for each further message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          rN ?= GRPC.CMD < R'.*'   // an empty, successful result means end of stream"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage, client-streaming : GRPC.CMD > 'CALL greeter.Greeter/Accumulate {\"name\":\"Alice\"}'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          GRPC.CMD > 'CALL greeter.Greeter/Accumulate {\"name\":\"Bob\"}'   // repeat CALL per message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          GRPC.CMD > 'FINISH'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          resp ?= GRPC.CMD < R'.*'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage, bidi-streaming   : GRPC.CMD > 'CALL echo.Echo/Chat {\"text\":\"hi\"}'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          r1 ?= GRPC.CMD < R'.*'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          GRPC.CMD > 'CALL echo.Echo/Chat {\"text\":\"again\"}'   // stream stays open"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          r2 ?= GRPC.CMD < R'.*'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          GRPC.CMD > 'FINISH'   // half-close only, no response is captured here"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                          r3 ?= GRPC.CMD < R'.*'   // keep reading to a clean end of stream"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         GRPC.CMD > 'CALL pkg.Health/Check' | R'.*'   // no request fields needed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : the response is real JSON text (see $resp above); the outer '...' is the shared script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         grammar's own STRING_DELIMITED decorator, needed because JSON's \" would otherwise be"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         parsed as this line's own field-boundary quoting; the JSON itself is never rewritten."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Only R'pattern' or an exact '...' match are supported for the response (T'.../X'.../"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         L'.../S'.../F'...' all need a live byte-stream scan this driver doesn't do); prefer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         R'pattern' — protobuf's JSON field order isn't guaranteed, and an exact match expects a"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         NUL-terminated response (see grpc_driver.hpp's \"Matching the response\"), which JSON is not."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Server/bidi-stream reads have no per-message timeout — a GRPC.CMD < on an open, quiet"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         stream blocks until the next message, end of stream, or a connection error (see grpc_driver.hpp)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         The GUI comm-dump panel shows the request/response as JSON (the real wire bytes are protobuf binary)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : periodic unary polling, same driver session as CMD"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : \"time1:val1, time2:val2, ...\" — each val is a full GRPC.CMD-style '> CALL ...' argument"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : GRPC.CYCLIC 1000:> 'CALL greeter.Greeter/Greet2 {\"name\":\"Zoe\",\"greeting\":\"Hi\"}' | R'.*'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         GRPC.CYCLIC 1000:> 'CALL greeter.Greeter/SayHello {}' | R'.*' &   // repeats until stopped"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a multi-field request body (a literal ',' inside the JSON) is fine as long as val stays"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         wrapped in '...' as shown above — CYCLIC's entry-list split is quote-aware around '...'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (see grpc_driver.hpp's \"CYCLIC\"), the same wrap CALL already always requires."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Only unary methods make sense here; a streaming CALL/FINISH \"works\" mechanically (every tick"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         just abandons and reopens/rewrites) but isn't a meaningful use of CYCLIC — use CMD instead."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[GRPC]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH   =            # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HOST             = 127.0.0.1  # gRPC server host to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PORT             = 50051      # gRPC server port to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_ENABLED      = false      # use TLS for the channel when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_CA_CERT      =            # path to the CA certificate used to verify the server (TLS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_CLIENT_CERT  =            # path to the client certificate (mutual TLS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_CLIENT_KEY   =            # path to the client private key (mutual TLS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("DESCRIPTOR_SET   =            # path to a compiled FileDescriptorSet used for reflection-free calls"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("AUTH_TOKEN       =            # bearer token sent as call credentials"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CALL_TIMEOUT     = 5000       # per-call timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONNECT_TIMEOUT  = 3000       # connect timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT     = 2000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE = 1024       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT       = false      # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED    = true       # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;
}

// -----------------------------------------------------------------------
// GRPC.CMD — see class doc comment (grpc_plugin.hpp)
// -----------------------------------------------------------------------

bool GrpcPlugin::m_GRPC_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<GrpcDriver> { return m_OpenDriver(); },
        GRPC_PLUGIN_NAME,
        m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR, &m_strResultData, m_bRawResult,
        // Non-capturing: GrpcDriver::send()/receive() are handed everything
        // they need through the driver parameter itself — see
        // grpc_driver.hpp's class doc comment.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const GrpcDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const GrpcDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief periodic unary polling — see grpc_driver.hpp's "CYCLIC" for how a multi-field
  *        request body (a comma inside '...') is handled correctly by the quote-aware
  *        entry-list split.
  *
  * \note Usage example:
  *       GRPC.CYCLIC 1000:> 'CALL greeter.Greeter/Greet2 {"name":"Zoe","greeting":"Hi"}' | R'.*'
  *       GRPC.CYCLIC 1000:> 'CALL greeter.Greeter/Greet2 {"name":"Zoe","greeting":"Hi"}' | R'.*' &
  *
  * \param[in] args  "time1:val1, time2:val2, ..." (see ucmdexec::parseCyclicArray())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool GrpcPlugin::m_GRPC_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<GrpcDriver> { return m_OpenDriver(); },
        GRPC_PLUGIN_NAME, m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR, st, m_bCyclicCached,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const GrpcDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const GrpcDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}
