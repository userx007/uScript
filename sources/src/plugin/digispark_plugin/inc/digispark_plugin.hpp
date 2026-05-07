#ifndef DIGISPARK_PLUGIN_HPP
#define DIGISPARK_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uDigisparkI2C.hpp"
#include "uDigisparkSPI.hpp"

#include <string>
#include <utility>
#include <span>
#include <vector>
#include <cstdint>

///////////////////////////////////////////////////////////////////
//                        PLUGIN VERSION                         //
///////////////////////////////////////////////////////////////////

#define DIGISPARK_PLUGIN_VERSION    "1.0.0.0"
#define DIGISPARK_PLUGIN_NAME       "DIGISPARK"

///////////////////////////////////////////////////////////////////
//                        PLUGIN COMMANDS                        //
///////////////////////////////////////////////////////////////////

/**
 * X-macro table of all commands exposed by this plugin.
 *
 * Each entry expands to:
 *   - a map insertion in the constructor
 *   - a private handler declaration  bool m_DIGISPARK_<CMD>(const std::string&) const
 *
 * To add a command: insert a new DIGISPARK_PLUGIN_CMD_RECORD line here
 * and provide the matching implementation in digispark_plugin.cpp.
 */
#define DIGISPARK_PLUGIN_COMMANDS_CONFIG_TABLE  \
DIGISPARK_PLUGIN_CMD_RECORD( INFO      )        \
DIGISPARK_PLUGIN_CMD_RECORD( CONFIG    )        \
DIGISPARK_PLUGIN_CMD_RECORD( I2C_SCAN  )        \
DIGISPARK_PLUGIN_CMD_RECORD( I2C_WRITE )        \
DIGISPARK_PLUGIN_CMD_RECORD( I2C_READ  )        \
DIGISPARK_PLUGIN_CMD_RECORD( I2C_WRRD  )        \
DIGISPARK_PLUGIN_CMD_RECORD( SPI_CFG   )        \
DIGISPARK_PLUGIN_CMD_RECORD( SPI_WRITE )        \
DIGISPARK_PLUGIN_CMD_RECORD( SPI_READ  )        \
DIGISPARK_PLUGIN_CMD_RECORD( SPI_XFER  )        \
DIGISPARK_PLUGIN_CMD_RECORD( SPI_WRREG )        \
DIGISPARK_PLUGIN_CMD_RECORD( SPI_RDREG )        \
DIGISPARK_PLUGIN_CMD_RECORD( SCRIPT    )        \

///////////////////////////////////////////////////////////////////
//                        PLUGIN INTERFACE                       //
///////////////////////////////////////////////////////////////////

/**
 * @brief Digispark ATtiny85 USB→I2C / USB→SPI bridge plugin.
 *
 * Exposes I2C and SPI operations as dispatched string commands.
 * The underlying hardware driver (I2CBridge or SPIBridge) is opened
 * on demand inside each command handler (RAII) so the HID device is
 * held only for the duration of a single call — or across all lines
 * of a SCRIPT file.
 *
 * Command syntax overview:
 *   DIGISPARK.INFO
 *   DIGISPARK.CONFIG  [v:<vid>] [p:<pid>] [r:<read_ms>] [w:<write_ms>]
 *   DIGISPARK.I2C_SCAN
 *   DIGISPARK.I2C_WRITE  <addr_hex>  <byte0_hex> [<byte1_hex> ...]
 *   DIGISPARK.I2C_READ   <addr_hex>  <n_bytes>
 *   DIGISPARK.I2C_WRRD   <addr_hex>  <reg_hex>   <n_bytes>
 *   DIGISPARK.SPI_CFG    [m:<mode_0_3>] [d:<div_0_3>]
 *   DIGISPARK.SPI_WRITE  <byte0_hex> [<byte1_hex> ...]
 *   DIGISPARK.SPI_READ   <n_bytes>
 *   DIGISPARK.SPI_XFER   <byte0_hex> [<byte1_hex> ...]
 *   DIGISPARK.SPI_WRREG  <reg_hex>   <val_hex>
 *   DIGISPARK.SPI_RDREG  <reg_hex>   <n_bytes>
 *   DIGISPARK.SCRIPT     <filename>  [<delay_ms>]
 */
class DigisparkPlugin : public PluginInterface
{

public:

