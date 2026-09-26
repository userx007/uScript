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
#include <cstdlib> // strtoul
#include <cstring> // strstr

#ifdef _WIN32
// ============================================================================
//  W I N D O W S   P L A T F O R M
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CH347DLL.H"

#include <windows.h>

// CH347DLL.H defines min/max as plain 2-argument macros unconditionally
// (only guarded by #ifndef, so #define NOMINMAX does NOT help here).
// These break C++20 STL headers (<span>, <algorithm>, …) which use
// 3-argument overloads and zero-argument static members.
// Purge them immediately so every subsequent standard header is clean.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// ────────────────────────────────────────────────────────────────────────────
// Handle type
// ────────────────────────────────────────────────────────────────────────────
using CH347_HANDLE                                 = ULONG;
static constexpr CH347_HANDLE CH347_INVALID_HANDLE = static_cast<CH347_HANDLE>(-1);

// ────────────────────────────────────────────────────────────────────────────
// IRQ type constants – mirror the Linux macro names used throughout the driver
// ────────────────────────────────────────────────────────────────────────────
#ifndef IRQ_TYPE_NONE
#define IRQ_TYPE_NONE         0u
#define IRQ_TYPE_EDGE_RISING  1u
#define IRQ_TYPE_EDGE_FALLING 2u
#define IRQ_TYPE_EDGE_BOTH    (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#endif

// ────────────────────────────────────────────────────────────────────────────
// Error sentinel macros (Linux ERR_* names used by callers)
// ────────────────────────────────────────────────────────────────────────────
#ifndef ERR_INVAL
#define ERR_INVAL (-1)
#define ERR_RANGE (-2)
#define ERR_IOCTL (-3)
#endif

// ────────────────────────────────────────────────────────────────────────────
// SPI limits (Linux macro names)
// ────────────────────────────────────────────────────────────────────────────
#ifndef CH347_SPI_MAX_FREQ
#define CH347_SPI_MAX_FREQ 60000000.0
#define CH347_SPI_MIN_FREQ 218750.0
#endif

// ============================================================================
// Internal helpers – NOT part of the public API
// ============================================================================
namespace ch347_compat_detail {

