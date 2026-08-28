#ifndef TCPIP_SETUP_HPP
#define TCPIP_SETUP_HPP

#include "PluginSetup.hpp"
#include "tcpip_plugin.hpp"
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

#define LT_HDR   "TCP-IP_P    |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"
#define TCP_HOST                    "TCP_HOST"
#define TCP_PORT                    "TCP_PORT"
#define TCP_CONNECT_TIMEOUT         "TCP_CONNECT_TIMEOUT"
#define TCP_READ_TIMEOUT            "TCP_READ_TIMEOUT"
#define TCP_WRITE_TIMEOUT           "TCP_WRITE_TIMEOUT"
#define TCP_READ_BUFFER_SIZE        "TCP_READ_BUFFER_SIZE"

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
bool TCPIPPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "TCPIP:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? TCPIP_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,       m_strArtefactsPath);
    sSettings.Bind(TCP_HOST,             [this](const std::string& v) { setTcpHost(v); return true; });
    sSettings.Bind(TCP_PORT,             [this](const std::string& v) { return setTcpPort(v); });
    sSettings.Bind(TCP_CONNECT_TIMEOUT,  [this](const std::string& v) { return setConnectTimeout(v); });
    sSettings.Bind(TCP_READ_TIMEOUT,     [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(TCP_WRITE_TIMEOUT,    [this](const std::string& v) { return setWriteTimeout(v); });
    // Route through the setter so the [1-TCPIP_MAX_BUFLENGTH] range check is
    // applied consistently regardless of whether the value came from the ini
    // file or from the CONFIG command.
    sSettings.Bind(TCP_READ_BUFFER_SIZE, [this](const std::string& v) { return setTcpReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of TCP parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  c=connect_tout  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_tcp_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h", .voidSetter = &T::setTcpHost           },
        { .key = "p", .boolSetter = &T::setTcpPort            },
        { .key = "c", .boolSetter = &T::setConnectTimeout     },
        { .key = "r", .boolSetter = &T::setReadTimeout        },
        { .key = "w", .boolSetter = &T::setWriteTimeout       },
        { .key = "s", .boolSetter = &T::setTcpReadBufferSize  },
        { .key = "raw", .boolSetter = &T::setRawResult },
        { .key = "cached", .boolSetter = &T::setCyclicCached },
    };

    return generic_setup_params(pOwner, args, table, "TCPIP SETUP |");
}

#endif // TCPIP_SETUP_HPP
