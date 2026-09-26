/**
 * @file uCH347Drivers.cpp
 * @brief Implementation of CH347SPI, CH347I2C, CH347GPIO, CH347JTAG.
 *
 * Each driver follows the same pattern:
 *   - open()       : call CH347OpenDevice then the matching init function
 *   - close()      : call CH347CloseDevice
 *   - tout_read / tout_write : delegate to the CH347 C API, map bool → Status
 *
 * Cross-platform notes
 * ====================
 * All CH347 API calls go through ch347_compat.h which provides:
 *   - A unified CH347_HANDLE type (int on Linux, ULONG on Windows).
 *   - CH347_INVALID_HANDLE sentinel (-1 on Linux, (ULONG)-1 on Windows).
 *   - Inline shims that bridge every Linux↔Windows API difference
 *     (different function names, signature variations, missing functions).
 *
 * Handle validity is tested with != CH347_INVALID_HANDLE rather than >= 0
 * to remain correct for the unsigned ULONG type used on Windows.
 */
#include "ICommDriver.hpp"
#include "ch347_compat.h"
#include "uCH347Gpio.hpp"
#include "uCH347I2c.hpp"
#include "uCH347Jtag.hpp"
#include "uCH347Spi.hpp"
#include "uLogger.hpp"

#include <cassert>
#include <ch347_lib.h>
#include <cstring>
#include <span>
#include <stdint.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "CH347_DRV   |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            IMPLEMENTATION                                   //
/////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// Pull ICommDriver's nested types into file scope.
// Return-type tokens in out-of-line function definitions are parsed *before*
// the enclosing ClassName:: scope is entered, so bare names like Status,
// ReadResult, etc. would not be found without these aliases.
// ---------------------------------------------------------------------------
using Status      = ICommDriver::Status;
using ReadResult  = ICommDriver::ReadResult;
using WriteResult = ICommDriver::WriteResult;
using ReadOptions = ICommDriver::ReadOptions;
using ReadMode    = ICommDriver::ReadMode;

// ---------------------------------------------------------------------------
// Context-specific bool → Status helpers.
//
// ICommDriver::Status has no generic "Error" value; map failures to the
// most semantically accurate code:
//   PORT_ACCESS   – device open / close / configuration command failed
//   READ_ERROR    – a read transfer returned false
//   WRITE_ERROR   – a write transfer returned false
//   INVALID_PARAM – unsupported mode or bad argument from the caller
//
// Each helper takes the name of the CH347 C-API call it is wrapping and
// logs an error trace whenever that call reports failure, so every one of
// the ~40 thin call sites below gets a meaningful failure trace "for free"
// without repeating an if/LOG_PRINT block at each site individually.
// ---------------------------------------------------------------------------
static inline Status accessStatus(bool bOk, const char *pstrOp)
{
    if (!bOk) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrOp); LOG_STRING("failed"));
    }
    return bOk ? Status::SUCCESS : Status::PORT_ACCESS;
}

static inline Status readStatus(bool bOk, const char *pstrOp)
{
    if (!bOk) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrOp); LOG_STRING("failed"));
    }
    return bOk ? Status::SUCCESS : Status::READ_ERROR;
}

static inline Status writeStatus(bool bOk, const char *pstrOp)
{
    if (!bOk) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrOp); LOG_STRING("failed"));
    }
    return bOk ? Status::SUCCESS : Status::WRITE_ERROR;
}

// ============================================================================
// CH347SPI
// ============================================================================

Status CH347SPI::open(const std::string &strDevice, const mSpiCfgS &cfg)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    if (m_iHandle == CH347_INVALID_HANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CH347OpenDevice failed for"); LOG_STRING(strDevice.c_str()));
        return Status::PORT_ACCESS;
    }

    mSpiCfgS cfgCopy = cfg;
    Status result    = accessStatus(CH347SPI_Init(m_iHandle, &cfgCopy), "CH347SPI_Init");
    if (result == Status::SUCCESS) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("CH347SPI ["); LOG_STRING(strDevice.c_str());
                  LOG_STRING("] opened, handle:"); LOG_INT(m_iHandle));
    }
    return result;
}

