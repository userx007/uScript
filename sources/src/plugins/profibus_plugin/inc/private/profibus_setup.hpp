#ifndef PROFIBUS_SETUP_HPP
#define PROFIBUS_SETUP_HPP
#include "profibus_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR  "PROFIBUS_P  |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define K_DEVICE          "DEVICE"
#define K_BAUD            "BAUD"
#define K_OWN_ADDRESS     "OWN_ADDRESS"
#define K_RESPONSE_TOUT   "RESPONSE_TIMEOUT"
#define K_HIGH_PRIORITY   "HIGH_PRIORITY"
#define K_READ_BUFSIZE    "READ_BUFFER_SIZE"
#define K_ARTEFACTS       "ARTEFACTS_PATH"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIG COMMAND SHORT KEYS                                  //
/////////////////////////////////////////////////////////////////////////////////
// See the usage string in m_PROFIBUS_INFO() (profibus_plugin.cpp): "[d=device]
// [b=baud] [a=own_address] [rt=response_tout] [hp=high_priority] [rb=read_bufsize]"

#define SK_DEVICE "d"
#define SK_BAUD   "b"
#define SK_ADDR   "a"
#define SK_RTOUT  "rt"
#define SK_HPRIO  "hp"
#define SK_RBUF   "rb"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief processing of the plugin specific settings.
  *
  * Pulls the plugin-specific keys out of the ini-backed PluginDataSet and feeds them through the
  * same setter surface the CONFIG command uses so an ini file
  * and a runtime CONFIG command are always interpreted identically
*/
/*--------------------------------------------------------------------------------------------------------*/
bool ProfibusPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "PROFIBUS:1"); falls back
    // to the fixed plugin name if the interpreter didn't supply one.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? PROFIBUS_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,     m_strArtefactsPath);
    sSettings.Bind(K_DEVICE,        m_strDevice);
    sSettings.Bind(K_BAUD,          [this](const std::string& v) { return setBaud(v); });
    sSettings.Bind(K_OWN_ADDRESS,   [this](const std::string& v) { return setOwnAddress(v); });
    sSettings.Bind(K_RESPONSE_TOUT, [this](const std::string& v) { return setResponseTimeout(v); });
    // setDefaultHighPriority() takes a plain bool (unlike the other setters
    // here, which parse the string themselves), so evaluate the ini value
    // locally first - same pattern m_PROFIBUS_CONFIG() below uses for SK_HPRIO.
    sSettings.Bind(K_HIGH_PRIORITY, [this](const std::string& v) {
        bool b = false; BoolExprEvaluator e;
        if (!e.evaluate(v, b)) return false;
        setDefaultHighPriority(b); return true;
    });
    sSettings.Bind(K_READ_BUFSIZE,  [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Device:"); LOG_STRING(m_strDevice)
              LOG_STRING("Baud:"); LOG_UINT32(m_u32Baud));
    return true;
}


bool ProfibusPlugin::m_PROFIBUS_CONFIG(const std::string& args, std::stop_token st) const
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

        if (!val.empty() && val[0] == '$') {
            // Unexpanded macro reference during script VALIDATION (dry run) —
            // real execution always resolves $macros before the plugin sees
            // the string; defer the actual value check to then.
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(val);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        if (key == SK_DEVICE) setDevice(val);
        else if (key == SK_BAUD)  { if (!setBaud(val))  bRetVal = false; }
        else if (key == SK_ADDR)  { if (!setOwnAddress(val)) bRetVal = false; }
        else if (key == SK_RTOUT) { if (!setResponseTimeout(val)) bRetVal = false; }
        else if (key == SK_HPRIO) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setDefaultHighPriority(b);
        }
        else if (key == SK_RBUF)  { if (!setReadBufferSize(val)) bRetVal = false; }
        else if (key == ucmdexec::RAW_RESULT_CONFIG_KEY) { if (!setRawResult(val)) bRetVal = false; }
        else if (key == ucmdexec::CYCLIC_CACHED_CONFIG_KEY) { if (!setCyclicCached(val)) bRetVal = false; }
    }
    return bRetVal;
}


#endif // PROFIBUS_SETUP_HPP
