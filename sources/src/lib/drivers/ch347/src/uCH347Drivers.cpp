/**
 * @file uCH347Drivers.cpp
 * @brief Implementation of CH347SPI, CH347I2C, CH347GPIO, CH347JTAG.
 *
 * Each driver follows the same pattern as the UART driver:
 *   - open()  : call the matching CH347*_Init / CH347OpenDevice path
 *   - close() : call CH347CloseDevice
 *   - tout_read / tout_write : delegate to the CH347 C API, map bool → Status
 */

#include "uCH347Spi.hpp"
#include "uCH347I2c.hpp"
#include "uCH347Gpio.hpp"
#include "uCH347Jtag.hpp"

#include <cstring>
#include <cassert>
#include <vector>

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
// ---------------------------------------------------------------------------
static inline Status accessStatus(bool ok)
{
    return ok ? Status::SUCCESS : Status::PORT_ACCESS;
}
static inline Status readStatus(bool ok)
{
    return ok ? Status::SUCCESS : Status::READ_ERROR;
}
static inline Status writeStatus(bool ok)
{
    return ok ? Status::SUCCESS : Status::WRITE_ERROR;
}

// ============================================================================
// CH347SPI
// ============================================================================

Status CH347SPI::open(const std::string& strDevice, const mSpiCfgS& cfg)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    if (m_iHandle < 0)
        return Status::PORT_ACCESS;

    mSpiCfgS cfgCopy = cfg;
    return accessStatus(CH347SPI_Init(m_iHandle, &cfgCopy));
}

Status CH347SPI::close()
{
    if (m_iHandle < 0)
        return Status::SUCCESS;
    bool ok   = CH347CloseDevice(m_iHandle);
    m_iHandle = -1;
    return accessStatus(ok);
}

bool CH347SPI::is_open() const { return m_iHandle >= 0; }

Status CH347SPI::set_frequency(uint32_t iHz)
{
    return accessStatus(CH347SPI_SetFrequency(m_iHandle, iHz));
}

Status CH347SPI::set_data_bits(uint8_t iDataBits)
{
    return accessStatus(CH347SPI_SetDataBits(m_iHandle, iDataBits));
}

Status CH347SPI::set_auto_cs(bool disable)
{
    return accessStatus(CH347SPI_SetAutoCS(m_iHandle, disable));
}

Status CH347SPI::change_cs(uint8_t iStatus)
{
    return accessStatus(CH347SPI_ChangeCS(m_iHandle, iStatus));
}

Status CH347SPI::get_config(mSpiCfgS& cfg) const
{
    return accessStatus(CH347SPI_GetCfg(m_iHandle, &cfg));
}

std::pair<bool, uint8_t> CH347SPI::resolve_cs(const SpiXferOptions& opts) const
{
    return { opts.ignoreCS, static_cast<uint8_t>(opts.chipSelect) };
}

ReadResult CH347SPI::tout_read(uint32_t /*u32ReadTimeout*/,
                               std::span<uint8_t>  buffer,
                               const ReadOptions&  options) const
{
    /* SPI WriteRead is only meaningful for exact-length transfers */
    if (options.mode != ReadMode::Exact)
        return { Status::INVALID_PARAM, 0, false };

    /* If the caller embedded a CS selector in options.token, use it;
     * otherwise fall back to the instance default. */
    SpiXferOptions opts = m_xferOpts;
    if (!options.token.empty())
        opts.chipSelect = static_cast<SpiCS>(static_cast<uint8_t>(options.token[0]));

    return tout_xfer(buffer, opts);
}

WriteResult CH347SPI::tout_write(uint32_t /*u32WriteTimeout*/,
                                 std::span<const uint8_t> buffer) const
{
    return tout_write_ex(buffer, m_xferOpts);
}

ReadResult CH347SPI::tout_xfer(std::span<uint8_t>    buffer,
                               const SpiXferOptions& opts) const
{
    auto [ignoreCS, cs] = resolve_cs(opts);
    /* CH347SPI_WriteRead clocks MOSI out and fills the same buffer with MISO */
    bool ok = CH347SPI_WriteRead(m_iHandle,
                                 ignoreCS,
                                 cs,
                                 static_cast<int>(buffer.size()),
                                 buffer.data());
    return { readStatus(ok), ok ? buffer.size() : 0u, false };
}

WriteResult CH347SPI::tout_write_ex(std::span<const uint8_t> buffer,
                                    const SpiXferOptions&    opts) const
{
    /* CH347SPI_Write needs a non-const void*; copy into a local buffer */
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    auto [ignoreCS, cs] = resolve_cs(opts);
    bool ok = CH347SPI_Write(m_iHandle,
                             ignoreCS,
                             cs,
                             static_cast<int>(tmp.size()),
                             opts.writeStep,
                             tmp.data());
    return { writeStatus(ok), ok ? buffer.size() : 0u };
}

