#ifndef HYDRABUS_SETUP_HPP
#define HYDRABUS_SETUP_HPP

#include "PluginSetup.hpp"
#include "hydrabus_plugin.hpp"
#include "uPluginSettings.hpp"

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

#define LT_HDR   "HYDRABUS_P  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define UART_PORT        "UART_PORT"
#define BAUDRATE         "BAUDRATE"
#define READ_TIMEOUT     "READ_TIMEOUT"
#define WRITE_TIMEOUT    "WRITE_TIMEOUT"
#define READ_BUF_SIZE    "READ_BUF_SIZE"
#define SCRIPT_DELAY     "SCRIPT_DELAY"

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
bool HydrabusPlugin::m_LocalSetParams(const PluginDataSet* ps)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "HYDRABUS:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one.
    m_strInstanceName = ps->strInstanceName.empty() ? HYDRABUS_PLUGIN_NAME : ps->strInstanceName;

    if (!ps || ps->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No settings in config"));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_sIniValues.strArtefactsPath);
    sSettings.Bind(UART_PORT,      m_sIniValues.strUartPort);
    sSettings.Bind(BAUDRATE,       m_sIniValues.u32UartBaudrate);
    sSettings.Bind(READ_TIMEOUT,   m_sIniValues.u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,  m_sIniValues.u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE,  m_sIniValues.u32ReadBufferSize);
    sSettings.Bind(SCRIPT_DELAY,   m_sIniValues.u32ScriptDelay);

    // accumulate mode: matches the original getX() lambdas ("ok &= ...")
    const bool bOk = sSettings.Apply(ps->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    if (!bOk)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return bOk;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of HYDRABUS parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (p=port  b=baudrate  r=read_tout  w=write_tout  s=recv_bufsize  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_hydrabus_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "p",  .voidSetter = &T::setUartPort        },
        { .key = "b",  .boolSetter = &T::setUartBaudrate    },
        { .key = "r",  .boolSetter = &T::setReadTimeout     },
        { .key = "w",  .boolSetter = &T::setWriteTimeout    },
        { .key = "s",  .boolSetter = &T::setReadBufferSize  },
        { .key = "sd", .boolSetter = &T::setScriptDelay     },
    };

    return generic_setup_params(pOwner, args, table, "HYDRABUS SETUP |");
}

#endif // HYDRABUS_SETUP_HPP