    // ── Constructor: populate command dispatch map via X-macro ────────────────
    DigisparkPlugin()
        : m_strVersion(DIGISPARK_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData("")
        , m_u16Vid(I2CBridge::I2C_DIGISPARK_VID)
        , m_u16Pid(I2CBridge::I2C_DIGISPARK_PID)
        , m_u32ReadTimeout(2000)
        , m_u32WriteTimeout(2000)
        , m_eSpiMode(SPIBridge::SPIMode::Mode0)
        , m_eSpiClkDiv(SPIBridge::SPIClockDiv::Div4)
    {
        #define DIGISPARK_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert(std::make_pair(#a, &DigisparkPlugin::m_DIGISPARK_##a));
        DIGISPARK_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  DIGISPARK_PLUGIN_CMD_RECORD
    }

    ~DigisparkPlugin() = default;

    // ── PluginInterface implementation ────────────────────────────────────────

    bool isInitialized(void) const { return m_bIsInitialized; }
    bool isEnabled    (void) const { return m_bIsEnabled;     }
    bool isFaultTolerant(void) const { return m_bIsFaultTolerant; }
    bool isPrivileged (void) const { return m_bIsPrivileged;  }

    bool setParams(const PluginDataSet *psSetParams)
    {
        bool bRetVal = false;
        if (generic_setparams<DigisparkPlugin>(this, psSetParams,
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
        generic_getparams<DigisparkPlugin>(this, psGetParams);
    }

    bool doDispatch(const std::string& strCmd, const std::string& strParams) const
    {
        return generic_dispatch<DigisparkPlugin>(this, strCmd, strParams);
    }

    const PluginCommandsMap<DigisparkPlugin> *getMap(void) const { return &m_mapCmds; }
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

    bool setReadTimeout (const std::string& str) const
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

    bool setSpiMode(const std::string& str) const
    {
        uint32_t v;
        if (!numeric::str2uint32(str, v) || v > 3) return false;
        m_eSpiMode = static_cast<SPIBridge::SPIMode>(v);
        return true;
    }

    bool setSpiClkDiv(const std::string& str) const
    {
        uint32_t v;
        if (!numeric::str2uint32(str, v) || v > 3) return false;
        m_eSpiClkDiv = static_cast<SPIBridge::SPIClockDiv>(v);
        return true;
    }

private:

    // ── I2C driver helpers ────────────────────────────────────────────────────

    /** Write bytes to an I2C slave. */
    bool m_I2CWrite(uint8_t                  u8Addr,
                    std::span<const uint8_t>  data,
                    std::shared_ptr<I2CBridge> shpDrv) const;

    /** Read N bytes from an I2C slave into buffer. */
    bool m_I2CRead (uint8_t                  u8Addr,
                    size_t                   szLen,
                    std::span<uint8_t>        buffer,
                    std::shared_ptr<I2CBridge> shpDrv) const;

    /** Write a register address then repeated-START read. */
    bool m_I2CWriteRead(uint8_t                  u8Addr,
                        std::span<const uint8_t>  writeData,
                        size_t                   szReadLen,
                        std::span<uint8_t>        buffer,
                        std::shared_ptr<I2CBridge> shpDrv) const;

    // ── SPI driver helpers ────────────────────────────────────────────────────

    /** Write bytes on MOSI, discard MISO. */
    bool m_SPIWrite   (std::span<const uint8_t>  data,
                       std::shared_ptr<SPIBridge> shpDrv) const;

    /** Clock N bytes in, send 0x00 on MOSI. */
    bool m_SPIRead    (size_t                   szLen,
                       std::span<uint8_t>        buffer,
                       std::shared_ptr<SPIBridge> shpDrv) const;

    /** Full-duplex transfer: mosi in, miso out (same length). */
    bool m_SPITransfer(std::span<const uint8_t>  mosi,
                       std::span<uint8_t>         miso,
                       std::shared_ptr<SPIBridge> shpDrv) const;

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

    PluginCommandsMap<DigisparkPlugin> m_mapCmds;

    // ── Plugin metadata ───────────────────────────────────────────────────────

    std::string        m_strVersion;
    mutable std::string m_strResultData;
    bool               m_bIsInitialized;
    bool               m_bIsEnabled;
    bool               m_bIsFaultTolerant;
    bool               m_bIsPrivileged;
    std::string        m_strArtefactsPath;

    // ── Runtime-configurable parameters (mutable: CONFIG can change them) ─────

    mutable uint16_t              m_u16Vid;           ///< HID VID (default 0x16C0)
    mutable uint16_t              m_u16Pid;           ///< HID PID (default 0x05DF)
    mutable uint32_t              m_u32ReadTimeout;   ///< Read timeout  [ms]
    mutable uint32_t              m_u32WriteTimeout;  ///< Write timeout [ms]
    mutable SPIBridge::SPIMode    m_eSpiMode;         ///< SPI clock mode (0–3)
    mutable SPIBridge::SPIClockDiv m_eSpiClkDiv;      ///< SPI clock divider (0–3)

    // ── Command handler declarations (expanded by X-macro) ────────────────────

    #define DIGISPARK_PLUGIN_CMD_RECORD(a) \
        bool m_DIGISPARK_##a(const std::string& args) const;
    DIGISPARK_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  DIGISPARK_PLUGIN_CMD_RECORD
};

#endif /* DIGISPARK_PLUGIN_HPP */
