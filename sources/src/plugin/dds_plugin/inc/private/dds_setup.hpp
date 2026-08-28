#ifndef DDS_SETUP_HPP
#define DDS_SETUP_HPP

#include "dds_plugin.hpp"
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

#define LT_HDR   "DDS_P       |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define K_ARTEFACTS         "ARTEFACTS_PATH"
#define K_DOMAIN            "DOMAIN"
#define K_PARTICIPANT_ID    "PARTICIPANT_ID"
#define K_USE_IPV6          "USE_IPV6"
#define K_IFACE             "IFACE"
#define K_MCAST_IFACE       "MCAST_IFACE"
#define K_SPDP_MCAST_GROUP  "SPDP_MULTICAST_GROUP"
#define K_NAME              "PARTICIPANT_NAME"
#define K_TTL               "TTL"
#define K_SPDP_PERIOD       "SPDP_PERIOD_MS"
#define K_LEASE             "LEASE_DURATION_SEC"
#define K_RELIABLE          "RELIABLE"
#define K_HB_PERIOD         "HEARTBEAT_PERIOD_MS"
#define K_HISTORY_DEPTH     "HISTORY_DEPTH"
#define K_FRAG_THRESHOLD    "FRAGMENT_THRESHOLD_BYTES"
#define K_READ_TIMEOUT      "READ_TIMEOUT"
#define K_READ_BUFSIZE      "READ_BUFFER_SIZE"

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
bool DdsPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? DDS_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,        m_strArtefactsPath);
    sSettings.Bind(K_DOMAIN,           [this](const std::string& v) { return setDomainId(v); });
    sSettings.Bind(K_PARTICIPANT_ID,   [this](const std::string& v) { return setParticipantId(v); });
    sSettings.Bind(K_USE_IPV6,         [this](const std::string& v) { return setUseIpv6(v); });
    sSettings.Bind(K_IFACE,            m_strIface);
    sSettings.Bind(K_MCAST_IFACE,      m_strMcastIface);
    sSettings.Bind(K_SPDP_MCAST_GROUP, m_strSpdpMcastGroup);
    sSettings.Bind(K_NAME,             m_strParticipantName);
    sSettings.Bind(K_TTL,              [this](const std::string& v) { return setTtl(v); });
    sSettings.Bind(K_SPDP_PERIOD,      [this](const std::string& v) { return setSpdpPeriodMs(v); });
    sSettings.Bind(K_LEASE,            [this](const std::string& v) { return setLeaseDurationSec(v); });
    sSettings.Bind(K_RELIABLE,         [this](const std::string& v) { return setReliable(v); });
    sSettings.Bind(K_HB_PERIOD,        [this](const std::string& v) { return setHeartbeatPeriodMs(v); });
    sSettings.Bind(K_HISTORY_DEPTH,    [this](const std::string& v) { return setHistoryDepth(v); });
    sSettings.Bind(K_FRAG_THRESHOLD,   [this](const std::string& v) { return setFragmentThresholdBytes(v); });
    sSettings.Bind(K_READ_TIMEOUT,     [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE,     [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY,    m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Domain:"); LOG_UINT32(m_u32DomainId));
    return true;

} /* m_LocalSetParams() */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of DDS parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (d=domain  pid=participant_id  v6=use_ipv6  i=iface  mi=mcast_iface
 *                     mg=spdp_mcast_group  n=participant_name  t=ttl  sp=spdp_period_ms
 *                     l=lease_duration_sec  r=reliable  hb=heartbeat_period_ms
 *                     hd=history_depth  fr=fragment_threshold_bytes  rt=read_tout  rb=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_dds_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "d",      .boolSetter = &T::setDomainId               },
        { .key = "pid",    .boolSetter = &T::setParticipantId          },
        { .key = "v6",     .boolSetter = &T::setUseIpv6                },
        { .key = "i",      .voidSetter = &T::setIface                  },
        { .key = "mi",     .voidSetter = &T::setMcastIface             },
        { .key = "mg",     .voidSetter = &T::setSpdpMcastGroup         },
        { .key = "n",      .voidSetter = &T::setParticipantName        },
        { .key = "t",      .boolSetter = &T::setTtl                    },
        { .key = "sp",     .boolSetter = &T::setSpdpPeriodMs           },
        { .key = "l",      .boolSetter = &T::setLeaseDurationSec       },
        { .key = "r",      .boolSetter = &T::setReliable               },
        { .key = "hb",     .boolSetter = &T::setHeartbeatPeriodMs      },
        { .key = "hd",     .boolSetter = &T::setHistoryDepth           },
        { .key = "fr",     .boolSetter = &T::setFragmentThresholdBytes },
        { .key = "rt",     .boolSetter = &T::setReadTimeout            },
        { .key = "rb",     .boolSetter = &T::setReadBufferSize         },
        { .key = "raw",    .boolSetter = &T::setRawResult              },
        { .key = "cached", .boolSetter = &T::setCyclicCached           },
    };

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

#endif // DDS_SETUP_HPP