Status CH347SPI::close()
{
    if (m_iHandle == CH347_INVALID_HANDLE) {
        return Status::SUCCESS;
    }
    bool ok = CH347CloseDevice(m_iHandle);
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CH347SPI closed, handle:"); LOG_INT(m_iHandle));
    m_iHandle = CH347_INVALID_HANDLE;
    return accessStatus(ok, "CH347CloseDevice");
}

bool CH347SPI::is_open() const
{
    return m_iHandle != CH347_INVALID_HANDLE;
}

Status CH347SPI::set_frequency(uint32_t u32Hz)
{
    return accessStatus(CH347SPI_SetFrequency(m_iHandle, u32Hz), "CH347SPI_SetFrequency");
}

Status CH347SPI::set_data_bits(uint8_t u8DataBits)
{
    return accessStatus(CH347SPI_SetDataBits(m_iHandle, u8DataBits), "CH347SPI_SetDataBits");
}

Status CH347SPI::set_auto_cs(bool bDisable)
{
    // On Windows CH347SPI_SetAutoCS is a documented no-op shim; see
    // ch347_compat.h for details.  Callers that need this on Windows must
    // set mSpiCfgS::iIsAutoDeativeCS and call open() / CH347SPI_Init() again.
    return accessStatus(CH347SPI_SetAutoCS(m_iHandle, bDisable), "CH347SPI_SetAutoCS");
}

Status CH347SPI::change_cs(uint8_t u8Status)
{
    return accessStatus(CH347SPI_ChangeCS(m_iHandle, u8Status), "CH347SPI_ChangeCS");
}

Status CH347SPI::get_config(mSpiCfgS &cfg) const
{
    return accessStatus(CH347SPI_GetCfg(m_iHandle, &cfg), "CH347SPI_GetCfg");
}

std::pair<bool, uint8_t> CH347SPI::resolve_cs(const SpiXferOptions &sOpts) const
{
    return {sOpts.ignoreCS, static_cast<uint8_t>(sOpts.chipSelect)};
}

ReadResult CH347SPI::tout_read(uint32_t /*u32ReadTimeout*/,
                               std::span<uint8_t> buffer,
                               const ReadOptions &sOptions,
                               std::string_view xtra_params,
                               std::stop_token /*stop_tok*/) const
{
    /* SPI WriteRead is only meaningful for exact-length transfers */
    if (sOptions.mode != ReadMode::Exact) {
        return {Status::INVALID_PARAM, 0, false};
    }

    /* If the caller embedded a CS selector in sOptions.token, use it;
     * otherwise fall back to the instance default. */
    SpiXferOptions opts = m_xferOpts;
    if (!sOptions.token.empty()) {
        opts.chipSelect = static_cast<SpiCS>(static_cast<uint8_t>(sOptions.token[0]));
    }

    return tout_xfer(buffer, opts);
}

WriteResult CH347SPI::tout_write(uint32_t /*u32WriteTimeout*/,
                                 std::span<const uint8_t> buffer,
                                 std::string_view xtra_params,
                                 std::stop_token /*stop_tok*/) const
{
    return tout_write_ex(buffer, m_xferOpts);
}

ReadResult CH347SPI::tout_xfer(std::span<uint8_t> buffer,
                               const SpiXferOptions &sOpts) const
{
    auto [ignoreCS, cs] = resolve_cs(sOpts);
    /* CH347SPI_WriteRead clocks MOSI out and fills the same buffer with MISO */
    bool ok             = CH347SPI_WriteRead(m_iHandle,
                                             ignoreCS,
                                             cs,
                                             static_cast<int>(buffer.size()),
                                             buffer.data());
    return {readStatus(ok, "CH347SPI_WriteRead"), ok ? buffer.size() : 0u, false};
}

