#ifndef GRPC_SETUP_HPP
#define GRPC_SETUP_HPP

#include "grpc_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "GRPC PLUGIN |"

// INI Keys
#define K_HOST              "HOST"
#define K_PORT              "PORT"
#define K_TLS_ENABLED       "TLS_ENABLED"
#define K_TLS_CA            "TLS_CA_CERT"
#define K_TLS_CLIENT_CERT   "TLS_CLIENT_CERT"
#define K_TLS_CLIENT_KEY    "TLS_CLIENT_KEY"
#define K_ARTEFACTS         "ARTEFACTS_PATH"
#define K_DESCRIPTOR_SET    "DESCRIPTOR_SET"
#define K_AUTH_TOKEN        "AUTH_TOKEN"
#define K_CALL_TIMEOUT      "CALL_TIMEOUT"
#define K_CONNECT_TIMEOUT   "CONNECT_TIMEOUT"
#define K_READ_TIMEOUT      "READ_TIMEOUT"
#define K_READ_BUFSIZE      "READ_BUFFER_SIZE"

// --- Setters requiring validation ---

bool GrpcPlugin::setPort(const std::string& portStr) const
{
    uint32_t port = 0;
    if (!numeric::str2uint32(portStr, port)) return false;
    if (port > 65535) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid port:"); LOG_UINT32(port));
        return false;
    }
    m_u16Port = static_cast<uint16_t>(port);
    return true;
}

bool GrpcPlugin::setCallTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32CallTimeout);
}

bool GrpcPlugin::setConnectTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32ConnectTimeout);
}

bool GrpcPlugin::setReadTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
}

bool GrpcPlugin::setReadBufferSize(const std::string& bufSizeStr) const
{
    uint32_t sz = 0;
    if (!numeric::str2uint32(bufSizeStr, sz)) return false;
    if (sz == 0) return false;
    m_u32ReadBufferSize = sz;
    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of gRPC parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  t=tls_enabled  ca=tls_ca_cert  crt=tls_client_cert
 *                     key=tls_client_key  d=descriptor_set  auth=auth_token  ct=call_tout
 *                     xt=connect_tout  rt=read_tout  rb=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_grpc_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h",      .voidSetter = &T::setHost              },
        { .key = "p",      .boolSetter = &T::setPort              },
        { .key = "t",      .boolSetter = &T::setTlsEnabled        },
        { .key = "ca",     .voidSetter = &T::setTlsCaPath         },
        { .key = "crt",    .voidSetter = &T::setTlsCertPath       },
        { .key = "key",    .voidSetter = &T::setTlsKeyPath        },
        { .key = "d",      .voidSetter = &T::setDescriptorSetPath },
        { .key = "auth",   .voidSetter = &T::setAuthToken         },
        { .key = "ct",     .boolSetter = &T::setCallTimeout       },
        { .key = "xt",     .boolSetter = &T::setConnectTimeout    },
        { .key = "rt",     .boolSetter = &T::setReadTimeout       },
        { .key = "rb",     .boolSetter = &T::setReadBufferSize    },
        { .key = "raw",    .boolSetter = &T::setRawResult         },
        { .key = "cached", .boolSetter = &T::setCyclicCached      },
    };

    return generic_setup_params(pOwner, args, table, "GRPC SETUP |");
}

// --- Local Params ---

bool GrpcPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,      m_strArtefactsPath);
    sSettings.Bind(K_HOST,           m_strHost);
    sSettings.Bind(K_PORT,           [this](const std::string& v) { return setPort(v); });
    sSettings.Bind(K_TLS_ENABLED,    [this](const std::string& v) { return setTlsEnabled(v); });
    sSettings.Bind(K_TLS_CA,          m_strTlsCaPath);
    sSettings.Bind(K_TLS_CLIENT_CERT, m_strTlsCertPath);
    sSettings.Bind(K_TLS_CLIENT_KEY,  m_strTlsKeyPath);
    sSettings.Bind(K_DESCRIPTOR_SET,  m_strDescriptorSetPath);
    sSettings.Bind(K_AUTH_TOKEN,      m_strAuthToken);
    sSettings.Bind(K_CALL_TIMEOUT,    [this](const std::string& v) { return setCallTimeout(v); });
    sSettings.Bind(K_CONNECT_TIMEOUT, [this](const std::string& v) { return setConnectTimeout(v); });
    sSettings.Bind(K_READ_TIMEOUT,    [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE,    [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost));

    return true;
}


bool GrpcPlugin::m_GRPC_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return generic_grpc_set_params(this, args);
}

#endif // GRPC_SETUP_HPP
