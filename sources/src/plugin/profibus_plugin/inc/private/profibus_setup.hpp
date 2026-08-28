#ifndef PROFIBUS_SETUP_HPP
#define PROFIBUS_SETUP_HPP

#include "profibus_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR  "PROFIBUS_P  |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define K_DEVICE          "DEVICE"
#define K_BAUD            "BAUD"
#define K_OWN_ADDRESS     "OWN_ADDRESS"
#define K_RESPONSE_TOUT   "RESPONSE_TIMEOUT"
#define K_HIGH_PRIORITY   "HIGH_PRIORITY"
#define K_READ_BUFSIZE    "READ_BUFFER_SIZE"
#define K_ARTEFACTS       "ARTEFACTS_PATH"

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
bool ProfibusPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "PROFIBUS:1"); falls back
    // to the fixed plugin name if the interpreter didn't supply one.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? PROFIBUS_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,     m_strArtefactsPath);
    sSettings.Bind(K_DEVICE,        m_strDevice);
    sSettings.Bind(K_BAUD,          [this](const std::string& v) { return setBaud(v); });
    sSettings.Bind(K_OWN_ADDRESS,   [this](const std::string& v) { return setOwnAddress(v); });
    sSettings.Bind(K_RESPONSE_TOUT, [this](const std::string& v) { return setResponseTimeout(v); });
    sSettings.Bind(K_HIGH_PRIORITY, [this](const std::string& v) { return setDefaultHighPriority(v); });
    sSettings.Bind(K_READ_BUFSIZE,  [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Device:"); LOG_STRING(m_strDevice)
              LOG_STRING("Baud:"); LOG_UINT32(m_u32Baud));
    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of Profibus parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (d=device  b=baud  a=own_address  rt=response_tout  hp=default_high_priority  rb=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_profibus_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "d",      .voidSetter = &T::setDevice                },
        { .key = "b",      .boolSetter = &T::setBaud                  },
        { .key = "a",      .boolSetter = &T::setOwnAddress            },
        { .key = "rt",     .boolSetter = &T::setResponseTimeout       },
        { .key = "hp",     .boolSetter = &T::setDefaultHighPriority   },
        { .key = "rb",     .boolSetter = &T::setReadBufferSize        },
        { .key = "raw",    .boolSetter = &T::setRawResult             },
        { .key = "cached", .boolSetter = &T::setCyclicCached          },
    };

    return generic_setup_params(pOwner, args, table, "PROFIBUS SETUP |");
}

#endif // PROFIBUS_SETUP_HPP
