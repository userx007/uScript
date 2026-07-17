#include "mqtt_plugin.hpp"
#include "mqtt_driver.hpp" // Ensure this is included
#include "uBoolEvaluator.hpp"

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

// Config Command Short Keys
#define SK_HOST "h"
#define SK_PORT "p"
#define SK_TLS  "t" // t:true/false
#define SK_QOS  "q"
#define SK_RET  "r" // r:true/false
#define SK_CA   "ca"
#define SK_CRT  "crt"
#define SK_KEY  "key"

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

    auto driver = std::make_shared<MqttDriver>();

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

bool MqttPlugin::m_MQTT_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: MQTT.SCRIPT <script_file>"));
        return false;
    }

    std::string scriptFile = args;
    std::string fullPath;

    // Check if absolute path, else prepend artefacts
    if (scriptFile.find('/') == 0 || scriptFile.find('\\') == 0) {
        fullPath = scriptFile;
    } else {
        ufile::buildFilePath(m_strArtefactsPath, scriptFile, fullPath);
    }

    if (!ufile::fileExistsAndNotEmpty(fullPath)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found:"); LOG_STRING(fullPath));
        return false;
    }

    auto driver = m_OpenDriver();
    if (!driver) return false;

    // Execute script lines
    std::ifstream file(fullPath);
    std::string line;
    bool overallSuccess = true;
    int lineNum = 0;

    while (std::getline(file, line)) {
        lineNum++;
        if (line.empty() || line[0] == '#') continue; // Skip comments/empty

        // Strip carriage return if Windows line ending
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "PUBLISH") {
            std::string topic, payload;
            iss >> topic >> payload;

            if (topic.empty()) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Line ") LOG_INT32(lineNum) LOG_STRING(" invalid format"));
                continue;
            }

            if (driver->publish(topic, payload) != ICommDriver::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Line ") LOG_INT32(lineNum) LOG_STRING(" pub failed: ") LOG_STRING(topic));
                overallSuccess = false;
                break;
            }
        } else if (cmd == "WAIT" || cmd == "DELAY") {
            uint32_t ms = 100;
            iss >> ms;
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }

    if (overallSuccess) {
        m_strResultData = "Script completed successfully (" + std::to_string(lineNum) + " lines processed)";
    } else {
        m_strResultData = "Script failed at line " + std::to_string(lineNum);
    }

    return overallSuccess;
}
