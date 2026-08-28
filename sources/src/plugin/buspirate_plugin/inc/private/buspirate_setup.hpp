#ifndef BUSPIRATE_INI_SETUP_HPP
#define BUSPIRATE_INI_SETUP_HPP

#include "PluginSetup.hpp"
#include "buspirate_plugin.hpp"
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
#define LT_HDR     "BUSPIRATE_P |"
#define LOG_HDR    LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    UART_PORT          "UART_PORT"
#define    BAUDRATE           "BAUDRATE"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"
#define    READ_BUF_TIMEOUT   "READ_BUF_TIMEOUT"
#define    SCRIPT_DELAY       "SCRIPT_DELAY"


/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

bool BuspiratePlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "BUSPIRATE:1"); falls back
    // to the fixed plugin name if the interpreter didn't supply one. Done before the "nothing
    // loaded from ini" early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? BUSPIRATE_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
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

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of BUSPIRATE parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (p=port  b=baudrate  r=read_tout  w=write_tout  s=recv_bufsize  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_buspirate_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "p",  .voidSetter = &T::setUartPort       },
        { .key = "b",  .boolSetter = &T::setUartBaudrate    },
        { .key = "r",  .boolSetter = &T::setReadTimeout     },
        { .key = "w",  .boolSetter = &T::setWriteTimeout    },
        { .key = "s",  .boolSetter = &T::setReadBufferSize  },
        { .key = "sd", .boolSetter = &T::setScriptDelay     },
    };

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

#endif // BUSPIRATE_INI_SETUP_HPP
