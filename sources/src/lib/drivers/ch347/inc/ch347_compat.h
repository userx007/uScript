#ifndef CH347_COMPAT_H
#define CH347_COMPAT_H

/**
 * @file ch347_compat.h
 * @brief Platform compatibility layer for the CH347 vendor library.
 *
 * Provides a unified API surface over:
 *   - Linux  : WCH libch347 (third_party/linux/ch347_lib.h)
 *   - Windows: WCH CH347DLL (third_party/windows/CH347DLL.H)
 *
 * All driver code should include this header instead of either vendor
 * header directly.
 *
 * ──────────────────────────────────────────────────────────────────────────
 * Handle semantics
 * ──────────────────────────────────────────────────────────────────────────
 * Linux  : CH347_HANDLE = int   file-descriptor; CH347_INVALID_HANDLE = -1.
 * Windows: CH347_HANDLE = ULONG device-index  ; CH347_INVALID_HANDLE = ~0u.
 *          CH347OpenDevice() returns a HANDLE internally managed by the DLL,
 *          but every subsequent DLL function identifies the device by its
 *          integer index, not the HANDLE pointer.
 *
 * ──────────────────────────────────────────────────────────────────────────
 * Device path / index convention
 * ──────────────────────────────────────────────────────────────────────────
 * Linux  : strDevice is a filesystem path, e.g. "/dev/ch34xpis0".
 * Windows: strDevice must be a decimal device-index string, e.g. "0" or "1".
 *          The compat open wrapper calls std::strtoul() on the string; a
 *          non-numeric value silently falls back to device index 0.
 *
 * ──────────────────────────────────────────────────────────────────────────
 * Notable limitations of the Windows shims
 * ──────────────────────────────────────────────────────────────────────────
 *  CH347SPI_SetAutoCS  – The Windows DLL has no direct equivalent; autoCS is
 *                        configured via mSpiCfgS::iIsAutoDeativeCS at
 *                        CH347SPI_Init() time.  The shim is a documented no-op.
 *
 *  CH347Jtag_ResetTrst – No equivalent in the Windows DLL.  The shim is a
 *                        documented no-op that always returns true.
 *
 *  CH347Jtag_IoScanT   – The Windows DLL exposes only CH347Jtag_IoScan (no
 *                        isLastPkt parameter).  The shim ignores isLastPkt and
 *                        always exits the Shift state after the call.
 *
 *  CH347GPIO_IRQ_Set   – The Windows DLL uses CH347SetIntRoutine() which maps
 *                        to two independent INT sources rather than per-pin
 *                        callback registration.  The shim routes the requested
 *                        pin to INT0 and disables INT1.  The handler must be
 *                        declared with the Windows CALLBACK calling convention
 *                        (mPCH347_INT_ROUTINE).
 *
 *  Multiple device opens – On Linux each sub-driver (SPI/I2C/GPIO/JTAG) opens
 *                          its own file descriptor.  On Windows all sub-drivers
 *                          share the same device index; the DLL reference-counts
 *                          open/close calls internally.
 */

// ============================================================================
// Shared stdint pull-in (both paths need it before the vendor headers)
// ============================================================================
#include <cstdint>
#include <cstdlib>   // strtoul
#include <cstring>   // strstr

#ifdef _WIN32
// ============================================================================
//  W I N D O W S   P L A T F O R M
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include "CH347DLL.H"



// CH347DLL.H defines min/max as plain 2-argument macros unconditionally
// (only guarded by #ifndef, so #define NOMINMAX does NOT help here).
// These break C++20 STL headers (<span>, <algorithm>, …) which use
// 3-argument overloads and zero-argument static members.
// Purge them immediately so every subsequent standard header is clean.
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

// ────────────────────────────────────────────────────────────────────────────
// Handle type
// ────────────────────────────────────────────────────────────────────────────
using CH347_HANDLE = ULONG;
static constexpr CH347_HANDLE CH347_INVALID_HANDLE = static_cast<CH347_HANDLE>(-1);

// ────────────────────────────────────────────────────────────────────────────
// IRQ type constants – mirror the Linux macro names used throughout the driver
// ────────────────────────────────────────────────────────────────────────────
#ifndef IRQ_TYPE_NONE
#  define IRQ_TYPE_NONE          0u
#  define IRQ_TYPE_EDGE_RISING   1u
#  define IRQ_TYPE_EDGE_FALLING  2u
#  define IRQ_TYPE_EDGE_BOTH     (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#endif

