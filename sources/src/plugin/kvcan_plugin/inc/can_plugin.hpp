#ifndef CAN_PLUGIN_HPP
#define CAN_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uCan.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define CAN_PLUGIN_VERSION    "1.0.0.0"
#define CAN_PLUGIN_NAME       "CAN"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// CAN_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef CAN_GET_BLOCKING
#define CAN_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define CAN_PLUGIN_COMMANDS_CONFIG_TABLE    \
CAN_PLUGIN_CMD_RECORD( INFO               ) \
CAN_PLUGIN_CMD_RECORD( CONFIG             ) \
CAN_PLUGIN_CMD_RECORD( FILTER             ) \
CAN_PLUGIN_CMD_RECORD( CMD                ) \
CAN_PLUGIN_CMD_RECORD( SCRIPT             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief CAN plugin class definition.
  *
  * Wraps the CAN SocketCAN driver and exposes it through the standard
  * PluginInterface dispatch model.  Works with any SocketCAN interface
  * (physical canN or virtual vcanN).
  *
  * Extra command vs UART/I2C/SPI plugins:
  *   FILTER — installs CAN hardware acceptance filters at runtime
  *             without reopening the socket.
*/
class CANPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        CANPlugin() : m_strVersion(CAN_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData("")
        {
            #define CAN_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<CANPlugin>{&CANPlugin::m_CAN_##a, CAN_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            CAN_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  CAN_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~CANPlugin()
        {

        }

        /**
          * \brief get the plugin initialization status
        */
        bool isInitialized( void ) const
        {
            return m_bIsInitialized;
        }

        /**
          * \brief get enabling status
        */
        bool isEnabled (void) const
        {
            return m_bIsEnabled;
        }

        /**
          * \brief Import external settings into the plugin
        */
        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;

            if (true == generic_setparams<CANPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        /**
          * \brief function to retrieve information from plugin
        */
        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<CANPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<CANPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<CANPlugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /**
          * \brief get the plugin version
        */
        const std::string& getVersion(void) const
        {
            return m_strVersion;
        }

        /**
          * \brief get the result data
        */
        const std::string& getData(void) const
        {
            return m_strResultData;
        }

        /**
          * \brief clear the result data (avoid that some data to be returned by other command)
        */
        void resetData(void) const
        {
            m_strResultData.clear();
        }

        /**
          * \brief perform the initialization of modules used by the plugin
          * \note public because it needs to be called explicitly after loading the plugin
        */
        bool doInit(void *pvUserData);

        /**
          * \brief perform the enabling of the plugin
          * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
          *       This mode is used for the command validation
        */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitly before closing/freeing the shared library
        */
        void doCleanup(void);

        /**
          * \brief get fault tolerant flag status
        */
        bool isFaultTolerant (void) const
        {
            return m_bIsFaultTolerant;
        }

        /**
          * \brief get the privileged status
        */
        bool isPrivileged (void) const
        {
            return m_bIsPrivileged;
        }

        /**
          * \brief get SocketCAN interface name
        */
        const char *getCanIface (void) const
        {
            return m_strCanIface.c_str();
        }

        /**
          * \brief set SocketCAN interface name (e.g. "vcan0", "can1")
        */
        void setCanIface (const std::string& strCanIface) const
        {
            m_strCanIface.assign(strCanIface);
        }

        /**
          * \brief set the CAN ID stamped on outgoing frames
          *        Accepts decimal or 0x-prefixed hex strings.
          *        Set CAN_EFF_FLAG (0x80000000) in the value for 29-bit extended IDs.
        */
        bool setCanTxId (const std::string& strTxId) const
        {
            return numeric::str2uint32(strTxId, m_u32CanTxId);
        }

        /**
          * \brief set CAN read timeout
        */
        bool setCanReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set CAN write timeout
        */
        bool setCanWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set CAN read buffer size
        */
        bool setCanReadBufferSize (const std::string& strReadBufferSize) const
        {
            return numeric::str2uint32(strReadBufferSize, m_u32CanReadBufferSize);
        }

    private:

        /**
          * \brief message sender
        */
        bool m_Send (std::span<const uint8_t> data, std::shared_ptr<const ICommDriver> shpDriver) const;

        /**
          * \brief message receiver
        */
        bool m_Receive (std::span<uint8_t> data, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const;

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /**
          * \brief helper: parse a comma-separated filter list string into CAN::CanFilter entries.
          *        Each entry has the form "<id>:<mask>" (hex or decimal).
          *        Example: "0x100:0x7FF,0x200:0x7FF"
        */
        bool m_ParseFilters (const std::string& strFilters, std::vector<CAN::CanFilter>& vFilters) const;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<CANPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;

        /**
          * \brief data returned by plugin
        */
        mutable std::string m_strResultData;

        /**
          * \brief plugin initialization status
        */
        bool m_bIsInitialized;

        /**
          * \brief plugin enabling status
        */
        bool m_bIsEnabled;

        /**
          * \brief plugin fault tolerant mode
        */
        bool m_bIsFaultTolerant;

        /**
          * \brief plugin is privileged
        */
        bool m_bIsPrivileged;

        /**
          * \brief the artefacts path got from configuration
        */
        std::string m_strArtefactsPath;

        /**
          * \brief SocketCAN interface name (e.g. "vcan0", "can1")
        */
        mutable std::string m_strCanIface;

        /**
          * \brief CAN ID stamped on every outgoing frame
        */
        mutable uint32_t m_u32CanTxId;

        /**
          * \brief CAN read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief CAN write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for CAN read operations (max 64 bytes for CAN FD)
        */
        mutable uint32_t m_u32CanReadBufferSize;

        /**
          * \brief acceptance filters applied to the open socket (empty = accept all)
        */
        mutable std::vector<CAN::CanFilter> m_vFilters;

        /**
          * \brief functions associated to the plugin commands
        */
        #define CAN_PLUGIN_CMD_RECORD(a, ...)  bool m_CAN_##a ( const std::string& args, std::stop_token st ) const;
        CAN_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  CAN_PLUGIN_CMD_RECORD
};

#endif /* CAN_PLUGIN_HPP */
