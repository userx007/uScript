#ifndef GRPC_SETUP_HPP
#define GRPC_SETUP_HPP
#include "PluginSetup.hpp"
#include "grpc_plugin.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR            "GRPC_P      |"
#define LOG_HDR           LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

// INI Keys
#define K_HOST            "HOST"
#define K_PORT            "PORT"
#define K_TLS_ENABLED     "TLS_ENABLED"
#define K_TLS_CA          "TLS_CA_CERT"
#define K_TLS_CLIENT_CERT "TLS_CLIENT_CERT"
#define K_TLS_CLIENT_KEY  "TLS_CLIENT_KEY"
#define K_ARTEFACTS       "ARTEFACTS_PATH"
#define K_DESCRIPTOR_SET  "DESCRIPTOR_SET"
#define K_AUTH_TOKEN      "AUTH_TOKEN"
#define K_CALL_TIMEOUT    "CALL_TIMEOUT"
#define K_CONNECT_TIMEOUT "CONNECT_TIMEOUT"
#define K_READ_TIMEOUT    "READ_TIMEOUT"
#define K_READ_BUFSIZE    "READ_BUFFER_SIZE"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

bool GrpcPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    if (psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing found in the ini file"));
        return true;
    }

    PluginSettingsBinder sSettings;

    // clang-format off
    sSettings.Bind(K_PORT,                          [this](const std::string &v) { return setPort(v); });
    sSettings.Bind(K_TLS_ENABLED,                   [this](const std::string &v) { return setTlsEnabled(v); });
    sSettings.Bind(K_CALL_TIMEOUT,                  [this](const std::string &v) { return setCallTimeout(v); });
    sSettings.Bind(K_CONNECT_TIMEOUT,               [this](const std::string &v) { return setConnectTimeout(v); });
    sSettings.Bind(K_READ_TIMEOUT,                  [this](const std::string &v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE,                  [this](const std::string &v) { return setReadBufferSize(v); });
    sSettings.Bind(K_ARTEFACTS,                     m_strArtefactsPath);
    sSettings.Bind(K_HOST,                          m_strHost);
    sSettings.Bind(K_TLS_CA,                        m_strTlsCaPath);
    sSettings.Bind(K_TLS_CLIENT_CERT,               m_strTlsCertPath);
    sSettings.Bind(K_TLS_CLIENT_KEY,                m_strTlsKeyPath);
    sSettings.Bind(K_DESCRIPTOR_SET,                m_strDescriptorSetPath);
    sSettings.Bind(K_AUTH_TOKEN,                    m_strAuthToken);
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY,    m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);
    // clang-format on

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of GRPC parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 * \return true if processing succeeded, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_grpc_set_params(const T *pOwner, const std::string &strArgs)
{
    // clang-format off
    static constexpr KVSetterEntry<T> table[] = {
        {.key = "h",        .voidSetter = &T::setHost},
        {.key = "p",        .boolSetter = &T::setPort},
        {.key = "t",        .boolSetter = &T::setTlsEnabled},
        {.key = "ca",       .voidSetter = &T::setTlsCaPath},
        {.key = "crt",      .voidSetter = &T::setTlsCertPath},
        {.key = "key",      .voidSetter = &T::setTlsKeyPath},
        {.key = "d",        .voidSetter = &T::setDescriptorSetPath},
        {.key = "auth",     .voidSetter = &T::setAuthToken},
        {.key = "ctout",    .boolSetter = &T::setCallTimeout},
        {.key = "xtout",    .boolSetter = &T::setConnectTimeout},
        {.key = "rtout",    .boolSetter = &T::setReadTimeout},
        {.key = "rbuf",     .boolSetter = &T::setReadBufferSize},
        {.key = "raw",      .boolSetter = &T::setRawResult},
        {.key = "cached",   .boolSetter = &T::setCyclicCached},
    };
    // clang-format on

    return generic_setup_params(pOwner, strArgs, table, LT_HDR);
}

#endif // GRPC_SETUP_HPP
