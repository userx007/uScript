#ifndef PROFIBUS_SETUP_HPP
#define PROFIBUS_SETUP_HPP

#include "profibus_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR  "PROFIBUS PLUGIN |"
#define LOG_HDR LOG_STRING(LT_HDR)

// INI Keys
#define K_DEVICE          "DEVICE"
#define K_BAUD            "BAUD"
#define K_OWN_ADDRESS     "OWN_ADDRESS"
#define K_RESPONSE_TOUT   "RESPONSE_TIMEOUT"
#define K_HIGH_PRIORITY   "HIGH_PRIORITY"
#define K_READ_BUFSIZE    "READ_BUFFER_SIZE"
#define K_ARTEFACTS       "ARTEFACTS_PATH"

// --- Setters requiring validation ---

bool ProfibusPlugin::setBaud(const std::string& baudStr) const
{
    uint32_t baud = 0;
    if (!numeric::str2uint32(baudStr, baud)) return false;

    // Only rates ProfibusDriver can actually reach through UART::open() —
    // see profibus_driver.hpp's "Known hardware/timing limitations".
    // 1500000/3000000 are platform-dependent there (present only when the
    // build's <termios.h> defines B1500000/B3000000); accepted here too
    // since rejecting them outright would be wrong on the platforms where
    // they do work, and UART::open() itself will fall back to 9600 with a
    // clear log warning on the platforms where they don't.
    switch (baud) {
        case 9600: case 19200: case 500000: case 1500000: case 3000000:
            m_u32Baud = baud;
            return true;
        default:
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Unreachable baud rate (see profibus_driver.hpp for why):"); LOG_UINT32(baud));
            return false;
    }
}

bool ProfibusPlugin::setOwnAddress(const std::string& addrStr) const
{
    uint32_t addr = 0;
    if (!numeric::str2uint32(addrStr, addr)) return false;
    if (addr > 125) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Own address must be 0-125 (126=commissioning, 127=broadcast):"); LOG_UINT32(addr));
        return false;
    }
    m_u8OwnAddress = static_cast<uint8_t>(addr);
    return true;
}

bool ProfibusPlugin::setReadBufferSize(const std::string& bufSizeStr) const
{
    uint32_t sz = 0;
    if (!numeric::str2uint32(bufSizeStr, sz)) return false;
    if (sz == 0) return false;
    m_u32ReadBufferSize = sz;
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

// --- Local Params ---

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


bool ProfibusPlugin::m_PROFIBUS_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return generic_profibus_set_params(this, args);
}


#endif // PROFIBUS_SETUP_HPP