// ============================================================================
// CH347I2C
// ============================================================================

Status CH347I2C::open(const std::string& strDevice, I2cSpeed speed)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    if (m_iHandle < 0)
        return Status::PORT_ACCESS;
    return accessStatus(CH347I2C_Set(m_iHandle, static_cast<int>(speed)));
}

Status CH347I2C::close()
{
    if (m_iHandle < 0)
        return Status::SUCCESS;
    bool ok   = CH347CloseDevice(m_iHandle);
    m_iHandle = -1;
    return accessStatus(ok);
}

bool CH347I2C::is_open() const { return m_iHandle >= 0; }

Status CH347I2C::set_speed(I2cSpeed speed)
{
    return accessStatus(CH347I2C_Set(m_iHandle, static_cast<int>(speed)));
}

Status CH347I2C::set_clock_stretch(bool enable)
{
    return accessStatus(CH347I2C_SetStretch(m_iHandle, enable));
}

Status CH347I2C::set_drive_mode(uint8_t mode)
{
    return accessStatus(CH347I2C_SetDriveMode(m_iHandle, mode));
}

Status CH347I2C::set_ignore_nack(uint8_t mode)
{
    return accessStatus(CH347I2C_SetIgnoreNack(m_iHandle, mode));
}

Status CH347I2C::set_inter_transaction_delay_ms(int iDelay)
{
    return accessStatus(CH347I2C_SetDelaymS(m_iHandle, iDelay));
}

Status CH347I2C::set_ack_clock_delay_us(int iDelayUs)
{
    return accessStatus(CH347I2C_SetAckClk_DelayuS(m_iHandle, iDelayUs));
}

ReadResult CH347I2C::tout_read(uint32_t /*u32ReadTimeout*/,
                               std::span<uint8_t>  buffer,
                               const ReadOptions&  options) const
{
    if (options.mode != ReadMode::Exact)
        return { Status::INVALID_PARAM, 0, false };

    I2cReadOptions i2cOpts;
    if (!options.token.empty())
        i2cOpts.devAddr = options.token[0];

    return tout_read_i2c(buffer, i2cOpts);
}

WriteResult CH347I2C::tout_write(uint32_t /*u32WriteTimeout*/,
                                 std::span<const uint8_t> buffer) const
{
    /* Pure write: no read phase.
     * buffer[0] must be (devAddr << 1) | 0  (caller's responsibility). */
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    bool ok = CH347StreamI2C(m_iHandle,
                             static_cast<int>(tmp.size()), tmp.data(),
                             0, nullptr);
    return { writeStatus(ok), ok ? buffer.size() : 0u };
}

ReadResult CH347I2C::tout_read_i2c(std::span<uint8_t>    buffer,
                                   const I2cReadOptions& opts,
                                   int*                  retAck) const
{
    const int writeLen = static_cast<int>(opts.writeLen);
    const int readLen  = static_cast<int>(buffer.size()) - writeLen;

    if (readLen < 0)
        return { Status::INVALID_PARAM, 0, false };

    std::vector<uint8_t> writeBuf(buffer.begin(), buffer.begin() + writeLen);
    std::vector<uint8_t> readBuf(static_cast<size_t>(readLen));

    bool ok;
    if (retAck)
    {
        ok = CH347StreamI2C_RetAck(m_iHandle,
                                   writeLen, writeBuf.empty() ? nullptr : writeBuf.data(),
                                   readLen,  readBuf.empty()  ? nullptr : readBuf.data(),
                                   retAck);
    }
    else
    {
        ok = CH347StreamI2C(m_iHandle,
                            writeLen, writeBuf.empty() ? nullptr : writeBuf.data(),
                            readLen,  readBuf.empty()  ? nullptr : readBuf.data());
    }

    if (ok)
        std::memcpy(buffer.data(), readBuf.data(), static_cast<size_t>(readLen));

    return { readStatus(ok), ok ? static_cast<size_t>(readLen) : 0u, false };
}

Status CH347I2C::read_eeprom(EEPROM_TYPE        eepromType,
                             int                iAddr,
                             std::span<uint8_t> buffer) const
{
    return readStatus(CH347ReadEEPROM(m_iHandle,
                                     eepromType, iAddr,
                                     static_cast<int>(buffer.size()),
                                     buffer.data()));
}

Status CH347I2C::write_eeprom(EEPROM_TYPE              eepromType,
                              int                      iAddr,
                              std::span<const uint8_t> buffer) const
{
    /* CH347WriteEEPROM takes a non-const pointer */
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    return writeStatus(CH347WriteEEPROM(m_iHandle,
                                        eepromType, iAddr,
                                        static_cast<int>(tmp.size()),
                                        tmp.data()));
}