// ────────────────────────────────────────────────────────────────────────────
// Error sentinel macros (Linux ERR_* names used by callers)
// ────────────────────────────────────────────────────────────────────────────
#ifndef ERR_INVAL
#  define ERR_INVAL  (-1)
#  define ERR_RANGE  (-2)
#  define ERR_IOCTL  (-3)
#endif

// ────────────────────────────────────────────────────────────────────────────
// SPI limits (Linux macro names)
// ────────────────────────────────────────────────────────────────────────────
#ifndef CH347_SPI_MAX_FREQ
#  define CH347_SPI_MAX_FREQ 60000000.0
#  define CH347_SPI_MIN_FREQ 218750.0
#endif

// ============================================================================
// Internal helpers – NOT part of the public API
// ============================================================================
namespace ch347_compat_detail {

/// Convert Linux (ignoreCS, iChipSelect) pair into the single Windows
/// iChipSelect ULONG.  Windows bit7=0 → ignore CS; bit7=1 → assert CS.
inline ULONG win_cs(bool ignoreCS, uint8_t iChipSelect) noexcept
{
    return ignoreCS ? 0UL : static_cast<ULONG>(iChipSelect);
}

} // namespace ch347_compat_detail

// ============================================================================
// Open / Close  (path string → device index)
// ============================================================================

/**
 * @brief Open the CH347 device.
 *
 * @param strDevice  Decimal device-index string, e.g. "0".
 *                   Non-numeric values default to index 0.
 * @return Device index on success; CH347_INVALID_HANDLE on failure.
 */
static inline CH347_HANDLE CH347_OpenDevice_Compat(const char *strDevice) noexcept
{
    char   *end  = nullptr;
    ULONG   idx  = 0;
    if (strDevice && *strDevice)
        idx = static_cast<ULONG>(std::strtoul(strDevice, &end, 10));

    HANDLE h = ::CH347OpenDevice(idx);
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
        return CH347_INVALID_HANDLE;
    return idx;
}

static inline bool CH347_CloseDevice_Compat(CH347_HANDLE idx) noexcept
{
    return ::CH347CloseDevice(idx) != FALSE;
}

// Remap the Linux function names used throughout driver code
#define CH347OpenDevice   CH347_OpenDevice_Compat
#define CH347CloseDevice  CH347_CloseDevice_Compat

// ============================================================================
// Timeout
// ============================================================================
/// Maps Linux CH34xSetTimeout → Windows CH347SetTimeout (different name).
static inline bool CH34xSetTimeout(CH347_HANDLE idx,
                                   uint32_t writeMs,
                                   uint32_t readMs) noexcept
{
    return ::CH347SetTimeout(idx,
                             static_cast<ULONG>(writeMs),
                             static_cast<ULONG>(readMs)) != FALSE;
}

// ============================================================================
// Chip information
// ============================================================================
/// Maps Linux CH34x_GetChipVersion → Windows CH347GetVersion (bcdDevice byte).
static inline bool CH34x_GetChipVersion(CH347_HANDLE idx,
                                        uint8_t     *version) noexcept
{
    UCHAR drv = 0, dll = 0, bcd = 0, chip = 0;
    if (!::CH347GetVersion(idx, &drv, &dll, &bcd, &chip))
        return false;
    if (version)
        *version = static_cast<uint8_t>(bcd);
    return true;
}

/**
 * @brief Maps Linux CH34X_GetDeviceID → Windows CH347GetDeviceInfor.
 *
 * Parses "USB\\VID_xxxx&PID_xxxx" from the device info and returns the
 * packed (VID << 16 | PID) value used by Linux callers.
 */
static inline bool CH34X_GetDeviceID(CH347_HANDLE idx,
                                     uint32_t    *id) noexcept
{
    mDeviceInforS info{};
    if (!::CH347GetDeviceInfor(idx, &info))
        return false;
    if (id)
    {
        uint32_t    vid   = 0, pid = 0;
        const char *vid_p = std::strstr(info.DeviceID, "VID_");
        const char *pid_p = std::strstr(info.DeviceID, "PID_");
        if (vid_p) vid = static_cast<uint32_t>(std::strtoul(vid_p + 4, nullptr, 16));
        if (pid_p) pid = static_cast<uint32_t>(std::strtoul(pid_p + 4, nullptr, 16));
        *id = (vid << 16) | pid;
    }
    return true;
}