    /// Convert Linux (ignoreCS, iChipSelect) pair into the single Windows
    /// iChipSelect ULONG.  Windows bit7=0 → ignore CS; bit7=1 → assert CS.
    inline ULONG win_cs(bool bIgnoreCS, uint8_t u8ChipSelect) noexcept
    {
        return bIgnoreCS ? 0UL : static_cast<ULONG>(u8ChipSelect);
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
static inline CH347_HANDLE CH347_OpenDevice_Compat(const char *pstrDevice) noexcept
{
    char *end = nullptr;
    ULONG idx = 0;
    if (pstrDevice && *pstrDevice) {
        idx = static_cast<ULONG>(std::strtoul(pstrDevice, &end, 10));
    }

    HANDLE h = ::CH347OpenDevice(idx);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) {
        return CH347_INVALID_HANDLE;
    }
    return idx;
}

static inline bool CH347_CloseDevice_Compat(CH347_HANDLE idx) noexcept
{
    return ::CH347CloseDevice(idx) != FALSE;
}

// Remap the Linux function names used throughout driver code
#define CH347OpenDevice  CH347_OpenDevice_Compat
#define CH347CloseDevice CH347_CloseDevice_Compat

// ============================================================================
// Timeout
// ============================================================================
/// Maps Linux CH34xSetTimeout → Windows CH347SetTimeout (different name).
static inline bool CH34xSetTimeout(CH347_HANDLE idx,
                                   uint32_t u32WriteMs,
                                   uint32_t u32ReadMs) noexcept
{
    return ::CH347SetTimeout(idx,
                             static_cast<ULONG>(u32WriteMs),
                             static_cast<ULONG>(u32ReadMs)) != FALSE;
}

// ============================================================================
// Chip information
// ============================================================================
/// Maps Linux CH34x_GetChipVersion → Windows CH347GetVersion (bcdDevice byte).
static inline bool CH34x_GetChipVersion(CH347_HANDLE idx,
                                        uint8_t *pu8Version) noexcept
{
    UCHAR drv = 0, dll = 0, bcd = 0, chip = 0;
    if (!::CH347GetVersion(idx, &drv, &dll, &bcd, &chip)) {
        return false;
    }
    if (pu8Version) {
        *pu8Version = static_cast<uint8_t>(bcd);
    }
    return true;
}

/**
 * @brief Maps Linux CH34X_GetDeviceID → Windows CH347GetDeviceInfor.
 *
 * Parses "USB\\VID_xxxx&PID_xxxx" from the device info and returns the
 * packed (VID << 16 | PID) value used by Linux callers.
 */
static inline bool CH34X_GetDeviceID(CH347_HANDLE idx,
                                     uint32_t *pu32Id) noexcept
{
    mDeviceInforS info{};
    if (!::CH347GetDeviceInfor(idx, &info)) {
        return false;
    }
    if (pu32Id) {
        uint32_t vid = 0, pid = 0;
        const char *vid_p = std::strstr(info.DeviceID, "VID_");
        const char *pid_p = std::strstr(info.DeviceID, "PID_");
        if (vid_p) {
            vid = static_cast<uint32_t>(std::strtoul(vid_p + 4, nullptr, 16));
        }
        if (pid_p) {
            pid = static_cast<uint32_t>(std::strtoul(pid_p + 4, nullptr, 16));
        }
        *pu32Id = (vid << 16) | pid;
    }
    return true;
}

// ============================================================================
// SPI
// ============================================================================
// NOTE: The shims below are renamed to avoid "ambiguating new declaration"
// errors.  The DLL declares these with BOOL/ULONG; our shims use bool/uint*
// with CH347_HANDLE (= ULONG) as the first parameter, which makes the
// parameter-type sets identical and the differing return types ambiguous to
// the C++ overload resolver.  The pattern: define a _Compat wrapper FIRST
// (the DLL name is still unmasked at that point), then #define the public
// name to the wrapper so all downstream callers transparently use it.
static inline bool CH347SPI_Init_Compat(CH347_HANDLE idx, mSpiCfgS *pCfg) noexcept
{
    return ::CH347SPI_Init(idx, pCfg) != FALSE;
}

#define CH347SPI_Init CH347SPI_Init_Compat

static inline bool CH347SPI_GetCfg_Compat(CH347_HANDLE idx, mSpiCfgS *pCfg) noexcept
{
    return ::CH347SPI_GetCfg(idx, pCfg) != FALSE;
}

#define CH347SPI_GetCfg CH347SPI_GetCfg_Compat

static inline bool CH347SPI_SetFrequency_Compat(CH347_HANDLE idx,
                                                uint32_t u32Hz) noexcept
{
    return ::CH347SPI_SetFrequency(idx, static_cast<ULONG>(u32Hz)) != FALSE;
}

#define CH347SPI_SetFrequency CH347SPI_SetFrequency_Compat

static inline bool CH347SPI_SetDataBits_Compat(CH347_HANDLE idx,
                                               uint8_t u8DataBits) noexcept
{
    return ::CH347SPI_SetDataBits(idx,
                                  static_cast<UCHAR>(u8DataBits)) != FALSE;
}

#define CH347SPI_SetDataBits CH347SPI_SetDataBits_Compat

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
                                      bool /*disable*/) noexcept
{
    return true; // no-op – see doxygen above
}

static inline bool CH347SPI_ChangeCS_Compat(CH347_HANDLE idx,
                                            uint8_t u8Status) noexcept
{
    return ::CH347SPI_ChangeCS(idx, static_cast<UCHAR>(u8Status)) != FALSE;
}

#define CH347SPI_ChangeCS CH347SPI_ChangeCS_Compat

