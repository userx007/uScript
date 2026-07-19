#include "mqtt_plugin.hpp"
#include "mqtt_driver.hpp" // Ensure this is included
#include "uBoolEvaluator.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uFile.hpp"
#include "uString.hpp"

#include <sstream>
#include <algorithm>
#include <random>
#include <chrono>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MQTT PLUGIN |"

// INI Keys
#define K_HOST           "HOST"
#define K_PORT           "PORT"
#define K_TLS_ENABLED    "TLS_ENABLED"
#define K_QOS            "QOS"
#define K_RETAIN         "RETAIN"
#define K_TLS_CA         "TLS_CA_CERT"
#define K_TLS_CLIENT_CERT "TLS_CLIENT_CERT"
#define K_TLS_CLIENT_KEY  "TLS_CLIENT_KEY"
#define K_ARTEFACTS      "ARTEFACTS_PATH"
#define K_READ_TIMEOUT   "READ_TIMEOUT"
#define K_READ_BUFSIZE   "READ_BUFFER_SIZE"

// Config Command Short Keys
#define SK_HOST "h"
#define SK_PORT "p"
#define SK_TLS  "t" // t:true/false
#define SK_QOS  "q"
#define SK_RET  "r" // r:true/false
#define SK_CA   "ca"
#define SK_CRT  "crt"
#define SK_KEY  "key"
#define SK_RTOUT "rt" // raw read timeout (ms), used by MQTT.CMD / MQTT.SCRIPT
#define SK_RBUF  "rb" // raw read buffer size (bytes), used by MQTT.CMD / MQTT.SCRIPT

extern "C"
{
    EXPORTED MqttPlugin* pluginEntry() { return new MqttPlugin(); }
    EXPORTED void pluginExit(MqttPlugin *ptrPlugin) { delete ptrPlugin; }
}

bool MqttPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return true;
}

void MqttPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled = false;
    m_strResultData.clear();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool MqttPlugin::setParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;
    // generic_setparams assumes standard fields like FaultTolerant/Privileged
    if (generic_setparams<MqttPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
        if (m_LocalSetParams(psSetParams)) {
            bRetVal = true;
        }
    }
    return bRetVal;
}

void MqttPlugin::getParams(PluginDataGet *psGetParams) const
{
    generic_getparams<MqttPlugin>(this, psGetParams);
}

bool MqttPlugin::doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
{
    return generic_dispatch<MqttPlugin>(this, strCmd, strParams, st);
}

// --- Setters ---

void MqttPlugin::setHost(const std::string& host) const { m_strHost = host; }

bool MqttPlugin::setPort(const std::string& portStr) const
{
    uint32_t port = 0;
    if (!numeric::str2uint32(portStr, port)) return false;
    if (port > 65535) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid port:"); LOG_UINT32(port));
        return false;
    }
    m_u16Port = static_cast<uint16_t>(port);
    return true;
}

void MqttPlugin::setTlsEnabled(bool val) const { m_bUseTls = val; }

bool MqttPlugin::setQos(const std::string& qosStr) const
{
    uint8_t qos = 0;
    if (!numeric::str2uint8(qosStr, qos)) return false;
    if (qos > 2) return false;
    m_u16Qos = qos;
    return true;
}

void MqttPlugin::setRetain(bool val) const { m_bRetain = val; }
void MqttPlugin::setTlsCertPath(const std::string& path) const { m_strTlsCertPath = path; }
void MqttPlugin::setTlsKeyPath(const std::string& path) const { m_strTlsKeyPath = path; }
void MqttPlugin::setTlsCaPath(const std::string& path) const { m_strTlsCaPath = path; }

bool MqttPlugin::setReadTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
}

bool MqttPlugin::setReadBufferSize(const std::string& bufSizeStr) const
{
    uint32_t u32Size = 0;
    if (!numeric::str2uint32(bufSizeStr, u32Size)) return false;
    if (u32Size == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ReadBufSize must be > 0"));
        return false;
    }
    m_u32ReadBufferSize = u32Size;
    return true;
}

// --- Local Params ---