// ============================================================================
// SPI
// ============================================================================
static inline bool CH347SPI_Init(CH347_HANDLE idx, mSpiCfgS *cfg) noexcept
{
    return ::CH347SPI_Init(idx, cfg) != FALSE;
}

static inline bool CH347SPI_GetCfg(CH347_HANDLE idx, mSpiCfgS *cfg) noexcept
{
    return ::CH347SPI_GetCfg(idx, cfg) != FALSE;
}

static inline bool CH347SPI_SetFrequency(CH347_HANDLE idx,
                                         uint32_t     iHz) noexcept
{
    return ::CH347SPI_SetFrequency(idx, static_cast<ULONG>(iHz)) != FALSE;
}

static inline bool CH347SPI_SetDataBits(CH347_HANDLE idx,
                                        uint8_t      iDataBits) noexcept
{
    return ::CH347SPI_SetDataBits(idx,
                                  static_cast<UCHAR>(iDataBits)) != FALSE;
}

/**
 * @brief AutoCS shim – no-op on Windows.
 *
 * The Windows DLL controls auto chip-select via
 * mSpiCfgS::iIsAutoDeativeCS, set at CH347SPI_Init() time.
 * There is no runtime toggle equivalent to the Linux function.
 * Callers that require dynamic autoCS switching must re-call
 * CH347SPI_Init() with the updated mSpiCfgS on Windows.
 *
 * @return Always true.
 */
static inline bool CH347SPI_SetAutoCS(CH347_HANDLE /*idx*/,
                                      bool         /*disable*/) noexcept
{
    return true; // no-op – see doxygen above
}

static inline bool CH347SPI_ChangeCS(CH347_HANDLE idx,
                                     uint8_t      iStatus) noexcept
{
    return ::CH347SPI_ChangeCS(idx, static_cast<UCHAR>(iStatus)) != FALSE;
}

/**
 * @brief SPI write – bridges Linux (ignoreCS + iChipSelect) to Windows CS packing.
 *
 * Linux separates "ignore CS" from the chip-select value.
 * Windows packs them: iChipSelect bit7 = 0 → ignore; bit7 = 1 → assert.
 */
static inline bool CH347SPI_Write(CH347_HANDLE idx,
                                  bool         ignoreCS,
                                  uint8_t      iChipSelect,
                                  int          iLength,
                                  int          iWriteStep,
                                  void        *ioBuffer) noexcept
{
    return ::CH347SPI_Write(idx,
                            ch347_compat_detail::win_cs(ignoreCS, iChipSelect),
                            static_cast<ULONG>(iLength),
                            static_cast<ULONG>(iWriteStep),
                            ioBuffer) != FALSE;
}

/// @copydoc CH347SPI_Write – full-duplex variant.
static inline bool CH347SPI_WriteRead(CH347_HANDLE idx,
                                      bool         ignoreCS,
                                      uint8_t      iChipSelect,
                                      int          iLength,
                                      void        *ioBuffer) noexcept
{
    return ::CH347SPI_WriteRead(idx,
                                ch347_compat_detail::win_cs(ignoreCS, iChipSelect),
                                static_cast<ULONG>(iLength),
                                ioBuffer) != FALSE;
}

// ============================================================================
// I2C
// ============================================================================
static inline bool CH347I2C_Set(CH347_HANDLE idx, int iMode) noexcept
{
    return ::CH347I2C_Set(idx, static_cast<ULONG>(iMode)) != FALSE;
}

static inline bool CH347I2C_SetStretch(CH347_HANDLE idx, bool enable) noexcept
{
    return ::CH347I2C_SetStretch(idx, enable ? TRUE : FALSE) != FALSE;
}

/**
 * @brief Drive-mode shim – maps Linux CH347I2C_SetDriveMode →
 *        Windows CH347I2C_SetDriverMode (different function name).
 */
static inline bool CH347I2C_SetDriveMode(CH347_HANDLE idx,
                                          uint8_t      mode) noexcept
{
    return ::CH347I2C_SetDriverMode(idx, static_cast<UCHAR>(mode)) != FALSE;
}

