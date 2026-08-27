#ifndef KI2C_SETUP_HPP
#define KI2C_SETUP_HPP

#include "PluginSetup.hpp"
#include "ki2c_plugin.hpp"
#include "uPluginSettings.hpp"
#include "uCommandExec.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of I2C parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (d=device  a=address  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_i2c_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "d",      .voidSetter = &T::setI2CDevice          },
        { .key = "a",      .boolSetter = &T::setI2CAddress         },
        { .key = "r",      .boolSetter = &T::setI2CReadTimeout     },
        { .key = "w",      .boolSetter = &T::setI2CWriteTimeout    },
        { .key = "s",      .boolSetter = &T::setI2CReadBufferSize  },
        { .key = "raw",    .boolSetter = &T::setRawResult          },
        { .key = "cached", .boolSetter = &T::setCyclicCached       },
    };

    return generic_setup_params(pOwner, args, table, "KI2C SETUP |");
}

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    KI2C_DEVICE        "I2C_DEVICE"
#define    KI2C_ADDRESS       "I2C_ADDRESS"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "KI2C:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? KI2C_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(KI2C_DEVICE,    m_strKI2CDevice);
    sSettings.Bind(KI2C_ADDRESS,   m_u8KI2CAddress);
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

#endif // KI2C_SETUP_HPP