WriteResult CH347SPI::tout_write_ex(std::span<const uint8_t> buffer,
                                    const SpiXferOptions &sOpts) const
{
    /* CH347SPI_Write needs a non-const void*; copy into a local buffer */
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    auto [ignoreCS, cs] = resolve_cs(sOpts);
    bool ok             = CH347SPI_Write(m_iHandle,
                                         ignoreCS,
                                         cs,
                                         static_cast<int>(tmp.size()),
                                         sOpts.writeStep,
                                         tmp.data());
    return {writeStatus(ok, "CH347SPI_Write"), ok ? buffer.size() : 0u};
}

// ============================================================================
// CH347I2C
// ============================================================================

Status CH347I2C::open(const std::string &strDevice, I2cSpeed eSpeed)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    if (m_iHandle == CH347_INVALID_HANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CH347OpenDevice failed for"); LOG_STRING(strDevice.c_str()));
        return Status::PORT_ACCESS;
    }
    Status result = accessStatus(CH347I2C_Set(m_iHandle, static_cast<int>(eSpeed)), "CH347I2C_Set");
    if (result == Status::SUCCESS) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("CH347I2C ["); LOG_STRING(strDevice.c_str());
                  LOG_STRING("] opened, handle:"); LOG_INT(m_iHandle));
    }
    return result;
}

Status CH347I2C::close()
{
    if (m_iHandle == CH347_INVALID_HANDLE) {
        return Status::SUCCESS;
    }
    bool ok = CH347CloseDevice(m_iHandle);
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CH347I2C closed, handle:"); LOG_INT(m_iHandle));
    m_iHandle = CH347_INVALID_HANDLE;
    return accessStatus(ok, "CH347CloseDevice");
}

bool CH347I2C::is_open() const
{
    return m_iHandle != CH347_INVALID_HANDLE;
}

Status CH347I2C::set_speed(I2cSpeed eSpeed)
{
    return accessStatus(CH347I2C_Set(m_iHandle, static_cast<int>(eSpeed)), "CH347I2C_Set");
}

Status CH347I2C::set_clock_stretch(bool bEnable)
{
    return accessStatus(CH347I2C_SetStretch(m_iHandle, bEnable), "CH347I2C_SetStretch");
}

Status CH347I2C::set_drive_mode(uint8_t u8Mode)
{
    // Routed to CH347I2C_SetDriverMode on Windows via the compat shim.
    return accessStatus(CH347I2C_SetDriveMode(m_iHandle, u8Mode), "CH347I2C_SetDriveMode");
}

Status CH347I2C::set_ignore_nack(uint8_t u8Mode)
{
    return accessStatus(CH347I2C_SetIgnoreNack(m_iHandle, u8Mode), "CH347I2C_SetIgnoreNack");
}

Status CH347I2C::set_inter_transaction_delay_ms(int iDelay)
{
    return accessStatus(CH347I2C_SetDelaymS(m_iHandle, iDelay), "CH347I2C_SetDelaymS");
}

Status CH347I2C::set_ack_clock_delay_us(int iDelayUs)
{
    return accessStatus(CH347I2C_SetAckClk_DelayuS(m_iHandle, iDelayUs), "CH347I2C_SetAckClk_DelayuS");
}

ReadResult CH347I2C::tout_read(uint32_t /*u32ReadTimeout*/,
                               std::span<uint8_t> buffer,
                               const ReadOptions &sOptions,
                               std::string_view xtra_params,
                               std::stop_token /*stop_tok*/) const
{
    if (sOptions.mode != ReadMode::Exact) {
        return {Status::INVALID_PARAM, 0, false};
    }

    I2cReadOptions i2cOpts;
    if (!sOptions.token.empty()) {
        i2cOpts.devAddr = sOptions.token[0];
    }

    return tout_read_i2c(buffer, i2cOpts);
}