static inline bool CH347I2C_SetIgnoreNack(CH347_HANDLE idx,
                                           uint8_t      mode) noexcept
{
    return ::CH347I2C_SetIgnoreNack(idx, static_cast<UCHAR>(mode)) != FALSE;
}

static inline bool CH347I2C_SetDelaymS(CH347_HANDLE idx, int iDelay) noexcept
{
    return ::CH347I2C_SetDelaymS(idx, static_cast<ULONG>(iDelay)) != FALSE;
}

static inline bool CH347I2C_SetAckClk_DelayuS(CH347_HANDLE idx,
                                               int          iDelay) noexcept
{
    return ::CH347I2C_SetAckClk_DelayuS(idx,
                                         static_cast<ULONG>(iDelay)) != FALSE;
}

static inline bool CH347StreamI2C(CH347_HANDLE idx,
                                  int          iWriteLength,
                                  void        *iWriteBuffer,
                                  int          iReadLength,
                                  void        *oReadBuffer) noexcept
{
    return ::CH347StreamI2C(idx,
                            static_cast<ULONG>(iWriteLength), iWriteBuffer,
                            static_cast<ULONG>(iReadLength),  oReadBuffer)
           != FALSE;
}

/**
 * @brief RetAck variant – bridges Linux CH347StreamI2C_RetAck →
 *        Windows CH347StreamI2C_RetACK (different capitalisation).
 */
static inline bool CH347StreamI2C_RetAck(CH347_HANDLE idx,
                                          int          iWriteLength,
                                          void        *iWriteBuffer,
                                          int          iReadLength,
                                          void        *oReadBuffer,
                                          int         *retAck) noexcept
{
    ULONG ack = 0;
    bool  ok  = ::CH347StreamI2C_RetACK(idx,
                                         static_cast<ULONG>(iWriteLength),
                                         iWriteBuffer,
                                         static_cast<ULONG>(iReadLength),
                                         oReadBuffer,
                                         &ack) != FALSE;
    if (retAck)
        *retAck = static_cast<int>(ack);
    return ok;
}

static inline bool CH347ReadEEPROM(CH347_HANDLE idx,
                                   EEPROM_TYPE  iEepromID,
                                   int          iAddr,
                                   int          iLength,
                                   uint8_t     *oBuffer) noexcept
{
    return ::CH347ReadEEPROM(idx, iEepromID,
                             static_cast<ULONG>(iAddr),
                             static_cast<ULONG>(iLength),
                             oBuffer) != FALSE;
}

static inline bool CH347WriteEEPROM(CH347_HANDLE idx,
                                    EEPROM_TYPE  iEepromID,
                                    int          iAddr,
                                    int          iLength,
                                    uint8_t     *iBuffer) noexcept
{
    return ::CH347WriteEEPROM(idx, iEepromID,
                              static_cast<ULONG>(iAddr),
                              static_cast<ULONG>(iLength),
                              iBuffer) != FALSE;
}

// ============================================================================
// GPIO
// ============================================================================
static inline bool CH347GPIO_Get(CH347_HANDLE idx,
                                 uint8_t     *iDir,
                                 uint8_t     *iData) noexcept
{
    UCHAR d = 0, v = 0;
    bool  ok = ::CH347GPIO_Get(idx, &d, &v) != FALSE;
    if (ok) {
        if (iDir)  *iDir  = static_cast<uint8_t>(d);
        if (iData) *iData = static_cast<uint8_t>(v);
    }
    return ok;
}

static inline bool CH347GPIO_Set(CH347_HANDLE idx,
                                 uint8_t      iEnable,
                                 uint8_t      iSetDirOut,
                                 uint8_t      iSetDataOut) noexcept
{
    return ::CH347GPIO_Set(idx,
                           static_cast<UCHAR>(iEnable),
                           static_cast<UCHAR>(iSetDirOut),
                           static_cast<UCHAR>(iSetDataOut)) != FALSE;
}

