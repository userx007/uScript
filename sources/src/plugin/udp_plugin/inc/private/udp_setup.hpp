#ifndef UDP_SETUP_HPP
#define UDP_SETUP_HPP

#include "PluginSetup.hpp"
#include "udp_plugin.hpp"
#include "uPluginSettings.hpp"
#include "uCommandExec.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of UDP parameters expressed as a space-separated key=value string.
 *
 * \note This is the CONFIG-time key=value grammar (default peer + timeouts).
 *       It is deliberately distinct from CMD's "d:host:port <payload>"
 *       destination-override token (see UDPPlugin::m_SplitDestOverride),
 *       which selects a one-off peer for a single datagram rather than
 *       reconfiguring the plugin's default peer.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  c=connect_tout  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_udp_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h", .voidSetter = &T::setUdpHost           },
        { .key = "p", .boolSetter = &T::setUdpPort            },
        { .key = "c", .boolSetter = &T::setConnectTimeout     },
        { .key = "r", .boolSetter = &T::setReadTimeout        },
        { .key = "w", .boolSetter = &T::setWriteTimeout       },
        { .key = "s", .boolSetter = &T::setUdpReadBufferSize  },
        { .key = "raw", .boolSetter = &T::setRawResult },
        { .key = "cached", .boolSetter = &T::setCyclicCached },
    };

    return generic_setup_params(pOwner, args, table, "UDP SETUP |");
}

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"                          
#define UDP_HOST                    "UDP_HOST"
#define UDP_PORT                    "UDP_PORT"                 
#define UDP_CONNECT_TIMEOUT         "UDP_CONNECT_TIMEOUT"         
#define UDP_READ_TIMEOUT            "UDP_READ_TIMEOUT"            
#define UDP_WRITE_TIMEOUT           "UDP_WRITE_TIMEOUT"           
#define UDP_READ_BUFFER_SIZE        "UDP_READ_BUFFER_SIZE"


///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "UDP:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? UDP_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,       m_strArtefactsPath);
    sSettings.Bind(UDP_HOST,             [this](const std::string& v) { setUdpHost(v); return true; });
    sSettings.Bind(UDP_PORT,             [this](const std::string& v) { return setUdpPort(v); });
    sSettings.Bind(UDP_CONNECT_TIMEOUT,  [this](const std::string& v) { return setConnectTimeout(v); });
    sSettings.Bind(UDP_READ_TIMEOUT,     [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(UDP_WRITE_TIMEOUT,    [this](const std::string& v) { return setWriteTimeout(v); });
    // Route through the setter so the [1-UDP_MAX_DGRAM_LEN] range check is
    // applied consistently regardless of whether the value came from the ini
    // file or from the CONFIG command.
    sSettings.Bind(UDP_READ_BUFFER_SIZE, [this](const std::string& v) { return setUdpReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */

#endif // UDP_SETUP_HPP
