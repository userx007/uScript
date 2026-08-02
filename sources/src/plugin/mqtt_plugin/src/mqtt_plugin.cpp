#include "mqtt_plugin.hpp"
#include "mqtt_driver.hpp" // Ensure this is included
#include "uBoolEvaluator.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

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
#define K_SHARE_SESSION  "SHARE_SESSION"      // see MqttPlugin::getShareSession()
#define K_RECEIVE_TOPIC  "RECEIVE_TOPIC"      // see MqttPlugin::getReceiveIncludeTopic()

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
#define SK_SHARE "ss" // t/f — share one connection between CMD/SCRIPT and SUBSCRIBE/RECEIVE
#define SK_RTOPIC "it" // t/f — RECEIVE stores "topic:payload" instead of just "payload"

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
    if (m_pPersistentDriver) {
        m_pPersistentDriver->disconnect();
        m_pPersistentDriver->close();
        m_pPersistentDriver.reset();
    }
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
    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS, m_strArtefactsPath);
    sSettings.Bind(K_HOST,      m_strHost);
    sSettings.Bind(K_PORT,      [this](const std::string& v) { return setPort(v); });
    sSettings.Bind(K_QOS,       [this](const std::string& v) { return setQos(v); });
    // NOTE: the previous implementation evaluated RETAIN and TLS_ENABLED against
    // PLUGIN_INI_FAULT_TOLERANT's value instead of their own key's value (and would
    // throw if that unrelated key was absent from the ini file). Fixed here: each
    // binding now evaluates its own key's raw value.
    sSettings.Bind(K_RETAIN, [this](const std::string& v) {
        BoolExprEvaluator beEvaluator;
        bool bVal = false;
        if (false == beEvaluator.evaluate(v, bVal)) {
            return false;
        }
        setRetain(bVal);
        return true;
    });
    sSettings.Bind(K_TLS_ENABLED, [this](const std::string& v) {
        BoolExprEvaluator beEvaluator;
        bool bVal = false;
        if (false == beEvaluator.evaluate(v, bVal)) {
            return false;
        }
        setTlsEnabled(bVal);
        return true;
    });
    sSettings.Bind(K_TLS_CA,          m_strTlsCaPath);
    sSettings.Bind(K_TLS_CLIENT_CERT, m_strTlsCertPath);
    sSettings.Bind(K_TLS_CLIENT_KEY,  m_strTlsKeyPath);
    sSettings.Bind(K_READ_TIMEOUT,    [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE,    [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(K_SHARE_SESSION,   m_bShareSession);
    sSettings.Bind(K_RECEIVE_TOPIC,   m_bReceiveIncludeTopic);

    // best-effort, accumulate mode: matches the original behaviour of attempting
    // every key regardless of earlier failures, and always returning true
    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost)
              LOG_STRING(" TLS:") LOG_BOOL(m_bUseTls));
    return true;
}

// --- Driver Helper ---

std::shared_ptr<MqttDriver> MqttPlugin::m_OpenFreshDriver(void) const
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

// Used by MQTT.CMD/SCRIPT (via their lambda passed to ucmdexec::generic_cmd/
// generic_script) to obtain the driver to publish on. Per m_bShareSession
// (CONFIG "ss:"/INI SHARE_SESSION, default false): either a fresh,
// short-lived connection as before (default), or the same persistent
// connection MQTT.SUBSCRIBE/MQTT.RECEIVE use — see m_GetOrOpenPersistentDriver().
std::shared_ptr<MqttDriver> MqttPlugin::m_OpenDriver(void) const
{
    if (m_bShareSession) {
        return m_GetOrOpenPersistentDriver();
    }
    return m_OpenFreshDriver();
}

