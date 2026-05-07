#ifndef DSPKI2C_PLUGIN_HPP
#define DSPKI2C_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uDigisparkI2C.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>
#include <cstdint>

///////////////////////////////////////////////////////////////////
//                        PLUGIN VERSION                         //
///////////////////////////////////////////////////////////////////

#define DSPKI2C_PLUGIN_VERSION    "1.0.0.0"
#define DSPKI2C_PLUGIN_NAME       "DSPKI2C"

///////////////////////////////////////////////////////////////////
//                        PLUGIN COMMANDS                        //
///////////////////////////////////////////////////////////////////

/**
 * X-macro table of all commands exposed by this plugin.
 *
 * Each entry expands to:
 *   - a map insertion in the constructor
 *   - a private handler declaration  bool m_DSPKI2C_<CMD>(const std::string&) const
 *
 * To add a command: insert a new DSPKI2C_PLUGIN_CMD_RECORD line here
 * and provide the matching implementation in dspki2c_plugin.cpp.
 */
#define DSPKI2C_PLUGIN_COMMANDS_CONFIG_TABLE    \
DSPKI2C_PLUGIN_CMD_RECORD( INFO      )          \
DSPKI2C_PLUGIN_CMD_RECORD( CONFIG    )          \
DSPKI2C_PLUGIN_CMD_RECORD( I2C_SCAN  )          \
DSPKI2C_PLUGIN_CMD_RECORD( I2C_WRITE )          \
DSPKI2C_PLUGIN_CMD_RECORD( I2C_READ  )          \
DSPKI2C_PLUGIN_CMD_RECORD( I2C_WRRD  )          \
DSPKI2C_PLUGIN_CMD_RECORD( SCRIPT    )          \

///////////////////////////////////////////////////////////////////
//                        PLUGIN INTERFACE                       //
///////////////////////////////////////////////////////////////////

/**
 * @brief Digispark ATtiny85 USB→I2C bridge plugin.
 *
 * Exposes I2C operations as dispatched string commands.
 * The underlying hardware driver (I2CBridge) is opened on demand
 * inside each command handler (RAII) so the HID device is held only
 * for the duration of a single call — or across all lines of a SCRIPT file.
 *
 * Command syntax overview:
 *   DSPKI2C.INFO
 *   DSPKI2C.CONFIG    [v:<vid>] [p:<pid>] [r:<read_ms>] [w:<write_ms>]
 *   DSPKI2C.I2C_SCAN
 *   DSPKI2C.I2C_WRITE  <addr_hex>  <byte0_hex> [<byte1_hex> ...]
 *   DSPKI2C.I2C_READ   <addr_hex>  <n_bytes>
 *   DSPKI2C.I2C_WRRD   <addr_hex>  <reg_hex>   <n_bytes>
 *   DSPKI2C.SCRIPT     <filename>  [<delay_ms>]
 */
class DspkI2CPlugin : public PluginInterface
{

public:

