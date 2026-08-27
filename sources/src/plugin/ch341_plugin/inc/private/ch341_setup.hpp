#ifndef CH341_SETUP_HPP
#define CH341_SETUP_HPP

#include "PluginSetup.hpp"
#include "ch341_plugin.hpp"
#include "uPluginSettings.hpp"
#include "uCommandExec.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CH341 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (p=port  b=baudrate  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ch341_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "p",      .boolSetter = &T::setCh341Port           },
        { .key = "b",      .boolSetter = &T::setCh341Baudrate       },
        { .key = "r",      .boolSetter = &T::setCh341ReadTimeout    },
        { .key = "w",      .boolSetter = &T::setCh341WriteTimeout   },
        { .key = "s",      .boolSetter = &T::setCh341ReadBufferSize },
        { .key = "raw",    .boolSetter = &T::setRawResult           },
        { .key = "cached", .boolSetter = &T::setCyclicCached        },
    };

    return generic_setup_params(pOwner, args, table, "CH341 SETUP |");
}

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    CH341_PORT         "CH341_PORT"
#define    BAUDRATE           "BAUDRATE"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"
#define    READ_BUF_TIMEOUT   "READ_BUF_TIMEOUT"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/

bool CH341Plugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "CH341:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? CH341_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(CH341_PORT,     m_strCh341Port);
    sSettings.Bind(BAUDRATE,       m_u32Ch341Baudrate);
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

#endif // CH341_SETUP_HPP