WriteResult CH347I2C::tout_write(uint32_t /*u32WriteTimeout*/,
                                 std::span<const uint8_t> buffer,
                                 std::string_view xtra_params,
                                 std::stop_token /*stop_tok*/) const
{
    /* Pure write: no read phase.
     * buffer[0] must be (devAddr << 1) | 0  (caller's responsibility). */
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    bool ok = CH347StreamI2C(m_iHandle,
                             static_cast<int>(tmp.size()), tmp.data(),
                             0, nullptr);
    return {writeStatus(ok, "CH347StreamI2C"), ok ? buffer.size() : 0u};
}

ReadResult CH347I2C::tout_read_i2c(std::span<uint8_t> buffer,
                                   const I2cReadOptions &sOpts,
                                   int *pRetAck) const
{
    const int writeLen = static_cast<int>(sOpts.writeLen);
    const int readLen  = static_cast<int>(buffer.size()) - writeLen;

    if (readLen < 0) {
        return {Status::INVALID_PARAM, 0, false};
    }

    std::vector<uint8_t> writeBuf(buffer.begin(), buffer.begin() + writeLen);
    std::vector<uint8_t> readBuf(static_cast<size_t>(readLen));

    bool ok;
    if (pRetAck) {
        // Routed to CH347StreamI2C_RetACK on Windows via the compat shim.
        ok = CH347StreamI2C_RetAck(m_iHandle,
                                   writeLen, writeBuf.empty() ? nullptr : writeBuf.data(),
                                   readLen, readBuf.empty() ? nullptr : readBuf.data(),
                                   pRetAck);
    } else {
        ok = CH347StreamI2C(m_iHandle,
                            writeLen, writeBuf.empty() ? nullptr : writeBuf.data(),
                            readLen, readBuf.empty() ? nullptr : readBuf.data());
    }

    if (ok) {
        std::memcpy(buffer.data(), readBuf.data(), static_cast<size_t>(readLen));
    }

    return {readStatus(ok, pRetAck ? "CH347StreamI2C_RetAck" : "CH347StreamI2C"), ok ? static_cast<size_t>(readLen) : 0u, false};
}

Status CH347I2C::read_eeprom(EEPROM_TYPE eepromType,
                             int iAddr,
                             std::span<uint8_t> buffer) const
{
    return readStatus(CH347ReadEEPROM(m_iHandle,
                                      eepromType, iAddr,
                                      static_cast<int>(buffer.size()),
                                      buffer.data()),
                      "CH347ReadEEPROM");
}

Status CH347I2C::write_eeprom(EEPROM_TYPE eepromType,
                              int iAddr,
                              std::span<const uint8_t> buffer) const
{
    /* CH347WriteEEPROM takes a non-const pointer */
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    return writeStatus(CH347WriteEEPROM(m_iHandle,
                                        eepromType, iAddr,
                                        static_cast<int>(tmp.size()),
                                        tmp.data()),
                       "CH347WriteEEPROM");
}

// ============================================================================
// CH347GPIO
// ============================================================================

Status CH347GPIO::open(const std::string &strDevice)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    if (m_iHandle == CH347_INVALID_HANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CH347OpenDevice failed for"); LOG_STRING(strDevice.c_str()));
        return Status::PORT_ACCESS;
    }
    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("CH347GPIO ["); LOG_STRING(strDevice.c_str());
              LOG_STRING("] opened, handle:"); LOG_INT(m_iHandle));
    return Status::SUCCESS;
}

Status CH347GPIO::close()
{
    if (m_iHandle == CH347_INVALID_HANDLE) {
        return Status::SUCCESS;
    }
    bool ok = CH347CloseDevice(m_iHandle);
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CH347GPIO closed, handle:"); LOG_INT(m_iHandle));
    m_iHandle = CH347_INVALID_HANDLE;
    return accessStatus(ok, "CH347CloseDevice");
}