/**
 * @brief SPI write – bridges Linux (ignoreCS + iChipSelect) to Windows CS packing.
 *
 * Linux separates "ignore CS" from the chip-select value.
 * Windows packs them: iChipSelect bit7 = 0 → ignore; bit7 = 1 → assert.
 */
static inline bool CH347SPI_Write(CH347_HANDLE idx,
                                  bool bIgnoreCS,
                                  uint8_t u8ChipSelect,
                                  int iLength,
                                  int iWriteStep,
                                  void *pvIoBuffer) noexcept
{
    return ::CH347SPI_Write(idx,
                            ch347_compat_detail::win_cs(bIgnoreCS, u8ChipSelect),
                            static_cast<ULONG>(iLength),
                            static_cast<ULONG>(iWriteStep),
                            pvIoBuffer) != FALSE;
}

/// @copydoc CH347SPI_Write – full-duplex variant.
static inline bool CH347SPI_WriteRead(CH347_HANDLE idx,
                                      bool bIgnoreCS,
                                      uint8_t u8ChipSelect,
                                      int iLength,
                                      void *pvIoBuffer) noexcept
{
    return ::CH347SPI_WriteRead(idx,
                                ch347_compat_detail::win_cs(bIgnoreCS, u8ChipSelect),
                                static_cast<ULONG>(iLength),
                                pvIoBuffer) != FALSE;
}

// ============================================================================
// I2C
// ============================================================================
static inline bool CH347I2C_Set(CH347_HANDLE idx, int iMode) noexcept
{
    return ::CH347I2C_Set(idx, static_cast<ULONG>(iMode)) != FALSE;
}

static inline bool CH347I2C_SetStretch(CH347_HANDLE idx, bool bEnable) noexcept
{
    return ::CH347I2C_SetStretch(idx, bEnable ? TRUE : FALSE) != FALSE;
}

/**
 * @brief Drive-mode shim – maps Linux CH347I2C_SetDriveMode →
 *        Windows CH347I2C_SetDriverMode (different function name).
 */
static inline bool CH347I2C_SetDriveMode(CH347_HANDLE idx,
                                         uint8_t u8Mode) noexcept
{
    return ::CH347I2C_SetDriverMode(idx, static_cast<UCHAR>(u8Mode)) != FALSE;
}

static inline bool CH347I2C_SetIgnoreNack_Compat(CH347_HANDLE idx,
                                                 uint8_t u8Mode) noexcept
{
    return ::CH347I2C_SetIgnoreNack(idx, static_cast<UCHAR>(u8Mode)) != FALSE;
}

#define CH347I2C_SetIgnoreNack CH347I2C_SetIgnoreNack_Compat

static inline bool CH347I2C_SetDelaymS(CH347_HANDLE idx, int iDelay) noexcept
{
    return ::CH347I2C_SetDelaymS(idx, static_cast<ULONG>(iDelay)) != FALSE;
}

static inline bool CH347I2C_SetAckClk_DelayuS(CH347_HANDLE idx,
                                              int iDelay) noexcept
{
    return ::CH347I2C_SetAckClk_DelayuS(idx,
                                        static_cast<ULONG>(iDelay)) != FALSE;
}

static inline bool CH347StreamI2C(CH347_HANDLE idx,
                                  int iWriteLength,
                                  void *pvWriteBuffer,
                                  int iReadLength,
                                  void *pvOReadBuffer) noexcept
{
    return ::CH347StreamI2C(idx,
                            static_cast<ULONG>(iWriteLength), pvWriteBuffer,
                            static_cast<ULONG>(iReadLength), pvOReadBuffer) != FALSE;
}

/**
 * @brief RetAck variant – bridges Linux CH347StreamI2C_RetAck →
 *        Windows CH347StreamI2C_RetACK (different capitalisation).
 */
