#ifndef MODBUS_SETUP_HPP
#define MODBUS_SETUP_HPP

#include "modbus_plugin.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MODBUS PLUGIN |"

// INI Keys
#define K_HOST           "HOST"
#define K_PORT           "PORT"
#define K_ARTEFACTS      "ARTEFACTS_PATH"
#define K_READ_TIMEOUT   "READ_TIMEOUT"
#define K_READ_BUFSIZE   "READ_BUFFER_SIZE"

// Config Command Short Keys
#define SK_HOST  "h"
#define SK_PORT  "p"
#define SK_RTOUT "rt"
#define SK_RBUF  "rb"

// --- Setters requiring validation ---

bool ModbusPlugin::setPort(const std::string& portStr) const
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

bool ModbusPlugin::setReadTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
}

bool ModbusPlugin::setReadBufferSize(const std::string& bufSizeStr) const
{
    uint32_t sz = 0;
    if (!numeric::str2uint32(bufSizeStr, sz)) return false;
    if (sz == 0) return false;
    m_u32ReadBufferSize = sz;
    return true;
}

// --- Local Params ---

bool ModbusPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "MODBUS:1"); falls back
    // to the fixed plugin name if the interpreter didn't supply one.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? MODBUS_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,    m_strArtefactsPath);
    sSettings.Bind(K_HOST,         m_strHost);
    sSettings.Bind(K_PORT,         [this](const std::string& v) { return setPort(v); });
    sSettings.Bind(K_READ_TIMEOUT, [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE, [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost));
    return true;
}


bool ModbusPlugin::m_MODBUS_CONFIG(const std::string& args, std::stop_token st) const
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

    while (stream >> token) {
        auto eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = token.substr(0, eqPos);
        std::string val = token.substr(eqPos + 1);

        if (!val.empty() && val[0] == '$') {
            // Unexpanded macro reference during script VALIDATION (dry run) —
            // real execution always resolves $macros before the plugin sees
            // the string; defer the actual value check to then.
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(val);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        if (key == SK_HOST) setHost(val);
        else if (key == SK_PORT)  { if (!setPort(val)) bRetVal = false; }
        else if (key == SK_RTOUT) { if (!setReadTimeout(val)) bRetVal = false; }
        else if (key == SK_RBUF)  { if (!setReadBufferSize(val)) bRetVal = false; }
        else if (key == ucmdexec::RAW_RESULT_CONFIG_KEY) { if (!setRawResult(val)) bRetVal = false; }
        else if (key == ucmdexec::CYCLIC_CACHED_CONFIG_KEY) { if (!setCyclicCached(val)) bRetVal = false; }
    }
    return bRetVal;
}


#endif // MODBUS_SETUP_HPP