bool CH347GPIO::is_open() const
{
    return m_iHandle != CH347_INVALID_HANDLE;
}

ReadResult CH347GPIO::tout_read(uint32_t /*u32ReadTimeout*/,
                                std::span<uint8_t> buffer,
                                const ReadOptions &sOptions,
                                std::string_view xtra_params,
                                std::stop_token /*stop_tok*/) const
{
    if (sOptions.mode != ReadMode::Exact) {
        return {Status::INVALID_PARAM, 0, false};
    }

    if (buffer.size() < GPIO_READ_BUFFER_SIZE) {
        return {Status::INVALID_PARAM, 0, false};
    }

    uint8_t iDir  = 0;
    uint8_t iData = 0;
    bool ok       = CH347GPIO_Get(m_iHandle, &iDir, &iData);
    if (ok) {
        buffer[0] = iDir;
        buffer[1] = iData;
    }
    return {readStatus(ok, "CH347GPIO_Get"), ok ? GPIO_READ_BUFFER_SIZE : 0u, false};
}

WriteResult CH347GPIO::tout_write(uint32_t /*u32WriteTimeout*/,
                                  std::span<const uint8_t> buffer,
                                  std::string_view xtra_params,
                                  std::stop_token /*stop_tok*/) const
{
    if (buffer.size() < GPIO_BUFFER_SIZE) {
        return {Status::INVALID_PARAM, 0u};
    }

    bool ok = CH347GPIO_Set(m_iHandle,
                            buffer[BUF_IDX_ENABLE],
                            buffer[BUF_IDX_DIR],
                            buffer[BUF_IDX_DATA]);
    return {writeStatus(ok, "CH347GPIO_Set"), ok ? GPIO_BUFFER_SIZE : 0u};
}

Status CH347GPIO::pin_write(uint8_t u8Pin, bool bLevel) const
{
    uint8_t levelMask = bLevel ? u8Pin : 0x00;
    return writeStatus(CH347GPIO_Set(m_iHandle, u8Pin, u8Pin, levelMask), "CH347GPIO_Set");
}

Status CH347GPIO::pin_read(uint8_t u8PinMask, uint8_t &u8Level) const
{
    uint8_t iDir = 0, iData = 0;
    bool ok = CH347GPIO_Get(m_iHandle, &iDir, &iData);
    if (ok) {
        u8Level = iData & u8PinMask;
    }
    return readStatus(ok, "CH347GPIO_Get");
}

Status CH347GPIO::pin_set_direction(uint8_t u8PinMask, bool bIsOutput) const
{
    uint8_t dir = bIsOutput ? u8PinMask : 0x00;
    return accessStatus(CH347GPIO_Set(m_iHandle, u8PinMask, dir, 0x00), "CH347GPIO_Set");
}

Status CH347GPIO::pins_write(uint8_t u8PinMask, uint8_t u8LevelMask) const
{
    return writeStatus(CH347GPIO_Set(m_iHandle, u8PinMask, u8PinMask, u8LevelMask), "CH347GPIO_Set");
}

Status CH347GPIO::irq_set(uint8_t u8PinIndex, GpioIrqEdge eEdge, void *pvHandler) const
{
    // On Windows this is routed to CH347SetIntRoutine() via the compat shim;
    // pvHandler must carry the Windows CALLBACK calling convention.
    return accessStatus(CH347GPIO_IRQ_Set(m_iHandle,
                                          u8PinIndex,
                                          eEdge != GpioIrqEdge::None,
                                          static_cast<uint8_t>(eEdge),
                                          pvHandler),
                        "CH347GPIO_IRQ_Set");
}

Status CH347GPIO::irq_disable(uint8_t u8PinIndex) const
{
    return accessStatus(CH347GPIO_IRQ_Set(m_iHandle,
                                          u8PinIndex,
                                          false,
                                          IRQ_TYPE_NONE,
                                          nullptr),
                        "CH347GPIO_IRQ_Set");
}