static inline bool CH347StreamI2C_RetAck(CH347_HANDLE idx,
                                         int iWriteLength,
                                         void *pvWriteBuffer,
                                         int iReadLength,
                                         void *pvOReadBuffer,
                                         int *pRetAck) noexcept
{
    ULONG ack = 0;
    bool ok   = ::CH347StreamI2C_RetACK(idx,
                                        static_cast<ULONG>(iWriteLength),
                                        pvWriteBuffer,
                                        static_cast<ULONG>(iReadLength),
                                        pvOReadBuffer,
                                        &ack) != FALSE;
    if (pRetAck) {
        *pRetAck = static_cast<int>(ack);
    }
    return ok;
}

static inline bool CH347ReadEEPROM(CH347_HANDLE idx,
                                   EEPROM_TYPE iEepromID,
                                   int iAddr,
                                   int iLength,
                                   uint8_t *pu8OBuffer) noexcept
{
    return ::CH347ReadEEPROM(idx, iEepromID,
                             static_cast<ULONG>(iAddr),
                             static_cast<ULONG>(iLength),
                             pu8OBuffer) != FALSE;
}

static inline bool CH347WriteEEPROM(CH347_HANDLE idx,
                                    EEPROM_TYPE iEepromID,
                                    int iAddr,
                                    int iLength,
                                    uint8_t *pu8Buffer) noexcept
{
    return ::CH347WriteEEPROM(idx, iEepromID,
                              static_cast<ULONG>(iAddr),
                              static_cast<ULONG>(iLength),
                              pu8Buffer) != FALSE;
}

// ============================================================================
// GPIO
// ============================================================================
static inline bool CH347GPIO_Get_Compat(CH347_HANDLE idx,
                                        uint8_t *pu8Dir,
                                        uint8_t *pu8Data) noexcept
{
    UCHAR d = 0, v = 0;
    bool ok = ::CH347GPIO_Get(idx, &d, &v) != FALSE;
    if (ok) {
        if (pu8Dir) {
            *pu8Dir = static_cast<uint8_t>(d);
        }
        if (pu8Data) {
            *pu8Data = static_cast<uint8_t>(v);
        }
    }
    return ok;
}

#define CH347GPIO_Get CH347GPIO_Get_Compat

static inline bool CH347GPIO_Set_Compat(CH347_HANDLE idx,
                                        uint8_t u8Enable,
                                        uint8_t u8SetDirOut,
                                        uint8_t u8SetDataOut) noexcept
{
    return ::CH347GPIO_Set(idx,
                           static_cast<UCHAR>(u8Enable),
                           static_cast<UCHAR>(u8SetDirOut),
                           static_cast<UCHAR>(u8SetDataOut)) != FALSE;
}

#define CH347GPIO_Set CH347GPIO_Set_Compat

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
                                     uint8_t u8PinIndex,
                                     bool bEnable,
                                     uint8_t u8IrqType,
                                     void *pvHandler) noexcept
{
    // > 7 disables the interrupt source in the Windows DLL
    const UCHAR Int0Pin  = bEnable
                               ? static_cast<UCHAR>(u8PinIndex)
                               : static_cast<UCHAR>(0xFF);
    // u8IrqType: 0=none,1=rising,2=falling,3=both  (same encoding on both OSes)
    const UCHAR Int0Mode = static_cast<UCHAR>(u8IrqType & 0x03u);

    return ::CH347SetIntRoutine(
               idx,
               Int0Pin, Int0Mode,
               0xFF, 0, // INT1 disabled
               bEnable ? reinterpret_cast<mPCH347_INT_ROUTINE>(pvHandler)
                      : nullptr) != FALSE;
}

