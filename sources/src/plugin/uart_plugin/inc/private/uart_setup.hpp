#ifndef UART_SETUP_HPP
#define UART_SETUP_HPP

#include "PluginSetup.hpp"
#include "uart_plugin.hpp"
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

#define LT_HDR   "UART_P      |"
#define LOG_HDR  LOG_STRING(LT_HDR)

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

bool UARTPlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "UART:1");
    // falls back to the fixed plugin name if the interpreter didn't supply
    // one (e.g. standalone construction outside the script interpreter).
    // Done before the "nothing loaded from ini" early-return below so it's
    // always captured regardless of whether an [UART]/[UART:N] section exists.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? UART_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(UART_PORT,      m_strUartPort);
    sSettings.Bind(BAUDRATE,       m_u32UartBaudrate);
    sSettings.Bind(READ_TIMEOUT,   m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,  m_u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE,  m_u32ReadBufferSize);
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of UART parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (p=port  b=baudrate  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_uart_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "p",      .boolSetter = &T::setUartPort           },
        { .key = "b",      .boolSetter = &T::setUartBaudrate       },
        { .key = "r",      .boolSetter = &T::setUartReadTimeout    },
        { .key = "w",      .boolSetter = &T::setUartWriteTimeout   },
        { .key = "s",      .boolSetter = &T::setUartReadBufferSize },
        { .key = "raw",    .boolSetter = &T::setRawResult          },
        { .key = "cached", .boolSetter = &T::setCyclicCached       },
    };

    return generic_setup_params(pOwner, args, table, "UART SETUP |");
}

#endif // UART_SETUP_HPP
