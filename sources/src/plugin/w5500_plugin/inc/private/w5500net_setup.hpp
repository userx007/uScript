#ifndef W5500NET_SETUP_HPP
#define W5500NET_SETUP_HPP

#include "PluginSetup.hpp"
#include "w5500net_plugin.hpp"
#include "uPluginSettings.hpp"
#include "uCommandExec.hpp"

#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "W5500NET_P  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"
#define SERVER_IP                   "SERVER_IP"
#define SERVER_PORT                 "SERVER_PORT"
#define READ_TIMEOUT                "READ_TIMEOUT"
#define WRITE_TIMEOUT               "WRITE_TIMEOUT"
#define READ_BUFFER_SIZE            "READ_BUFFER_SIZE"

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief processing of the plugin specific settings.
  *
  * Pulls the plugin-specific keys out of the ini-backed PluginDataSet and feeds them through the
  * same setter surface the CONFIG command uses so an ini file
  * and a runtime CONFIG command are always interpreted identically
*/
/*--------------------------------------------------------------------------------------------------------*/
bool W5500NetPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "W5500NET:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? W5500NET_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,   m_strArtefactsPath);
    sSettings.Bind(SERVER_IP,        [this](const std::string& v) { setServerIp(v); return true; });
    sSettings.Bind(SERVER_PORT,      [this](const std::string& v) { return setServerPort(v); });
    sSettings.Bind(READ_TIMEOUT,     [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(WRITE_TIMEOUT,    [this](const std::string& v) { return setWriteTimeout(v); });
    sSettings.Bind(READ_BUFFER_SIZE, [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of W5500Net parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (i=ip  p=port  r=read_tout  w=write_tout  s=bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_w5500net_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "i", .voidSetter = &T::setServerIp       },
        { .key = "p", .boolSetter = &T::setServerPort     },
        { .key = "r", .boolSetter = &T::setReadTimeout    },
        { .key = "w", .boolSetter = &T::setWriteTimeout   },
        { .key = "s", .boolSetter = &T::setReadBufferSize },
        { .key = "raw", .boolSetter = &T::setRawResult },
        { .key = "cached", .boolSetter = &T::setCyclicCached },
    };

    return generic_setup_params(pOwner, args, table, "W5500NET SETUP |");
}

#endif // W5500NET_SETUP_HPP