// ============================================================================
// JTAG – packet-buffer utility functions (no device handle)
// ============================================================================
// On Linux these are exported by libch347.so.  The Windows DLL has no
// equivalent, so they are implemented here by replaying the logic recovered
// from disassembly of the Linux shared library.
//
// Protocol byte format (CH347 JTAG bit-bang):
//   bit 0 : TCK
//   bit 1 : TMS
//   bit 4 : protocol framing flag (always set by the library)
//   other : TDI / persistent pin state held in CH347_JtagPinState.base_pins
//
// CH347_JtagPinState mirrors the library-internal JtatPinSta global (16 B):
//   [0]  tms_state : last TMS byte written (0x00 or 0x02)
//   [4]  tck_state : last TCK phase marker  (0x00 or 0x10)
//   [8]  flag      : set to 1 after CH347Jtag_ClockTms
//   [12] base_pins : persistent pin-state byte (TDI etc.)
struct CH347_JtagPinState_t {
        uint32_t tms_state = 0;
        uint32_t tck_state = 0;
        uint32_t flag      = 0;
        uint32_t base_pins = 0;
};

inline CH347_JtagPinState_t CH347_JtagPinState{};

/// Change TMS on the rising edge of TCK to shift the TAP state machine.
/// Appends two bytes (TCK-low then TCK-high) to BitBangPkt[BI..BI+1].
/// @return Updated byte index (BI + 2).
static inline uint32_t CH347Jtag_ClockTms(uint8_t *pu8BitBangPkt,
                                          uint32_t u32Tms,
                                          uint32_t u32BI) noexcept
{
    const uint8_t tms_bit        = (u32Tms == 1u) ? 0x02u : 0x00u;
    const uint8_t base           = static_cast<uint8_t>(CH347_JtagPinState.base_pins);
    pu8BitBangPkt[u32BI++]             = (base | tms_bit) | 0x10u; // TCK low  (bit4 = framing)
    pu8BitBangPkt[u32BI++]             = (base | tms_bit) | 0x11u; // TCK high (bit0 = TCK, bit4)
    CH347_JtagPinState.tms_state = tms_bit;
    CH347_JtagPinState.tck_state = 0x10u;
    CH347_JtagPinState.flag      = 1u;
    return u32BI;
}

/// Ensure TCK is left low after a sequence; appends one idle byte.
/// @return Updated byte index (BI + 1).
static inline uint32_t CH347Jtag_IdleClock(uint8_t *pu8BitBangPkt,
                                           uint32_t u32BI) noexcept
{
    // Reconstruct the idle pin byte from saved state (mirrors Linux logic).
    const uint8_t tms_part = (CH347_JtagPinState.tms_state != 0u)
                                 ? 0x02u
                                 : static_cast<uint8_t>(CH347_JtagPinState.base_pins);
    const uint8_t tck_part = (CH347_JtagPinState.tck_state != 0u)
                                 ? 0x10u
                                 : static_cast<uint8_t>(CH347_JtagPinState.base_pins);
    pu8BitBangPkt[u32BI++]       = tms_part | tck_part;
    return u32BI;
}

// ============================================================================
// JTAG – device-handle functions (shims bridging Linux↔Windows API)
// ============================================================================
// Same _Compat + #define pattern as SPI/GPIO above – see note there.
static inline bool CH347Jtag_INIT_Compat(CH347_HANDLE idx,
                                         uint8_t u8ClockRate) noexcept
{
    return ::CH347Jtag_INIT(idx, static_cast<UCHAR>(u8ClockRate)) != FALSE;
}

#define CH347Jtag_INIT CH347Jtag_INIT_Compat

static inline bool CH347Jtag_GetCfg_Compat(CH347_HANDLE idx,
                                           uint8_t *pu8ClockRate) noexcept
{
    UCHAR r = 0;
    bool ok = ::CH347Jtag_GetCfg(idx, &r) != FALSE;
    if (ok && pu8ClockRate) {
        *pu8ClockRate = static_cast<uint8_t>(r);
    }
    return ok;
}

#define CH347Jtag_GetCfg CH347Jtag_GetCfg_Compat

