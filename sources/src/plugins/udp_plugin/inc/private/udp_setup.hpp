#ifndef UDP_SETUP_HPP
#define UDP_SETUP_HPP
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"
#include "udp_plugin.hpp"

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

#define LT_HDR               "UDP_P       |"
#define LOG_HDR              LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH       "ARTEFACTS_PATH"
#define UDP_HOST             "UDP_HOST"
#define UDP_PORT             "UDP_PORT"
#define UDP_CONNECT_TIMEOUT  "UDP_CONNECT_TIMEOUT"
#define UDP_READ_TIMEOUT     "UDP_READ_TIMEOUT"
#define UDP_WRITE_TIMEOUT    "UDP_WRITE_TIMEOUT"
#define UDP_READ_BUFFER_SIZE "UDP_READ_BUFFER_SIZE"

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
bool UDPPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? UDP_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing found in the ini file"));
        return true;
    }

    PluginSettingsBinder sSettings;

    // clang-format off
    sSettings.Bind(UDP_HOST,                        [this](const std::string &v) { setUdpHost(v); return true; });
    sSettings.Bind(UDP_PORT,                        [this](const std::string &v) { return setUdpPort(v); });
    sSettings.Bind(UDP_CONNECT_TIMEOUT,             [this](const std::string &v) { return setConnectTimeout(v); });
    sSettings.Bind(UDP_READ_TIMEOUT,                [this](const std::string &v) { return setReadTimeout(v); });
    sSettings.Bind(UDP_WRITE_TIMEOUT,               [this](const std::string &v) { return setWriteTimeout(v); });
    sSettings.Bind(UDP_READ_BUFFER_SIZE,            [this](const std::string &v) { return setUdpReadBufferSize(v); });
    sSettings.Bind(ARTEFACTS_PATH,                  m_strArtefactsPath);
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY,    m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);
    // clang-format on

    return sSettings.Apply(psSetParams->mapSettings,
                           [](const std::string &strKey, const std::string &strRawValue) {
                               LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
                           });

} /* m_LocalSetParams() */

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
bool generic_udp_set_params(const T *pOwner, const std::string &strArgs)
{
    // clang-format off
    static constexpr KVSetterEntry<T> table[] = {
        {.key = "h",        .voidSetter = &T::setUdpHost},
        {.key = "p",        .boolSetter = &T::setUdpPort},
        {.key = "c",        .boolSetter = &T::setConnectTimeout},
        {.key = "r",        .boolSetter = &T::setReadTimeout},
        {.key = "w",        .boolSetter = &T::setWriteTimeout},
        {.key = "s",        .boolSetter = &T::setUdpReadBufferSize},
        {.key = "raw",      .boolSetter = &T::setRawResult},
        {.key = "cached",   .boolSetter = &T::setCyclicCached},
    };
    // clang-format on

    return generic_setup_params(pOwner, strArgs, table, LT_HDR);
}

#endif // UDP_SETUP_HPP