    // ── Constructor: populate command dispatch map via X-macro ────────────────
    DspkI2CPlugin()
        : m_strVersion(DSPKI2C_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData("")
        , m_u16Vid(I2CBridge::I2C_DIGISPARK_VID)
        , m_u16Pid(I2CBridge::I2C_DIGISPARK_PID)
        , m_u32ReadTimeout(2000)
        , m_u32WriteTimeout(2000)
    {
        #define DSPKI2C_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert(std::make_pair(#a, &DspkI2CPlugin::m_DSPKI2C_##a));
        DSPKI2C_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  DSPKI2C_PLUGIN_CMD_RECORD
    }

    ~DspkI2CPlugin() = default;

    // ── PluginInterface implementation ────────────────────────────────────────

    bool isInitialized(void) const { return m_bIsInitialized; }
    bool isEnabled    (void) const { return m_bIsEnabled;     }
    bool isFaultTolerant(void) const { return m_bIsFaultTolerant; }
    bool isPrivileged (void) const { return m_bIsPrivileged;  }

    bool setParams(const PluginDataSet *psSetParams)
    {
        bool bRetVal = false;
        if (generic_setparams<DspkI2CPlugin>(this, psSetParams,
                                              &m_bIsFaultTolerant,
                                              &m_bIsPrivileged))
        {
            if (m_LocalSetParams(psSetParams))
                bRetVal = true;
        }
        return bRetVal;
    }

    void getParams(PluginDataGet *psGetParams) const
    {
        generic_getparams<DspkI2CPlugin>(this, psGetParams);
    }

    bool doDispatch(const std::string& strCmd, const std::string& strParams) const
    {
        return generic_dispatch<DspkI2CPlugin>(this, strCmd, strParams);
    }

    const PluginCommandsMap<DspkI2CPlugin> *getMap(void) const { return &m_mapCmds; }
    const std::string& getVersion(void) const { return m_strVersion; }
    const std::string& getData   (void) const { return m_strResultData; }

    void resetData(void) const { m_strResultData.clear(); }

    bool doInit(void *pvUserData);

    bool doEnable(void)
    {
        m_bIsEnabled = true;
        return true;
    }

    void doCleanup(void);

    // ── Runtime parameter accessors (used by CONFIG command) ──────────────────

    void setVid(uint16_t u16Vid) const { m_u16Vid = u16Vid; }
    void setPid(uint16_t u16Pid) const { m_u16Pid = u16Pid; }

    bool setReadTimeout(const std::string& str) const
    {
        uint32_t v;
        if (!numeric::str2uint32(str, v)) return false;
        m_u32ReadTimeout = v;
        return true;
    }

    bool setWriteTimeout(const std::string& str) const
    {
        uint32_t v;
        if (!numeric::str2uint32(str, v)) return false;
        m_u32WriteTimeout = v;
        return true;
    }

private:

    // ── I2C driver helpers ────────────────────────────────────────────────────

    /** Write bytes to an I2C slave. */
    bool m_I2CWrite(uint8_t                   u8Addr,
                    std::span<const uint8_t>   data,
                    std::shared_ptr<I2CBridge>  shpDrv) const;

    /** Read N bytes from an I2C slave into buffer. */
    bool m_I2CRead (uint8_t                   u8Addr,
                    size_t                    szLen,
                    std::span<uint8_t>         buffer,
                    std::shared_ptr<I2CBridge>  shpDrv) const;

    /** Write a register address then repeated-START read. */
    bool m_I2CWriteRead(uint8_t                   u8Addr,
                        std::span<const uint8_t>   writeData,
                        size_t                    szReadLen,
                        std::span<uint8_t>         buffer,
                        std::shared_ptr<I2CBridge>  shpDrv) const;

    // ── Argument parsing helpers ──────────────────────────────────────────────

    /** Parse a hex byte string like "0x3C" or "3C" into a uint8_t. */
    static bool m_ParseHexByte(const std::string& str, uint8_t& out);

    /** Parse space-separated hex bytes like "AA BB 0xCC" into a vector. */
    static bool m_ParseHexBytes(const std::vector<std::string>& tokens,
                                size_t                          szStart,
                                std::vector<uint8_t>&           out);

    /** Parse a uint16_t from a decimal or 0x-prefixed hex string. */
    static bool m_ParseUint16(const std::string& str, uint16_t& out);

    // ── INI / setParams helper ────────────────────────────────────────────────

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    // ── Command dispatch map ──────────────────────────────────────────────────

    PluginCommandsMap<DspkI2CPlugin> m_mapCmds;

    // ── Plugin metadata ───────────────────────────────────────────────────────

    std::string         m_strVersion;
    mutable std::string m_strResultData;
    bool                m_bIsInitialized;
    bool                m_bIsEnabled;
    bool                m_bIsFaultTolerant;
    bool                m_bIsPrivileged;
    std::string         m_strArtefactsPath;

    // ── Runtime-configurable parameters (mutable: CONFIG can change them) ─────

    mutable uint16_t  m_u16Vid;           ///< HID VID (default 0x16C0)
    mutable uint16_t  m_u16Pid;           ///< HID PID (default 0x05DF)
    mutable uint32_t  m_u32ReadTimeout;   ///< Read timeout  [ms]
    mutable uint32_t  m_u32WriteTimeout;  ///< Write timeout [ms]

    // ── Command handler declarations (expanded by X-macro) ────────────────────

    #define DSPKI2C_PLUGIN_CMD_RECORD(a) \
        bool m_DSPKI2C_##a(const std::string& args) const;
    DSPKI2C_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  DSPKI2C_PLUGIN_CMD_RECORD
};

#endif /* DSPKI2C_PLUGIN_HPP */
