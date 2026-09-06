#ifndef VXLAPI_COMPAT_H
#define VXLAPI_COMPAT_H

/*
 * ---------------------------------------------------------------------------
 * NOT VECTOR'S HEADER.
 * ---------------------------------------------------------------------------
 * This is a minimal, independently-written compatibility declaration for the
 * small subset of the Vector XL Driver Library (XL-API) entry points that
 * uVector.cpp calls. It exists purely so this repository's build doesn't
 * hard-depend on the official Vector XL Driver Library SDK being installed.
 *
 * It is NOT a copy of, and is not derived from, vxlapi.h as shipped by
 * Vector Informatik GmbH — that file is proprietary and this project has no
 * rights to redistribute it. If you have the official "Vector XL Driver
 * Library" SDK installed (via the Vector Driver Setup package), point this
 * component's CMakeLists.txt at its real vxlapi.h/vxlapi64.lib/vxlapi64.dll
 * instead of using this stub — see VECTOR_XLAPI_INCLUDE_DIR / VECTOR_XLAPI_LIB_DIR.
 *
 * Struct layouts and constants below were cross-checked field-by-field
 * against a real vxlapi.h supplied separately (type widths, member order,
 * and struct sizes only — no text, comments, or file structure copied) and
 * must binary-match the real DLL's ABI to link correctly. Still, prefer the
 * official header over this stub whenever you have it — see
 * VECTOR_XLAPI_INCLUDE_DIR below — since any SDK version drift (new fields,
 * reordered reserved bytes, etc.) won't be reflected here automatically.
 * ---------------------------------------------------------------------------
 */

#include <windows.h>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------
typedef int16_t   XLstatus;
typedef int32_t   XLportHandle;   // Windows "long" is 32-bit even in 64-bit builds (LLP64)
typedef uint64_t  XLaccess;
typedef uint32_t  XLulong;

#define XL_INVALID_PORTHANDLE   ((XLportHandle)(-1))

// ---------------------------------------------------------------------------
// Status codes (subset)
// ---------------------------------------------------------------------------
#define XL_SUCCESS              ((XLstatus)0)
#define XL_ERR_QUEUE_IS_EMPTY   ((XLstatus)10)
#define XL_ERR_INVALID_ACCESS   ((XLstatus)3)
#define XL_ERR_PORT_IS_OFFLINE  ((XLstatus)13)

// ---------------------------------------------------------------------------
// Bus types / activation flags
// ---------------------------------------------------------------------------
#define XL_BUS_TYPE_CAN         0x00000001UL
#define XL_ACTIVATE_RESET_CLOCK 8
#define XL_INTERFACE_VERSION_V3 3

// ---------------------------------------------------------------------------
// Event tags (subset)
// ---------------------------------------------------------------------------
#define XL_RECEIVE_MSG   1
#define XL_CHIP_STATE    4
#define XL_TRANSMIT_MSG  10

// ---------------------------------------------------------------------------
// CAN message flags
// ---------------------------------------------------------------------------
#define XL_CAN_EXT_MSG_ID        0x80000000UL   // extended (29-bit) id flag folded into XLcanMsg::id
#define XL_CAN_MSG_FLAG_ERROR_FRAME   0x01U
#define XL_CAN_MSG_FLAG_OVERRUN       0x02U
#define XL_CAN_MSG_FLAG_NERR          0x04U     // line error on lowspeed
#define XL_CAN_MSG_FLAG_WAKEUP        0x08U
#define XL_CAN_MSG_FLAG_REMOTE_FRAME  0x10U
#define XL_CAN_MSG_FLAG_TX_COMPLETED  0x40U     // this is OUR OWN transmitted frame, looped back
                                                 // through xlReceive() as a receive-queue event -
                                                 // see uVector.cpp's frame-flag filtering.
#define XL_CAN_MSG_FLAG_TX_REQUEST    0x80U

// Deliberately NOT #pragma pack(1): the real header uses natural alignment
// throughout, relying on careful field ordering (and explicit reserved
// bytes) to reach its documented struct sizes. XLcanMsg/XLevent below land
// on the same 32/48-byte sizes either way since they're already gap-free
// under natural alignment - so no #pragma pack is needed or used here,
// matching the original.

typedef struct {
    uint32_t id;
    uint16_t flags;
    uint16_t dlc;
    uint64_t res1;
    uint8_t  data[8];
    uint64_t res2;
} XLcanMsg;

typedef struct {
    uint8_t  tag;          // XLeventTag: 1 byte
    uint8_t  chanIndex;    // 1 byte
    uint16_t transId;      // 2 bytes
    uint16_t portHandle;   // 2 bytes, internal use only
    uint8_t  flags;        // 1 byte (e.g. XL_EVENT_FLAG_OVERRUN)
    uint8_t  reserved;     // 1 byte
    uint64_t timeStamp;    // 8 bytes
    union {
        XLcanMsg msg;
        uint8_t  raw[32];
    } tagData;             // 32 bytes
} XLevent;                 // 48 bytes total

