#ifndef VECTOR_ETH_SETUP_HPP
#define VECTOR_ETH_SETUP_HPP
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"
#include "vector_eth_plugin.hpp"

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

#define LT_HDR                    "VECTOR_ETH_P|"
#define LOG_HDR                   LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH            "ARTEFACTS_PATH"
#define VECTOR_ETH_APP_NAME       "VECTOR_ETH_APP_NAME"
#define VECTOR_ETH_APP_CHANNEL    "VECTOR_ETH_APP_CHANNEL"
#define VECTOR_ETH_DEVICE_HW      "VECTOR_ETH_DEVICE_HW"
#define VECTOR_ETH_DEVICE_SERIAL  "VECTOR_ETH_DEVICE_SERIAL"
#define VECTOR_ETH_DEVICE_NAME    "VECTOR_ETH_DEVICE_NAME"
#define VECTOR_ETH_DEVICE_HWINDEX "VECTOR_ETH_DEVICE_HWINDEX"
#define VECTOR_ETH_DEVICE_HWCH    "VECTOR_ETH_DEVICE_HWCHANNEL"
#define VECTOR_ETH_DEST_MAC       "VECTOR_ETH_DEST_MAC"
#define VECTOR_ETH_ETHERTYPE      "VECTOR_ETH_ETHERTYPE"
#define VECTOR_ETH_FILTER_SRC_MAC "VECTOR_ETH_FILTER_SRC_MAC"
#define VECTOR_ETH_FILTER_TYPE    "VECTOR_ETH_FILTER_TYPE"
#define VECTOR_ETH_SPEED          "VECTOR_ETH_SPEED"
#define VECTOR_ETH_DUPLEX         "VECTOR_ETH_DUPLEX"
#define VECTOR_ETH_CONNECTOR      "VECTOR_ETH_CONNECTOR"
#define VECTOR_ETH_PHY            "VECTOR_ETH_PHY"
#define READ_TIMEOUT              "READ_TIMEOUT"
#define WRITE_TIMEOUT             "WRITE_TIMEOUT"
#define READ_BUF_SIZE             "READ_BUF_SIZE"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief processing of the plugin specific settings - same role as VectorPlugin::m_LocalSetParams().
 */
/*--------------------------------------------------------------------------------------------------------*/
bool VectorEthPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? VECTOR_ETH_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing found in the ini file"));
        return true;
    }

    PluginSettingsBinder sSettings;

    // clang-format off
    sSettings.Bind(VECTOR_ETH_DEVICE_HW, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setDeviceHw(v); });

    sSettings.Bind(VECTOR_ETH_DEVICE_SERIAL, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setDeviceSerial(v); });

    sSettings.Bind(VECTOR_ETH_DEVICE_NAME, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setDeviceName(v); });

    sSettings.Bind(VECTOR_ETH_DEVICE_HWINDEX, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setDeviceHwIndex(v); });

    sSettings.Bind(VECTOR_ETH_DEVICE_HWCH, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setDeviceHwChannel(v); });

    sSettings.Bind(VECTOR_ETH_DEST_MAC, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setDestMac(v); });

    sSettings.Bind(VECTOR_ETH_ETHERTYPE, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setEtherType(v); });

    sSettings.Bind(VECTOR_ETH_SPEED, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setEthSpeed(v); });
    sSettings.Bind(VECTOR_ETH_DUPLEX, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setEthDuplex(v); });

    sSettings.Bind(VECTOR_ETH_CONNECTOR, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setEthConnector(v); });

    sSettings.Bind(VECTOR_ETH_PHY, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setEthPhy(v); });

    sSettings.Bind(VECTOR_ETH_APP_CHANNEL,          [this](const std::string &v) { return setAppChannel(v); });
    sSettings.Bind(VECTOR_ETH_FILTER_SRC_MAC,       [this](const std::string &v) { return setRxFilterSrcMac(v); });
    sSettings.Bind(VECTOR_ETH_FILTER_TYPE,          [this](const std::string &v) { return setRxFilterEtherType(v); });
    sSettings.Bind(READ_BUF_SIZE,                   [this](const std::string &v) { return setEthReadBufferSize(v); });
    sSettings.Bind(READ_TIMEOUT,                    m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,                   m_u32WriteTimeout);
    sSettings.Bind(ARTEFACTS_PATH,                  m_strArtefactsPath);
    sSettings.Bind(VECTOR_ETH_APP_NAME,             m_strAppName);
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY,    m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);
    // clang-format on

    return sSettings.Apply(psSetParams->mapSettings,
                           [](const std::string &strKey, const std::string &strRawValue) {
                               LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
                           });

} /* m_LocalSetParams() */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of VectorEth parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (a=app_name  i=app_channel_index  hw=/serial=/name=/hwidx=/hwch= direct
 *                     selection  dst=dest_mac  type=ethertype  speed=  duplex=  connector=  phy=
 *                     r=read_tout  w=write_tout  s=recv_bufsize  raw=  cached=)
 * \return true if processing succeeded, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_eth_set_params(const T *pOwner, const std::string &strArgs)
{
    // clang-format off
    static constexpr KVSetterEntry<T> table[] = {
        {.key = "a",        .voidSetter = &T::setAppName},
        {.key = "i",        .boolSetter = &T::setAppChannel},
        {.key = "hw",       .boolSetter = &T::setDeviceHw},
        {.key = "serial",   .boolSetter = &T::setDeviceSerial},
        {.key = "name",     .boolSetter = &T::setDeviceName},
        {.key = "hwidx",    .boolSetter = &T::setDeviceHwIndex},
        {.key = "hwch",     .boolSetter = &T::setDeviceHwChannel},
        {.key = "dst",      .boolSetter = &T::setDestMac},
        {.key = "type",     .boolSetter = &T::setEtherType},
        {.key = "speed",    .boolSetter = &T::setEthSpeed},
        {.key = "duplex",   .boolSetter = &T::setEthDuplex},
        {.key = "connector",.boolSetter = &T::setEthConnector},
        {.key = "phy",      .boolSetter = &T::setEthPhy},
        {.key = "r",        .boolSetter = &T::setEthReadTimeout},
        {.key = "w",        .boolSetter = &T::setEthWriteTimeout},
        {.key = "s",        .boolSetter = &T::setEthReadBufferSize},
        {.key = "raw",      .boolSetter = &T::setRawResult},
        {.key = "cached",   .boolSetter = &T::setCyclicCached},
    };
    // clang-format on

    return generic_setup_params(pOwner, strArgs, table, LT_HDR);
}

#endif // VECTOR_ETH_SETUP_HPP