// ============================================================================
// CH347JTAG
// ============================================================================

Status CH347JTAG::open(const std::string &strDevice, uint8_t u8ClockRate)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    if (m_iHandle == CH347_INVALID_HANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CH347OpenDevice failed for"); LOG_STRING(strDevice.c_str()));
        return Status::PORT_ACCESS;
    }
    Status result = accessStatus(CH347Jtag_INIT(m_iHandle, u8ClockRate), "CH347Jtag_INIT");
    if (result == Status::SUCCESS) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("CH347JTAG ["); LOG_STRING(strDevice.c_str());
                  LOG_STRING("] opened, handle:"); LOG_INT(m_iHandle));
    }
    return result;
}

Status CH347JTAG::close()
{
    if (m_iHandle == CH347_INVALID_HANDLE) {
        return Status::SUCCESS;
    }
    bool ok = CH347CloseDevice(m_iHandle);
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CH347JTAG closed, handle:"); LOG_INT(m_iHandle));
    m_iHandle = CH347_INVALID_HANDLE;
    return accessStatus(ok, "CH347CloseDevice");
}

bool CH347JTAG::is_open() const
{
    return m_iHandle != CH347_INVALID_HANDLE;
}

Status CH347JTAG::get_clock_rate(uint8_t &u8ClockRate) const
{
    return accessStatus(CH347Jtag_GetCfg(m_iHandle, &u8ClockRate), "CH347Jtag_GetCfg");
}

ReadResult CH347JTAG::tout_read(uint32_t /*u32ReadTimeout*/,
                                std::span<uint8_t> buffer,
                                const ReadOptions &sOptions,
                                std::string_view xtra_params,
                                std::stop_token /*stop_tok*/) const
{
    if (sOptions.mode != ReadMode::Exact) {
        return {Status::INVALID_PARAM, 0, false};
    }

    JtagRegister reg = JtagRegister::DR;
    if (!sOptions.token.empty() && (sOptions.token[0] & JTAG_TOKEN_IR_FLAG)) {
        reg = JtagRegister::IR;
    }

    m_lastReg    = reg;
    Status s     = read_register(reg, buffer);
    size_t nRead = (s == Status::SUCCESS) ? buffer.size() : 0u;
    return {s, nRead, false};
}

WriteResult CH347JTAG::tout_write(uint32_t /*u32WriteTimeout*/,
                                  std::span<const uint8_t> buffer,
                                  std::string_view xtra_params,
                                  std::stop_token /*stop_tok*/) const
{
    Status s = write_register(m_lastReg, buffer);
    return {s, s == Status::SUCCESS ? buffer.size() : 0u};
}

Status CH347JTAG::tap_reset() const
{
    // Linux: CH347Jtag_Reset() returns 0 on success.
    // Windows: shim calls CH347Jtag_SwitchTapStateEx(idx, 0), returns 0/-1.
    return accessStatus(CH347Jtag_Reset(m_iHandle) == 0, "CH347Jtag_Reset");
}

Status CH347JTAG::tap_reset_trst(bool bHighLevel) const
{
    // On Windows this is a no-op shim; see ch347_compat.h and header docs.
    return accessStatus(CH347Jtag_ResetTrst(m_iHandle, bHighLevel), "CH347Jtag_ResetTrst");
}

Status CH347JTAG::tap_set_state(uint8_t u8TapState) const
{
    // Routed to CH347Jtag_SwitchTapStateEx on Windows via the compat shim.
    return accessStatus(CH347Jtag_SwitchTapState(m_iHandle, u8TapState), "CH347Jtag_SwitchTapState");
}

Status CH347JTAG::tap_tms_change(std::span<const uint8_t> tmsBytes,
                                 uint32_t u32Step, uint32_t u32Skip) const
{
    /* CH347Jtag_TmsChange takes a non-const pointer */
    std::vector<uint8_t> tmp(tmsBytes.begin(), tmsBytes.end());
    return accessStatus(CH347Jtag_TmsChange(m_iHandle,
                                            tmp.data(), u32Step, u32Skip),
                        "CH347Jtag_TmsChange");
}

