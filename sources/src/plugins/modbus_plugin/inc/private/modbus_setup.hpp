#ifndef MODBUS_SETUP_HPP
#define MODBUS_SETUP_HPP

#include "PluginSetup.hpp"
#include "modbus_plugin.hpp"
#include "uCommandExec.hpp"
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

#define LT_HDR         "MODBUS_P    |"
#define LOG_HDR        LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define K_HOST         "HOST"
#define K_PORT         "PORT"
#define K_ARTEFACTS    "ARTEFACTS_PATH"
#define K_READ_TIMEOUT "READ_TIMEOUT"
#define K_READ_BUFSIZE "READ_BUFFER_SIZE"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIG COMMAND SHORT KEYS                                  //
/////////////////////////////////////////////////////////////////////////////////
// See the usage string in m_MODBUS_INFO() (modbus_plugin.cpp): "[h=host]
// [p=port] [rt=read_tout] [rb=read_bufsize]"

#define SK_HOST        "h"
#define SK_PORT        "p"
#define SK_RTOUT       "rt"
#define SK_RBUF        "rb"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

bool ModbusPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "MODBUS:1"); falls back
    // to the fixed plugin name if the interpreter didn't supply one.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? MODBUS_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) {
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS, m_strArtefactsPath);
    sSettings.Bind(K_HOST, m_strHost);
    sSettings.Bind(K_PORT, [this](const std::string &v) { return setPort(v); });
    sSettings.Bind(K_READ_TIMEOUT, [this](const std::string &v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE, [this](const std::string &v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost));
    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of MODBUS parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  rt=read_tout  rb=read_bufsize)
 * \return true if processing succeeded, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_modbus_set_params(const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        {.key = SK_HOST, .boolSetter = &T::setHost},
        {.key = SK_PORT, .boolSetter = &T::setPort},
        {.key = SK_RTOUT, .boolSetter = &T::setReadTimeout},
        {.key = SK_RBUF, .boolSetter = &T::setReadBufferSize},
        {.key = "raw", .boolSetter = &T::setRawResult},
        {.key = "cached", .boolSetter = &T::setCyclicCached},
    };

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

#endif // MODBUS_SETUP_HPP
