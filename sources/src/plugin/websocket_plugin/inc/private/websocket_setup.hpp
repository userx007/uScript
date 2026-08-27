#ifndef WEBSOCKET_SETUP_HPP
#define WEBSOCKET_SETUP_HPP

#include "PluginSetup.hpp"
#include "websocket_plugin.hpp"
#include "uPluginSettings.hpp"
#include "uCommandExec.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of WebSocket parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  u=path  o=subprotocol  c=connect_tout  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_websocket_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h", .voidSetter = &T::setWsHost           },
        { .key = "p", .boolSetter = &T::setWsPort            },
        { .key = "u", .boolSetter = &T::setWsPath             },
        { .key = "o", .voidSetter = &T::setWsSubprotocol     },
        { .key = "c", .boolSetter = &T::setConnectTimeout     },
        { .key = "r", .boolSetter = &T::setReadTimeout        },
        { .key = "w", .boolSetter = &T::setWriteTimeout       },
        { .key = "s", .boolSetter = &T::setWsReadBufferSize   },
        { .key = "raw", .boolSetter = &T::setRawResult },
        { .key = "cached", .boolSetter = &T::setCyclicCached },
    };

    return generic_setup_params(pOwner, args, table, "WEBSOCKET SETUP |");
}

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"
#define WS_HOST                     "WS_HOST"
#define WS_PORT                     "WS_PORT"
#define WS_PATH                     "WS_PATH"
#define WS_SUBPROTOCOL              "WS_SUBPROTOCOL"
#define WS_CONNECT_TIMEOUT          "WS_CONNECT_TIMEOUT"
#define WS_READ_TIMEOUT             "WS_READ_TIMEOUT"
#define WS_WRITE_TIMEOUT            "WS_WRITE_TIMEOUT"
#define WS_READ_BUFFER_SIZE         "WS_READ_BUFFER_SIZE"


///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
bool WEBSOCKETPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "WEBSOCKET:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? WEBSOCKET_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,       m_strArtefactsPath);
    sSettings.Bind(WS_HOST,              [this](const std::string& v) { setWsHost(v); return true; });
    sSettings.Bind(WS_PORT,              [this](const std::string& v) { return setWsPort(v); });
    sSettings.Bind(WS_PATH,              [this](const std::string& v) { return setWsPath(v); });
    sSettings.Bind(WS_SUBPROTOCOL,       [this](const std::string& v) { setWsSubprotocol(v); return true; });
    sSettings.Bind(WS_CONNECT_TIMEOUT,   [this](const std::string& v) { return setConnectTimeout(v); });
    sSettings.Bind(WS_READ_TIMEOUT,      [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(WS_WRITE_TIMEOUT,     [this](const std::string& v) { return setWriteTimeout(v); });
    // Route through the setter so the [1-WS_MAX_BUFLENGTH] range check is
    // applied consistently regardless of whether the value came from the ini
    // file or from the CONFIG command.
    sSettings.Bind(WS_READ_BUFFER_SIZE,  [this](const std::string& v) { return setWsReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */

#endif // WEBSOCKET_SETUP_HPP