/**
 * @brief GPIO IRQ shim – bridges Linux CH347GPIO_IRQ_Set → Windows
 *        CH347SetIntRoutine.
 *
 * The Linux driver registers a per-pin ISR with an IRQ-type mask.
 * The Windows DLL supports exactly two independent interrupt sources
 * (INT0, INT1), each mapped to an arbitrary GPIO pin by index.
 *
 * This shim assigns the requested pin to INT0 with the requested edge
 * mode, and permanently disables INT1 (pin index > 7).
 *
 * @param handler  Must point to a function with the Windows CALLBACK
 *                 calling convention, matching the signature:
 *                   void CALLBACK fn(PUCHAR iStatus)
 *                 Cast it through void* at the call site.
 *
 * @note  If two simultaneous IRQ sources are needed on Windows, call
 *        CH347SetIntRoutine() directly with both INT0 and INT1 configured.
 */
static inline bool CH347GPIO_IRQ_Set(CH347_HANDLE idx,
                                     uint8_t      pinIndex,
                                     bool         enable,
                                     uint8_t      irqType,
                                     void        *handler) noexcept
{
    // > 7 disables the interrupt source in the Windows DLL
    const UCHAR Int0Pin  = enable
                               ? static_cast<UCHAR>(pinIndex)
                               : static_cast<UCHAR>(0xFF);
    // irqType: 0=none,1=rising,2=falling,3=both  (same encoding on both OSes)
    const UCHAR Int0Mode = static_cast<UCHAR>(irqType & 0x03u);

    return ::CH347SetIntRoutine(
               idx,
               Int0Pin, Int0Mode,
               0xFF,    0,           // INT1 disabled
               enable ? reinterpret_cast<mPCH347_INT_ROUTINE>(handler)
                      : nullptr)
           != FALSE;
}

// ============================================================================
// JTAG
// ============================================================================
static inline bool CH347Jtag_INIT(CH347_HANDLE idx,
                                   uint8_t      iClockRate) noexcept
{
    return ::CH347Jtag_INIT(idx, static_cast<UCHAR>(iClockRate)) != FALSE;
}

static inline bool CH347Jtag_GetCfg(CH347_HANDLE idx,
                                     uint8_t     *ClockRate) noexcept
{
    UCHAR r  = 0;
    bool  ok = ::CH347Jtag_GetCfg(idx, &r) != FALSE;
    if (ok && ClockRate)
        *ClockRate = static_cast<uint8_t>(r);
    return ok;
}

/**
 * @brief TAP reset shim.
 *
 * The Windows DLL has no CH347Jtag_Reset().  This shim drives the TAP
 * state machine to Test-Logic-Reset (state 0) via CH347Jtag_SwitchTapStateEx.
 *
 * @return 0 on success, -1 on failure (matches Linux int return convention).
 */
static inline int CH347Jtag_Reset(CH347_HANDLE idx) noexcept
{
    return ::CH347Jtag_SwitchTapStateEx(idx, 0) != FALSE ? 0 : -1;
}

/**
 * @brief TRST shim – no-op on Windows.
 *
 * The Windows DLL provides no TRST-pin control function.
 * Hard-reset via TRST is therefore unavailable; use CH347Jtag_Reset()
 * (TMS-based soft reset) instead.
 *
 * @return Always true.
 */
static inline bool CH347Jtag_ResetTrst(CH347_HANDLE /*idx*/,
                                        bool         /*highLevel*/) noexcept
{
    return true; // no-op – see doxygen above
}

/// Maps Linux CH347Jtag_SwitchTapState → Windows CH347Jtag_SwitchTapStateEx.
static inline bool CH347Jtag_SwitchTapState(CH347_HANDLE idx,
                                             uint8_t      tapState) noexcept
{
    return ::CH347Jtag_SwitchTapStateEx(idx,
                                         static_cast<UCHAR>(tapState)) != FALSE;
}

static inline bool CH347Jtag_TmsChange(CH347_HANDLE idx,
                                        uint8_t     *tmsValue,
                                        uint32_t     Step,
                                        uint32_t     Skip) noexcept
{
    return ::CH347Jtag_TmsChange(idx, tmsValue,
                                  static_cast<ULONG>(Step),
                                  static_cast<ULONG>(Skip)) != FALSE;
}

/**
 * @brief IoScanT shim.
 *
 * The Windows DLL only exposes CH347Jtag_IoScan (no isLastPkt parameter).
 * The shim ignores isLastPkt; the Shift state is always exited after the call.
 * For packet-spanning transfers on Windows, use CH347Jtag_ByteWriteDR/IR
 * followed by CH347Jtag_ByteReadDR/IR.
 */