// Used by MQTT.SUBSCRIBE (always) and by m_OpenDriver() (only when
// m_bShareSession is set): returns the one persistent connection, opening
// (and connecting) it first if this is the first call to need it. Unlike
// m_OpenFreshDriver()'s fresh-per-call instances, m_pPersistentDriver
// itself is a plugin member, so it survives across calls — including calls
// made from a background thread via "name ?= MQTT.RECEIVE &" (see
// mqtt_plugin.hpp's m_pPersistentDriver doc comment).
std::shared_ptr<MqttDriver> MqttPlugin::m_GetOrOpenPersistentDriver(void) const
{
    if (m_pPersistentDriver && m_pPersistentDriver->is_open()) {
        return m_pPersistentDriver;
    }
    m_pPersistentDriver = m_OpenFreshDriver();
    return m_pPersistentDriver;
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [q=qos] [t=tls] [r=retain] [ca=capath] [crt=certpath] [key=keypath] [rt=read_tout] [rb=read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CONFIG h=broker.local p=1883 q=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : ss=true makes CMD/SCRIPT publish on the same persistent connection SUBSCRIBE/RECEIVE use (default false: CMD/SCRIPT always opens its own fresh connection)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         it=true makes RECEIVE store \"topic:payload\" instead of just \"payload\" (default false)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : publish one message, on a live MQTT session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > payload ~ topic [| expected_ack]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CMD > H\"48656C6C6F\" ~ sensors/temp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD > \"21.5\" ~ sensors/temp | PUBACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : topic is required and always comes from '~' (xtra_params) — there is no default topic."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         QoS/retain come from CONFIG (q=/r=), not from the CMD line itself."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         QoS 0 publishes have no acknowledgement — '| expected_ack' only makes sense for QoS 1 ('PUBACK') or QoS 2 ('PUBCOMP')."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh CONNECT/CONNACK session is opened for CMD and closed once it completes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : publish (and assert acks for) several messages from a script file over one live MQTT session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SUBSCRIBE : send SUBSCRIBE for one topic filter and wait for SUBACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args      : topic [qos]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage     : MQTT.SUBSCRIBE sensors/temp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("            MQTT.SUBSCRIBE sensors/# 1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note      : opens (or reuses) the same persistent connection MQTT.RECEIVE reads from; may be called more than once for more topics"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RECEIVE : wait for one message on an active subscription and store it in a variable macro"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args    : (none)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage   : MQTT.SUBSCRIBE sensors/temp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("          temp ?= MQTT.RECEIVE          // one message, blocks up to rt: read timeout"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("          temp ?= MQTT.RECEIVE &        // background thread; $temp always holds the latest message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note    : requires an active MQTT.SUBSCRIBE first — fails immediately otherwise"));
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

        // A value that still starts with '$' is an unexpanded "$macroname"
        // (or "$macroname.SIZE") reference — this call is happening during
        // script VALIDATION (a dry run), before the referenced variable
        // macro has a real value yet. Real execution always resolves every
        // $macro before the plugin ever sees the string (see
        // ScriptInterpreter::m_executeCommand()'s real-exec vs. dry-run
        // branches). Accept the key and defer the actual value/range check
        // to real execution.
        if (!val.empty() && val[0] == '$') {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(val);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

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
        else if (key == SK_SHARE) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) {
                setShareSession(b);
            } else { bRetVal = false; }
        }
        else if (key == SK_RTOPIC) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) {
                setReceiveIncludeTopic(b);
            } else { bRetVal = false; }
        }
    }
    return bRetVal;
}

// -----------------------------------------------------------------------
// MQTT.CMD: open a connection (TCP + TLS + MQTT CONNECT/CONNACK) and run a
// single publish (+ optional ack wait) against the live session, the MQTT
// analogue of TCPIPPlugin::m_TCPIP_CMD. Command parsing/execution is
// delegated to the same shared CommScriptCommandValidator /
// CommScriptCommandInterpreter used by TCPIP (and UART), operating on
// MqttDriver's ICommDriver surface — MqttDriver::tout_write() builds and
// sends the actual PUBLISH packet (topic from '~ xtra_params', payload
// from the '>' data, QoS/retain from CONFIG), and MqttDriver::tout_read()
// waits for whatever acknowledgement that publish made outstanding
// (nothing for QoS 0, PUBACK for QoS 1, or the full PUBREC/PUBREL/PUBCOMP
// handshake for QoS 2 — see mqtt_driver.hpp's class doc comment). This is
// the only way to publish: there is no separate PUBLISH-style command.
//
// Usage example:
//   MQTT.CMD > H"48656C6C6F" ~ sensors/temp             // QoS 0: fire-and-forget
//   MQTT.CMD > "21.5" ~ sensors/temp | PUBACK            // QoS 1: publish, assert the ack
// -----------------------------------------------------------------------
bool MqttPlugin::m_MQTT_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<MqttDriver> {
            // open TCP + TLS + MQTT session (per-invocation; closed by driver's destructor)
            return m_OpenDriver();
        },
        MQTT_PLUGIN_NAME,
        m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR, &m_strResultData);
}