/**
 * @brief TAP reset shim.
 *
 * The Windows DLL has no CH347Jtag_Reset().  This shim drives the TAP
 * state machine to Test-Logic-Reset (state 0) via CH347Jtag_SwitchTapStateEx.
 *
 * @return 0 on success, -1 on failure (matches Linux int return convention).
 */
static inline int CH347Jtag_Reset_Compat(CH347_HANDLE idx) noexcept
{
    return ::CH347Jtag_SwitchTapStateEx(idx, 0) != FALSE ? 0 : -1;
}

#define CH347Jtag_Reset CH347Jtag_Reset_Compat

/**
 * @brief TRST shim.
 *
 * The Windows DLL does provide CH347Jtag_ResetTrst(ULONG, BOOL), but its
 * BOOL parameter type would create an ambiguating overload against our
 * bool-parameter shim.  Wrap and rename following the standard pattern.
 */
static inline bool CH347Jtag_ResetTrst_Compat(CH347_HANDLE idx,
                                              bool bHighLevel) noexcept
{
    return ::CH347Jtag_ResetTrst(idx, bHighLevel ? TRUE : FALSE) != FALSE;
}

#define CH347Jtag_ResetTrst CH347Jtag_ResetTrst_Compat

/// Maps Linux CH347Jtag_SwitchTapState → Windows CH347Jtag_SwitchTapStateEx.
/// (The Windows DLL also exports a no-index CH347Jtag_SwitchTapState(UCHAR)
/// which has a different parameter count – no conflict, but keep explicit.)
static inline bool CH347Jtag_SwitchTapState(CH347_HANDLE idx,
                                            uint8_t u8TapState) noexcept
{
    return ::CH347Jtag_SwitchTapStateEx(idx,
                                        static_cast<UCHAR>(u8TapState)) != FALSE;
}

static inline bool CH347Jtag_TmsChange_Compat(CH347_HANDLE idx,
                                              uint8_t *pu8TmsValue,
                                              uint32_t u32Step,
                                              uint32_t u32Skip) noexcept
{
    return ::CH347Jtag_TmsChange(idx, pu8TmsValue,
                                 static_cast<ULONG>(u32Step),
                                 static_cast<ULONG>(u32Skip)) != FALSE;
}

#define CH347Jtag_TmsChange CH347Jtag_TmsChange_Compat

/**
 * @brief IoScanT shim.
 *
 * The Windows DLL exposes the full CH347Jtag_IoScanT (with isLastPkt),
 * but its BOOL parameter type would create an ambiguating overload.
 * The shim translates bool→BOOL and delegates directly.
 */
static inline bool CH347Jtag_IoScanT_Compat(CH347_HANDLE idx,
                                            uint8_t *pu8DataBits,
                                            uint32_t u32DataBitsNb,
                                            bool bIsRead,
                                            bool bIsLastPkt) noexcept
{
    return ::CH347Jtag_IoScanT(idx, pu8DataBits,
                               static_cast<ULONG>(u32DataBitsNb),
                               bIsRead ? TRUE : FALSE,
                               bIsLastPkt ? TRUE : FALSE) != FALSE;
}

#define CH347Jtag_IoScanT CH347Jtag_IoScanT_Compat

static inline bool CH347Jtag_ByteWriteDR_Compat(CH347_HANDLE idx,
                                                int iWriteLength,
                                                void *pvWriteBuffer) noexcept
{
    return ::CH347Jtag_ByteWriteDR(idx,
                                   static_cast<ULONG>(iWriteLength),
                                   pvWriteBuffer) != FALSE;
}

#define CH347Jtag_ByteWriteDR CH347Jtag_ByteWriteDR_Compat

static inline bool CH347Jtag_ByteReadDR_Compat(CH347_HANDLE idx,
                                               uint32_t *pu32OReadLength,
                                               void *pvOReadBuffer) noexcept
{
    ULONG l = pu32OReadLength ? *pu32OReadLength : 0;
    bool ok = ::CH347Jtag_ByteReadDR(idx, &l, pvOReadBuffer) != FALSE;
    if (ok && pu32OReadLength) {
        *pu32OReadLength = static_cast<uint32_t>(l);
    }
    return ok;
}

