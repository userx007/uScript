#ifndef RAWETH_SETUP_HPP
#define RAWETH_SETUP_HPP

#include "PluginSetup.hpp"
#include "raweth_plugin.hpp"
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

#define LT_HDR   "RAWETH_P    |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"
#define RAWETH_IFACE                "RAWETH_IFACE"
#define RAWETH_DEST_MAC             "RAWETH_DEST_MAC"
#define RAWETH_ETHERTYPE            "RAWETH_ETHERTYPE"
#define RAWETH_PROMISCUOUS          "RAWETH_PROMISCUOUS"
#define RAWETH_READ_TIMEOUT         "RAWETH_READ_TIMEOUT"
#define RAWETH_WRITE_TIMEOUT        "RAWETH_WRITE_TIMEOUT"
#define RAWETH_READ_BUFFER_SIZE     "RAWETH_READ_BUFFER_SIZE"

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
bool RawEthPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "RAWETH:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? RAWETH_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,       m_strArtefactsPath);
    sSettings.Bind(RAWETH_IFACE,         [this](const std::string& v) { return setIface(v); });
    sSettings.Bind(RAWETH_DEST_MAC,      [this](const std::string& v) { return setDestMac(v); });
    sSettings.Bind(RAWETH_ETHERTYPE,     [this](const std::string& v) { return setEtherType(v); });
    sSettings.Bind(RAWETH_PROMISCUOUS,   [this](const std::string& v) { return setPromiscuous(v); });
    sSettings.Bind(RAWETH_READ_TIMEOUT,  [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(RAWETH_WRITE_TIMEOUT, [this](const std::string& v) { return setWriteTimeout(v); });
    // Route through the setter so the [1-RAWETH_MAX_BUFLENGTH] range check is
    // applied consistently regardless of whether the value came from the ini
    // file or from the CONFIG command.
    sSettings.Bind(RAWETH_READ_BUFFER_SIZE, [this](const std::string& v) { return setRawEthReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of RawEth parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (i=iface  d=dest_mac  t=ethertype  x=promiscuous  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_raweth_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "i",      .boolSetter = &T::setIface                 },
        { .key = "d",      .boolSetter = &T::setDestMac                },
        { .key = "t",      .boolSetter = &T::setEtherType              },
        { .key = "x",      .boolSetter = &T::setPromiscuous            },
        { .key = "r",      .boolSetter = &T::setReadTimeout            },
        { .key = "w",      .boolSetter = &T::setWriteTimeout           },
        { .key = "s",      .boolSetter = &T::setRawEthReadBufferSize   },
        { .key = "raw",    .boolSetter = &T::setRawResult              },
        { .key = "cached", .boolSetter = &T::setCyclicCached           },
    };

    return generic_setup_params(pOwner, args, table, "RAWETH SETUP |");
}

#endif // RAWETH_SETUP_HPP