static inline bool CH347Jtag_IoScanT(CH347_HANDLE idx,
                                      uint8_t     *DataBits,
                                      uint32_t     DataBitsNb,
                                      bool         IsRead,
                                      bool         /*IsLastPkt*/) noexcept
{
    return ::CH347Jtag_IoScan(idx, DataBits,
                               static_cast<ULONG>(DataBitsNb),
                               IsRead ? TRUE : FALSE) != FALSE;
}

static inline bool CH347Jtag_ByteWriteDR(CH347_HANDLE idx,
                                          int          iWriteLength,
                                          void        *iWriteBuffer) noexcept
{
    return ::CH347Jtag_ByteWriteDR(idx,
                                    static_cast<ULONG>(iWriteLength),
                                    iWriteBuffer) != FALSE;
}

static inline bool CH347Jtag_ByteReadDR(CH347_HANDLE idx,
                                         uint32_t    *oReadLength,
                                         void        *oReadBuffer) noexcept
{
    ULONG l  = oReadLength ? *oReadLength : 0;
    bool  ok = ::CH347Jtag_ByteReadDR(idx, &l, oReadBuffer) != FALSE;
    if (ok && oReadLength)
        *oReadLength = static_cast<uint32_t>(l);
    return ok;
}

static inline bool CH347Jtag_ByteWriteIR(CH347_HANDLE idx,
                                          int          iWriteLength,
                                          void        *iWriteBuffer) noexcept
{
    return ::CH347Jtag_ByteWriteIR(idx,
                                    static_cast<ULONG>(iWriteLength),
                                    iWriteBuffer) != FALSE;
}

static inline bool CH347Jtag_ByteReadIR(CH347_HANDLE idx,
                                         uint32_t    *oReadLength,
                                         void        *oReadBuffer) noexcept
{
    ULONG l  = oReadLength ? *oReadLength : 0;
    bool  ok = ::CH347Jtag_ByteReadIR(idx, &l, oReadBuffer) != FALSE;
    if (ok && oReadLength)
        *oReadLength = static_cast<uint32_t>(l);
    return ok;
}

static inline bool CH347Jtag_WriteRead(CH347_HANDLE idx,
                                        bool         IsDR,
                                        int          iWriteBitLength,
                                        void        *iWriteBitBuffer,
                                        uint32_t    *oReadBitLength,
                                        void        *oReadBitBuffer) noexcept
{
    ULONG l  = oReadBitLength ? *oReadBitLength : 0;
    bool  ok = ::CH347Jtag_WriteRead(idx,
                                      IsDR ? TRUE : FALSE,
                                      static_cast<ULONG>(iWriteBitLength),
                                      iWriteBitBuffer,
                                      &l, oReadBitBuffer) != FALSE;
    if (ok && oReadBitLength)
        *oReadBitLength = static_cast<uint32_t>(l);
    return ok;
}

static inline bool CH347Jtag_WriteRead_Fast(CH347_HANDLE idx,
                                             bool         IsDR,
                                             int          iWriteLength,
                                             void        *iWriteBuffer,
                                             uint32_t    *oReadLength,
                                             void        *oReadBuffer) noexcept
{
    ULONG l  = oReadLength ? *oReadLength : 0;
    bool  ok = ::CH347Jtag_WriteRead_Fast(idx,
                                           IsDR ? TRUE : FALSE,
                                           static_cast<ULONG>(iWriteLength),
                                           iWriteBuffer,
                                           &l, oReadBuffer) != FALSE;
    if (ok && oReadLength)
        *oReadLength = static_cast<uint32_t>(l);
    return ok;
}

// CH347Jtag_ClockTms / CH347Jtag_IdleClock operate on a local packet buffer
// and take no device handle – signatures are identical on both platforms.

#else // _WIN32
// ============================================================================
//  L I N U X   P L A T F O R M
// ============================================================================

#include "ch347_lib.h"

/// Unified handle type (file descriptor on Linux).
using CH347_HANDLE = int;

/// Sentinel value for an invalid / un-opened handle.
static constexpr CH347_HANDLE CH347_INVALID_HANDLE = -1;

// On Linux the vendor header already exposes every function with the exact
// signatures used by driver code; no wrappers are needed.

#endif // _WIN32

#endif // CH347_COMPAT_H