bool MqttPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    BoolExprEvaluator beEvaluator;

    if (psSetParams->mapSettings.empty()) return true;

    auto it = psSetParams->mapSettings.find(K_ARTEFACTS);
    if (it != psSetParams->mapSettings.end()) m_strArtefactsPath = it->second;

    it = psSetParams->mapSettings.find(K_HOST);
    if (it != psSetParams->mapSettings.end()) m_strHost = it->second;

    it = psSetParams->mapSettings.find(K_PORT);
    if (it != psSetParams->mapSettings.end()) setPort(it->second);

    it = psSetParams->mapSettings.find(K_QOS);
    if (it != psSetParams->mapSettings.end()) setQos(it->second);

    it = psSetParams->mapSettings.find(K_RETAIN);
    if (it != psSetParams->mapSettings.end()) {
        bool val = false;
        if (true == beEvaluator.evaluate( psSetParams->mapSettings.at(PLUGIN_INI_FAULT_TOLERANT), val)) {
            setRetain(val);
        }
    }

    it = psSetParams->mapSettings.find(K_TLS_ENABLED);
    if (it != psSetParams->mapSettings.end()) {
        bool val = false;
        if (true == beEvaluator.evaluate( psSetParams->mapSettings.at(PLUGIN_INI_FAULT_TOLERANT), val)) {
            setTlsEnabled(val);
        }
    }

    it = psSetParams->mapSettings.find(K_TLS_CA);
    if (it != psSetParams->mapSettings.end()) m_strTlsCaPath = it->second;

    it = psSetParams->mapSettings.find(K_TLS_CLIENT_CERT);
    if (it != psSetParams->mapSettings.end()) m_strTlsCertPath = it->second;

    it = psSetParams->mapSettings.find(K_TLS_CLIENT_KEY);
    if (it != psSetParams->mapSettings.end()) m_strTlsKeyPath = it->second;

    it = psSetParams->mapSettings.find(K_READ_TIMEOUT);
    if (it != psSetParams->mapSettings.end()) setReadTimeout(it->second);

    it = psSetParams->mapSettings.find(K_READ_BUFSIZE);
    if (it != psSetParams->mapSettings.end()) setReadBufferSize(it->second);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost)
              LOG_STRING(" TLS:") LOG_BOOL(m_bUseTls));
    return true;
}

// --- Driver Helper ---

std::shared_ptr<MqttDriver> MqttPlugin::m_OpenDriver(void) const
{
    if (m_strHost.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured"));
        return nullptr;
    }

    auto driver = std::make_shared<MqttDriver>(m_strHost);

    MqttDriver::Config cfg;
    cfg.host = m_strHost;
    cfg.port = m_u16Port;
    cfg.connectTimeoutMs = 5000;
    cfg.useTls = m_bUseTls;
    cfg.caCertPath = m_strTlsCaPath;
    cfg.clientCertPath = m_strTlsCertPath;
    cfg.clientKeyPath = m_strTlsKeyPath;
    cfg.clientId = m_strClientId.empty() ? "mqtt_pub_plugin_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) : m_strClientId;
    cfg.keepAlive = 60;
    cfg.qos = m_u16Qos;
    cfg.retain = m_bRetain;

    if (driver->open(cfg) != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Driver Open failed"));
        return nullptr;
    }

    // Attempt to connect MQTT session
    if (driver->connect() != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MQTT Connect failed"));
        driver->close();
        return nullptr;
    }

    return driver;
}

// --- Commands ---

bool MqttPlugin::m_MQTT_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << MQTT_PLUGIN_NAME " v" << m_strVersion
        << " host=" << m_strHost
        << " port=" << m_u16Port
        << " tls=" << (m_bUseTls ? "true" : "false")
        << " qos=" << (int)m_u16Qos;
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(MQTT_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: publish to / script against an MQTT v3.1.1 broker"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the broker host, port, TLS and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h:host] [p:port] [q:qos] [t:tls] [r:retain] [ca:capath] [crt:certpath] [key:keypath] [rt:read_tout] [rb:read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CONFIG h:broker.local p:1883 q:1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PUB    : connect, publish one message and disconnect"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : topic payload"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.PUB sensors/temp 21.5"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both, on a live MQTT session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh CONNECT/CONNACK session is opened for CMD and closed once it completes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file over a live MQTT session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.SCRIPT script.txt"));
    LOG_SEP();

    return true;
}

bool MqttPlugin::m_MQTT_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing config args"));
        return false;
    }

    std::istringstream stream(args);
    std::string token;
    bool bRetVal = true;
    BoolExprEvaluator beEvaluator;

    while (stream >> token) {
        auto eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = token.substr(0, eqPos);
        std::string val = token.substr(eqPos + 1);

        if (key == SK_HOST) m_strHost = val;
        else if (key == SK_PORT) {
            if (!setPort(val)) bRetVal = false;
        }
        else if (key == SK_QOS) {
            if (!setQos(val)) bRetVal = false;
        }
        else if (key == SK_TLS) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) {
                setTlsEnabled(b);
            } else { bRetVal = false; }
        }
        else if (key == SK_RET) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) {
                setRetain(b);
            } else { bRetVal = false; }
        }
        else if (key == SK_CA) {
            setTlsCaPath(val);
        }
        else if (key == SK_CRT) {
            setTlsCertPath(val);
        }
        else if (key == SK_KEY) {
            setTlsKeyPath(val);
        }
        else if (key == SK_RTOUT) {
            if (!setReadTimeout(val)) bRetVal = false;
        }
        else if (key == SK_RBUF) {
            if (!setReadBufferSize(val)) bRetVal = false;
        }
    }
    return bRetVal;
}