// ============================================================================
// CH347GPIO
// ============================================================================

Status CH347GPIO::open(const std::string& strDevice)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    return (m_iHandle >= 0) ? Status::SUCCESS : Status::PORT_ACCESS;
}

Status CH347GPIO::close()
{
    if (m_iHandle < 0)
        return Status::SUCCESS;
    bool ok   = CH347CloseDevice(m_iHandle);
    m_iHandle = -1;
    return accessStatus(ok);
}

bool CH347GPIO::is_open() const { return m_iHandle >= 0; }

ReadResult CH347GPIO::tout_read(uint32_t /*u32ReadTimeout*/,
                                std::span<uint8_t> buffer,
                                const ReadOptions& options) const
{
    if (options.mode != ReadMode::Exact)
        return { Status::INVALID_PARAM, 0, false };

    if (buffer.size() < GPIO_READ_BUFFER_SIZE)
        return { Status::INVALID_PARAM, 0, false };

    uint8_t iDir  = 0;
    uint8_t iData = 0;
    bool ok = CH347GPIO_Get(m_iHandle, &iDir, &iData);
    if (ok)
    {
        buffer[0] = iDir;
        buffer[1] = iData;
    }
    return { readStatus(ok), ok ? GPIO_READ_BUFFER_SIZE : 0u, false };
}

WriteResult CH347GPIO::tout_write(uint32_t /*u32WriteTimeout*/,
                                  std::span<const uint8_t> buffer) const
{
    if (buffer.size() < GPIO_BUFFER_SIZE)
        return { Status::INVALID_PARAM, 0u };

    bool ok = CH347GPIO_Set(m_iHandle,
                            buffer[BUF_IDX_ENABLE],
                            buffer[BUF_IDX_DIR],
                            buffer[BUF_IDX_DATA]);
    return { writeStatus(ok), ok ? GPIO_BUFFER_SIZE : 0u };
}

Status CH347GPIO::pin_write(uint8_t pin, bool level) const
{
    uint8_t levelMask = level ? pin : 0x00;
    return writeStatus(CH347GPIO_Set(m_iHandle, pin, pin, levelMask));
}

Status CH347GPIO::pin_read(uint8_t pinMask, uint8_t& level) const
{
    uint8_t iDir = 0, iData = 0;
    bool ok = CH347GPIO_Get(m_iHandle, &iDir, &iData);
    if (ok) level = iData & pinMask;
    return readStatus(ok);
}

Status CH347GPIO::pin_set_direction(uint8_t pinMask, bool isOutput) const
{
    uint8_t dir = isOutput ? pinMask : 0x00;
    return accessStatus(CH347GPIO_Set(m_iHandle, pinMask, dir, 0x00));
}

Status CH347GPIO::pins_write(uint8_t pinMask, uint8_t levelMask) const
{
    return writeStatus(CH347GPIO_Set(m_iHandle, pinMask, pinMask, levelMask));
}

Status CH347GPIO::irq_set(uint8_t pinIndex, GpioIrqEdge edge, void* handler) const
{
    return accessStatus(CH347GPIO_IRQ_Set(m_iHandle,
                                          pinIndex,
                                          edge != GpioIrqEdge::None,
                                          static_cast<uint8_t>(edge),
                                          handler));
}

Status CH347GPIO::irq_disable(uint8_t pinIndex) const
{
    return accessStatus(CH347GPIO_IRQ_Set(m_iHandle,
                                          pinIndex,
                                          false,
                                          IRQ_TYPE_NONE,
                                          nullptr));
}

// ============================================================================
// CH347JTAG
// ============================================================================

Status CH347JTAG::open(const std::string& strDevice, uint8_t iClockRate)
{
    m_iHandle = CH347OpenDevice(strDevice.c_str());
    if (m_iHandle < 0)
        return Status::PORT_ACCESS;
    return accessStatus(CH347Jtag_INIT(m_iHandle, iClockRate));
}

Status CH347JTAG::close()
{
    if (m_iHandle < 0)
        return Status::SUCCESS;
    bool ok   = CH347CloseDevice(m_iHandle);
    m_iHandle = -1;
    return accessStatus(ok);
}

bool CH347JTAG::is_open() const { return m_iHandle >= 0; }

Status CH347JTAG::get_clock_rate(uint8_t& iClockRate) const
{
    return accessStatus(CH347Jtag_GetCfg(m_iHandle, &iClockRate));
}

ReadResult CH347JTAG::tout_read(uint32_t /*u32ReadTimeout*/,
                                std::span<uint8_t> buffer,
                                const ReadOptions& options) const
{
    if (options.mode != ReadMode::Exact)
        return { Status::INVALID_PARAM, 0, false };

    JtagRegister reg = JtagRegister::DR;
    if (!options.token.empty() && (options.token[0] & JTAG_TOKEN_IR_FLAG))
        reg = JtagRegister::IR;

    m_lastReg    = reg;
    Status s     = read_register(reg, buffer);
    size_t nRead = (s == Status::SUCCESS) ? buffer.size() : 0u;
    return { s, nRead, false };
}

