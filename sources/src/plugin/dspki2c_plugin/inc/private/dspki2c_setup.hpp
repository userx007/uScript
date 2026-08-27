#ifndef DSPKI2C_SETUP_HPP
#define DSPKI2C_SETUP_HPP

#include "PluginSetup.hpp"
#include "dspki2c_plugin.hpp"
#include "uPluginSettings.hpp"
#include "uCommandExec.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of Digispark I2C parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (v=usb_vid  p=usb_pid  a=slave_addr  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * \note Short-circuits to true (without applying anything) while the plugin isn't yet enabled -
 *       this is the argument-validation-only dry run, before any real Digispark device is
 *       expected to be attached.
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_i2c_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "v",      .boolSetter = &T::setVid             },
        { .key = "p",      .boolSetter = &T::setPid             },
        { .key = "a",      .boolSetter = &T::setSlaveAddr       },
        { .key = "r",      .boolSetter = &T::setReadTimeout     },
        { .key = "w",      .boolSetter = &T::setWriteTimeout    },
        { .key = "s",      .boolSetter = &T::setReadBufferSize  },
        { .key = "raw",    .boolSetter = &T::setRawResult       },
        { .key = "cached", .boolSetter = &T::setCyclicCached    },
    };

    if (args.empty()) {
        LOG_PRINT(LOG_INFO, LOG_STRING("DSPKI2C SETUP |"); LOG_STRING("Missing args"));
        return false;
    }

    // Short-circuit to true (without applying anything) while the plugin isn't yet enabled -
    // this is the argument-validation-only dry run, before any real Digispark device is
    // expected to be attached.
    if (false == pOwner->isEnabled()) {
        return true;
    }

    return parseAndCallSetupHandlers(pOwner, args, table, "DSPKI2C SETUP |");
}

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    I2C_VID            "I2C_VID"
#define    I2C_PID            "I2C_PID"
#define    I2C_SLAVE_ADDR     "I2C_SLAVE_ADDR"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/

bool DSPKi2cPlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "DSPKI2C:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? DSPKI2C_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(I2C_VID, [this](const std::string& v) {
        if (false == setVid(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid I2C_VID value"));
            return false;
        }
        return true;
    });
    sSettings.Bind(I2C_PID, [this](const std::string& v) {
        if (false == setPid(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid I2C_PID value"));
            return false;
        }
        return true;
    });
    sSettings.Bind(I2C_SLAVE_ADDR, [this](const std::string& v) {
        if (false == setSlaveAddr(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid I2C_SLAVE_ADDR (must be 7-bit hex, 00-7F)"));
            return false;
        }
        return true;
    });
    sSettings.Bind(READ_TIMEOUT,  m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT, m_u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE, m_u32ReadBufferSize);
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */

#endif // DSPKI2C_SETUP_HPP