bool MqttPlugin::m_MQTT_PUB(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: MQTT.PUB <topic> <payload>"));
        return false;
    }

    // Parse topic and payload. Simple split by first space.
    auto spacePos = args.find(' ');
    if (spacePos == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing payload for topic"));
        return false;
    }

    std::string topic = args.substr(0, spacePos);
    std::string payload = args.substr(spacePos + 1);

    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty topic"));
        return false;
    }

    auto driver = m_OpenDriver();
    if (!driver) return false;

    bool success = driver->publish(topic, payload, m_u16Qos, m_bRetain) == ICommDriver::Status::SUCCESS;

    if (!success) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Publish failed for topic:"); LOG_STRING(topic));
        return false;
    }

    m_strResultData = "Published to " + topic;
    return true;
}

// -----------------------------------------------------------------------
// MQTT.CMD: open a connection (TCP + TLS + MQTT CONNECT/CONNACK) and run a
// single send/receive command against the live session, the MQTT analogue
// of TCPIPPlugin::m_TCPIP_CMD. Command parsing/execution is delegated to
// the same shared CommScriptCommandValidator / CommScriptCommandInterpreter
// used by TCPIP (and UART), operating on MqttDriver's raw ICommDriver
// pass-through (see MqttDriver::tout_read/tout_write) rather than on the
// higher-level publish()/connect() API - i.e. this is a raw byte-level
// diagnostic command running on top of an already-negotiated MQTT session.
//
// Usage example:
//   MQTT.CMD > Hello | ok                   // send "Hello", expect to read back "ok"
//   MQTT.CMD < "Please send!" | Sending...   // wait to receive "Please send!", send back "Sending..."
// -----------------------------------------------------------------------
bool MqttPlugin::m_MQTT_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;

    bool bRetVal = false;

    resetData();

    do {
        if (args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing command"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (!m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        try {
            // open TCP + TLS + MQTT session (per-invocation; closed by driver's destructor)
            auto driver = m_OpenDriver();

            if (driver) {
                CommScriptCommandValidator validator;
                CommCommand command;

                if (true == validator.validateCommand(0, args, command)) {
                    CommScriptCommandInterpreter<MqttDriver> interpreter(
                        driver,
                        m_u32ReadBufferSize,
                        m_u32ReadTimeout
                    );
                    bRetVal = interpreter.interpretCommand(command, m_bIsEnabled);
                }
            }
        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while (false);

    return bRetVal;
}

// -----------------------------------------------------------------------
// MQTT.SCRIPT: run a scripted sequence of raw sends/receives over a single
// MQTT session, the MQTT analogue of TCPIPPlugin::m_TCPIP_SCRIPT. As with
// MQTT.CMD, this drives MqttDriver's ICommDriver pass-through via
// CommScriptClient<MqttDriver> rather than a bespoke PUBLISH/WAIT line
// parser, so MQTT.SCRIPT files use the same send/expect grammar as
// TCPIP.SCRIPT / UART.SCRIPT.
//
// Usage example:
//   MQTT.SCRIPT scriptname [|delay]
// -----------------------------------------------------------------------
bool MqttPlugin::m_MQTT_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;

    bool bRetVal = false;

    resetData();

    do {
        // expected to have as parameter the name of the script
        if (args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if ((szNrArgs < 1) || (szNrArgs > 2)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: scriptpathname [|delay] "));
            break;
        }

        size_t szDelay = 0;
        if (2 == szNrArgs) {
            if (!numeric::str2sizet(vstrArgs[1], szDelay)) {
                break;
            }
        }

        std::string strScriptPathName;
        ufile::buildFilePath(m_strArtefactsPath, vstrArgs[0], strScriptPathName);

        if (!ufile::fileExistsAndNotEmpty(strScriptPathName)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
            break;
        }

        try {
            // open TCP + TLS + MQTT session (per-invocation; closed by driver's destructor)
            auto driver = m_OpenDriver();

            if (driver) {
                CommScriptClient<MqttDriver> client(
                    strScriptPathName,
                    driver,
                    m_u32ReadBufferSize,   // szMaxRecvSize
                    m_u32ReadTimeout,      // u32DefaultTimeout
                    szDelay                // szDelay
                );
                bRetVal = client.execute(m_bIsEnabled);
            }
        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while (false);

    return bRetVal;
}