Status CH347JTAG::write_register(JtagRegister eReg,
                                 std::span<const uint8_t> buffer) const
{
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    bool ok = (eReg == JtagRegister::DR)
                  ? CH347Jtag_ByteWriteDR(m_iHandle, static_cast<int>(tmp.size()), tmp.data())
                  : CH347Jtag_ByteWriteIR(m_iHandle, static_cast<int>(tmp.size()), tmp.data());
    return writeStatus(ok, (eReg == JtagRegister::DR) ? "CH347Jtag_ByteWriteDR" : "CH347Jtag_ByteWriteIR");
}

Status CH347JTAG::read_register(JtagRegister eReg,
                                std::span<uint8_t> buffer) const
{
    uint32_t readLen = static_cast<uint32_t>(buffer.size());
    bool ok          = (eReg == JtagRegister::DR)
                           ? CH347Jtag_ByteReadDR(m_iHandle, &readLen, buffer.data())
                           : CH347Jtag_ByteReadIR(m_iHandle, &readLen, buffer.data());
    return readStatus(ok, (eReg == JtagRegister::DR) ? "CH347Jtag_ByteReadDR" : "CH347Jtag_ByteReadIR");
}

ReadResult CH347JTAG::write_read(JtagRegister eReg,
                                 std::span<const uint8_t> writeBuf,
                                 std::span<uint8_t> readBuf) const
{
    std::vector<uint8_t> wTmp(writeBuf.begin(), writeBuf.end());
    uint32_t readLen = static_cast<uint32_t>(readBuf.size());
    bool ok          = CH347Jtag_WriteRead(m_iHandle,
                                           eReg == JtagRegister::DR,
                                           static_cast<int>(wTmp.size()), wTmp.data(),
                                           &readLen, readBuf.data());
    return {readStatus(ok, "CH347Jtag_WriteRead"), ok ? static_cast<size_t>(readLen) : 0u, false};
}

ReadResult CH347JTAG::write_read_fast(JtagRegister eReg,
                                      std::span<const uint8_t> writeBuf,
                                      std::span<uint8_t> readBuf) const
{
    std::vector<uint8_t> wTmp(writeBuf.begin(), writeBuf.end());
    uint32_t readLen = static_cast<uint32_t>(readBuf.size());
    bool ok          = CH347Jtag_WriteRead_Fast(m_iHandle,
                                                eReg == JtagRegister::DR,
                                                static_cast<int>(wTmp.size()), wTmp.data(),
                                                &readLen, readBuf.data());
    return {readStatus(ok, "CH347Jtag_WriteRead_Fast"), ok ? static_cast<size_t>(readLen) : 0u, false};
}

Status CH347JTAG::io_scan(std::span<uint8_t> dataBuffer,
                          uint32_t u32DataBitsNb,
                          bool bIsRead,
                          bool bIsLastPacket) const
{
    // On Windows bIsLastPacket is ignored by the CH347Jtag_IoScanT shim;
    // see ch347_compat.h and header docs for details.
    bool ok = CH347Jtag_IoScanT(m_iHandle,
                                dataBuffer.data(), u32DataBitsNb,
                                bIsRead, bIsLastPacket);
    return readStatus(ok, "CH347Jtag_IoScanT");
}

/*static*/
uint32_t CH347JTAG::build_tms_clock(std::span<uint8_t> pkt,
                                    uint32_t u32Tms,
                                    uint32_t u32Bi)
{
    return CH347Jtag_ClockTms(pkt.data(), u32Tms, u32Bi);
}

/*static*/
uint32_t CH347JTAG::build_idle_clock(std::span<uint8_t> pkt, uint32_t u32Bi)
{
    return CH347Jtag_IdleClock(pkt.data(), u32Bi);
}