// ---------------------------------------------------------------------------
// Hardware type identifiers (XL_HWTYPE_*) — subset covering the CAN-capable
// devices this plugin's device-selection feature (hw=/DEVICES) recognises by
// name. Not exhaustive; extend as needed. Values are Vector's own assigned
// constants (functional identifiers, not creative content).
// ---------------------------------------------------------------------------
#define XL_HWTYPE_NONE          0
#define XL_HWTYPE_VIRTUAL       1
#define XL_HWTYPE_CANCARDX      2
#define XL_HWTYPE_CANCARDY      12
#define XL_HWTYPE_CANCARDXL     15
#define XL_HWTYPE_CANCASEXL     21
#define XL_HWTYPE_CANBOARDXL    25
#define XL_HWTYPE_VN8900        45
#define XL_HWTYPE_VN8950        47
#define XL_HWTYPE_VN1610        55
#define XL_HWTYPE_VN1630        57
#define XL_HWTYPE_VN1640        59
#define XL_HWTYPE_VN8970        61
#define XL_HWTYPE_VN1611        63
#define XL_HWTYPE_VN5610        65
#define XL_HWTYPE_VN7570        67
#define XL_HWTYPE_VX1121        73
#define XL_HWTYPE_VX1131        75
#define XL_HWTYPE_VN7610        81
#define XL_HWTYPE_VN7572        83
#define XL_HWTYPE_VN8972        85
#define XL_HWTYPE_VX0312        91
#define XL_HWTYPE_VN8800        95
#define XL_HWTYPE_VN5610A       101
#define XL_HWTYPE_VN7640        102

// ---------------------------------------------------------------------------
// Bus capability flags (subset — CAN only)
// ---------------------------------------------------------------------------
#define XL_BUS_COMPATIBLE_CAN   XL_BUS_TYPE_CAN
#define XL_BUS_ACTIVE_CAP_CAN   (XL_BUS_COMPATIBLE_CAN << 16)

// ---------------------------------------------------------------------------
// xlGetDriverConfig() — channel enumeration
// ---------------------------------------------------------------------------
#define XL_MAX_LENGTH            31
#define XL_CONFIG_MAX_CHANNELS   64

// Same field layout/order as the real XLbusParams so offsets of the fields
// declared after it in XLchannelConfig (serialNumber, etc.) line up.
typedef struct {
    uint32_t busType;
    union {
        struct {
            uint32_t bitRate;
            uint8_t  sjw, tseg1, tseg2, sam, outputMode;
            uint8_t  reserved[7];
            uint8_t  canOpMode;
        } can;
        uint8_t raw[28];
    } data;
} XLbusParams;

typedef struct {
    char        name[XL_MAX_LENGTH + 1];
    uint8_t     hwType;
    uint8_t     hwIndex;
    uint8_t     hwChannel;
    uint16_t    transceiverType;
    uint16_t    transceiverState;
    uint16_t    configError;
    uint8_t     channelIndex;
    uint64_t    channelMask;
    uint32_t    channelCapabilities;
    uint32_t    channelBusCapabilities;
    uint8_t     isOnBus;
    uint32_t    connectedBusType;
    XLbusParams busParams;
    uint32_t    _doNotUse;
    uint32_t    driverVersion;
    uint32_t    interfaceVersion;
    uint32_t    raw_data[10];
    uint32_t    serialNumber;
    uint32_t    articleNumber;
    char        transceiverName[XL_MAX_LENGTH + 1];
    uint32_t    specialCabFlags;
    uint32_t    dominantTimeout;
    uint8_t     dominantRecessiveDelay;
    uint8_t     recessiveDominantDelay;
    uint8_t     connectionInfo;
    uint8_t     currentlyAvailableTimestamps;
    uint16_t    minimalSupplyVoltage;
    uint16_t    maximalSupplyVoltage;
    uint32_t    maximalBaudrate;
    uint8_t     fpgaCoreCapabilities;
    uint8_t     specialDeviceStatus;
    uint16_t    channelBusActiveCapabilities;
    uint16_t    breakOffset;
    uint16_t    delimiterOffset;
    uint32_t    reserved[3];
} XLchannelConfig;

typedef struct {
    uint32_t        dllVersion;
    uint32_t        channelCount;
    uint32_t        reserved[10];
    XLchannelConfig channel[XL_CONFIG_MAX_CHANNELS];
} XLdriverConfig;

// ---------------------------------------------------------------------------
// Driver / port lifecycle
// ---------------------------------------------------------------------------
XLstatus __stdcall xlOpenDriver(void);
XLstatus __stdcall xlCloseDriver(void);

XLstatus __stdcall xlGetApplConfig(char* appName, unsigned int appChannel,
                                    unsigned int* pHwType, unsigned int* pHwIndex,
                                    unsigned int* pHwChannel, unsigned int busType);

XLaccess __stdcall xlGetChannelMask(int hwType, int hwIndex, int hwChannel);

XLstatus __stdcall xlGetDriverConfig(XLdriverConfig* pDriverConfig);

XLstatus __stdcall xlOpenPort(XLportHandle* pPortHandle, char* userName,
                               XLaccess accessMask, XLaccess* pPermissionMask,
                               unsigned int rxQueueSize, unsigned int xlInterfaceVersion,
                               unsigned int busType);

XLstatus __stdcall xlClosePort(XLportHandle portHandle);

XLstatus __stdcall xlActivateChannel(XLportHandle portHandle, XLaccess accessMask,
                                      unsigned int busType, unsigned int flags);

XLstatus __stdcall xlDeactivateChannel(XLportHandle portHandle, XLaccess accessMask);

// ---------------------------------------------------------------------------
// Classic CAN I/O
// ---------------------------------------------------------------------------
XLstatus __stdcall xlCanSetChannelBitrate(XLportHandle portHandle, XLaccess accessMask,
                                           unsigned long bitrate);

XLstatus __stdcall xlCanTransmit(XLportHandle portHandle, XLaccess accessMask,
                                  unsigned int* pMsgCount, void* pMsg);

XLstatus __stdcall xlReceive(XLportHandle portHandle, unsigned int* pMsgCount, XLevent* pEvent);

// ---------------------------------------------------------------------------
// Notification / diagnostics
// ---------------------------------------------------------------------------
XLstatus __stdcall xlSetNotification(XLportHandle portHandle, HANDLE* pHandle, int queueLevel);
char* __stdcall xlGetErrorString(XLstatus err);

#ifdef __cplusplus
}
#endif

#endif // VXLAPI_COMPAT_H