#define CH347Jtag_ByteReadDR CH347Jtag_ByteReadDR_Compat

static inline bool CH347Jtag_ByteWriteIR_Compat(CH347_HANDLE idx,
                                                int iWriteLength,
                                                void *pvWriteBuffer) noexcept
{
    return ::CH347Jtag_ByteWriteIR(idx,
                                   static_cast<ULONG>(iWriteLength),
                                   pvWriteBuffer) != FALSE;
}

#define CH347Jtag_ByteWriteIR CH347Jtag_ByteWriteIR_Compat

static inline bool CH347Jtag_ByteReadIR_Compat(CH347_HANDLE idx,
                                               uint32_t *pu32OReadLength,
                                               void *pvOReadBuffer) noexcept
{
    ULONG l = pu32OReadLength ? *pu32OReadLength : 0;
    bool ok = ::CH347Jtag_ByteReadIR(idx, &l, pvOReadBuffer) != FALSE;
    if (ok && pu32OReadLength) {
        *pu32OReadLength = static_cast<uint32_t>(l);
    }
    return ok;
}

#define CH347Jtag_ByteReadIR CH347Jtag_ByteReadIR_Compat

static inline bool CH347Jtag_WriteRead_Compat(CH347_HANDLE idx,
                                              bool bIsDR,
                                              int iWriteBitLength,
                                              void *pvWriteBitBuffer,
                                              uint32_t *pu32OReadBitLength,
                                              void *pvOReadBitBuffer) noexcept
{
    ULONG l = pu32OReadBitLength ? *pu32OReadBitLength : 0;
    bool ok = ::CH347Jtag_WriteRead(idx,
                                    bIsDR ? TRUE : FALSE,
                                    static_cast<ULONG>(iWriteBitLength),
                                    pvWriteBitBuffer,
                                    &l, pvOReadBitBuffer) != FALSE;
    if (ok && pu32OReadBitLength) {
        *pu32OReadBitLength = static_cast<uint32_t>(l);
    }
    return ok;
}

#define CH347Jtag_WriteRead CH347Jtag_WriteRead_Compat

static inline bool CH347Jtag_WriteRead_Fast_Compat(CH347_HANDLE idx,
                                                   bool bIsDR,
                                                   int iWriteLength,
                                                   void *pvWriteBuffer,
                                                   uint32_t *pu32OReadLength,
                                                   void *pvOReadBuffer) noexcept
{
    ULONG l = pu32OReadLength ? *pu32OReadLength : 0;
    bool ok = ::CH347Jtag_WriteRead_Fast(idx,
                                         bIsDR ? TRUE : FALSE,
                                         static_cast<ULONG>(iWriteLength),
                                         pvWriteBuffer,
                                         &l, pvOReadBuffer) != FALSE;
    if (ok && pu32OReadLength) {
        *pu32OReadLength = static_cast<uint32_t>(l);
    }
    return ok;
}

#define CH347Jtag_WriteRead_Fast CH347Jtag_WriteRead_Fast_Compat

// CH347Jtag_ClockTms / CH347Jtag_IdleClock operate on a local packet buffer
// and take no device handle – implemented above as inline functions for Windows.

#else // _WIN32
// ============================================================================
//  L I N U X   P L A T F O R M
// ============================================================================

#include "ch347_lib.h"

/// Unified handle type (file descriptor on Linux).
using CH347_HANDLE                                 = int;

/// Sentinel value for an invalid / un-opened handle.
static constexpr CH347_HANDLE CH347_INVALID_HANDLE = -1;

// On Linux the vendor header already exposes every function with the exact
// signatures used by driver code; no wrappers are needed.

#endif // _WIN32

#endif // CH347_COMPAT_H