// -----------------------------------------------------------------------
// MQTT.SCRIPT: run a scripted sequence of publishes (+ optional ack
// assertions) over a single MQTT session, the MQTT analogue of
// TCPIPPlugin::m_TCPIP_SCRIPT. As with MQTT.CMD, this drives MqttDriver's
// ICommDriver publish/ack surface via CommScriptClient<MqttDriver> rather
// than a bespoke PUBLISH/WAIT line
// parser, so MQTT.SCRIPT files use the same send/expect grammar as
// TCPIP.SCRIPT / UART.SCRIPT.
//
// Usage example:
//   MQTT.SCRIPT scriptname [|delay]
// -----------------------------------------------------------------------
bool MqttPlugin::m_MQTT_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<MqttDriver> {
            // open TCP + TLS + MQTT session (per-invocation; closed by driver's destructor)
            return m_OpenDriver();
        },
        MQTT_PLUGIN_NAME,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR);
}

// -----------------------------------------------------------------------
// MQTT.SUBSCRIBE: send SUBSCRIBE for one topic filter and wait for SUBACK,
// on the persistent connection MQTT.RECEIVE later reads from (see
// m_GetOrOpenPersistentDriver() / mqtt_driver.hpp's subscribe() doc
// comment). May be called more than once, including with different topic
// filters, to build up several subscriptions on the same connection.
//
// Usage example:
//   MQTT.SUBSCRIBE sensors/temp        // QoS defaults to CONFIG's q:
//   MQTT.SUBSCRIBE sensors/# 1         // wildcard filter, QoS 1 requested
// -----------------------------------------------------------------------
bool MqttPlugin::m_MQTT_SUBSCRIBE(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: MQTT.SUBSCRIBE <topic> [qos]"));
        return false;
    }

    std::istringstream iss(args);
    std::string topic;
    std::string qosStr;
    iss >> topic >> qosStr;

    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing topic filter"));
        return false;
    }

    uint8_t qos = m_u16Qos;
    if (!qosStr.empty()) {
        uint32_t qosVal = 0;
        if (!numeric::str2uint32(qosStr, qosVal) || qosVal > 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid qos (must be 0-2):"); LOG_STRING(qosStr));
            return false;
        }
        qos = static_cast<uint8_t>(qosVal);
    }

    auto driver = m_GetOrOpenPersistentDriver();
    if (!driver) {
        return false;
    }

    if (driver->subscribe(topic, qos) != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE failed for topic:"); LOG_STRING(topic));
        return false;
    }

    m_strResultData = "Subscribed to " + topic;
    return true;
}

// -----------------------------------------------------------------------
// MQTT.RECEIVE: wait for one incoming message on the persistent
// subscription connection (see MQTT.SUBSCRIBE above) and store it —
// "topic:payload" or just "payload", per CONFIG's it:/INI RECEIVE_TOPIC,
// default just "payload" — in the destination variable macro.
//
// Called plain, this blocks (up to CONFIG's rt: read timeout) for exactly
// one message and returns. Called as "event ?= MQTT.RECEIVE &" instead,
// the core script engine repeats it on a background thread, so $event
// always reflects whichever message arrived most recently — see
// src/script/core/README.md's "Threaded variable macros" section, and
// mqtt_plugin.hpp's m_pPersistentDriver doc comment for why that requires
// a persistent (rather than fresh-per-call) connection.
//
// Requires an active MQTT.SUBSCRIBE on this connection — fails immediately
// if none has been made (there would be nothing to ever receive).
//
// Usage example:
//   MQTT.SUBSCRIBE sensors/temp
//   temp ?= MQTT.RECEIVE &   // background thread; $temp tracks the latest reading
// -----------------------------------------------------------------------
bool MqttPlugin::m_MQTT_RECEIVE(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();

    if (!m_pPersistentDriver || !m_pPersistentDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No active subscription — call MQTT.SUBSCRIBE first"));
        return false;
    }

    std::string topic;
    std::string payload;
    auto status = m_pPersistentDriver->receiveMessage(m_u32ReadTimeout, topic, payload);
    if (status != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("RECEIVE failed:"); LOG_STRING(ICommDriver::to_string(status)));
        return false;
    }

    m_strResultData = m_bReceiveIncludeTopic ? (topic + ":" + payload) : payload;
    return true;
}