WriteResult CH347JTAG::tout_write(uint32_t /*u32WriteTimeout*/,
                                  std::span<const uint8_t> buffer) const
{
    Status s = write_register(m_lastReg, buffer);
    return { s, s == Status::SUCCESS ? buffer.size() : 0u };
}

Status CH347JTAG::tap_reset() const
{
    /* CH347Jtag_Reset returns 0 on success (not the usual bool convention) */
    return accessStatus(CH347Jtag_Reset(m_iHandle) == 0);
}

Status CH347JTAG::tap_reset_trst(bool highLevel) const
{
    return accessStatus(CH347Jtag_ResetTrst(m_iHandle, highLevel));
}

Status CH347JTAG::tap_set_state(uint8_t tapState) const
{
    return accessStatus(CH347Jtag_SwitchTapState(m_iHandle, tapState));
}

Status CH347JTAG::tap_tms_change(std::span<const uint8_t> tmsBytes,
                                 uint32_t step, uint32_t skip) const
{
    /* CH347Jtag_TmsChange takes a non-const pointer */
    std::vector<uint8_t> tmp(tmsBytes.begin(), tmsBytes.end());
    return accessStatus(CH347Jtag_TmsChange(m_iHandle,
                                            tmp.data(), step, skip));
}

Status CH347JTAG::write_register(JtagRegister             reg,
                                 std::span<const uint8_t> buffer) const
{
    std::vector<uint8_t> tmp(buffer.begin(), buffer.end());
    bool ok = (reg == JtagRegister::DR)
        ? CH347Jtag_ByteWriteDR(m_iHandle, static_cast<int>(tmp.size()), tmp.data())
        : CH347Jtag_ByteWriteIR(m_iHandle, static_cast<int>(tmp.size()), tmp.data());
    return writeStatus(ok);
}

Status CH347JTAG::read_register(JtagRegister       reg,
                                std::span<uint8_t> buffer) const
{
    uint32_t readLen = static_cast<uint32_t>(buffer.size());
    bool ok = (reg == JtagRegister::DR)
        ? CH347Jtag_ByteReadDR(m_iHandle, &readLen, buffer.data())
        : CH347Jtag_ByteReadIR(m_iHandle, &readLen, buffer.data());
    return readStatus(ok);
}

ReadResult CH347JTAG::write_read(JtagRegister             reg,
                                 std::span<const uint8_t> writeBuf,
                                 std::span<uint8_t>       readBuf) const
{
    std::vector<uint8_t> wTmp(writeBuf.begin(), writeBuf.end());
    uint32_t readLen = static_cast<uint32_t>(readBuf.size());
    bool ok = CH347Jtag_WriteRead(m_iHandle,
                                  reg == JtagRegister::DR,
                                  static_cast<int>(wTmp.size()), wTmp.data(),
                                  &readLen, readBuf.data());
    return { readStatus(ok), ok ? static_cast<size_t>(readLen) : 0u, false };
}

ReadResult CH347JTAG::write_read_fast(JtagRegister             reg,
                                      std::span<const uint8_t> writeBuf,
                                      std::span<uint8_t>       readBuf) const
{
    std::vector<uint8_t> wTmp(writeBuf.begin(), writeBuf.end());
    uint32_t readLen = static_cast<uint32_t>(readBuf.size());
    bool ok = CH347Jtag_WriteRead_Fast(m_iHandle,
                                       reg == JtagRegister::DR,
                                       static_cast<int>(wTmp.size()), wTmp.data(),
                                       &readLen, readBuf.data());
    return { readStatus(ok), ok ? static_cast<size_t>(readLen) : 0u, false };
}

Status CH347JTAG::io_scan(std::span<uint8_t> dataBuffer,
                          uint32_t           dataBitsNb,
                          bool               isRead,
                          bool               isLastPacket) const
{
    bool ok = CH347Jtag_IoScanT(m_iHandle,
                                dataBuffer.data(), dataBitsNb,
                                isRead, isLastPacket);
    return readStatus(ok);
}

/*static*/
uint32_t CH347JTAG::build_tms_clock(std::span<uint8_t> pkt,
                                    uint32_t           tms,
                                    uint32_t           bi)
{
    return CH347Jtag_ClockTms(pkt.data(), tms, bi);
}

/*static*/
uint32_t CH347JTAG::build_idle_clock(std::span<uint8_t> pkt, uint32_t bi)
{
    return CH347Jtag_IdleClock(pkt.data(), bi);
}
