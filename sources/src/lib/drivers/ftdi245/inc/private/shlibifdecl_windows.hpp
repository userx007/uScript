
#ifndef SHLIBIFDECL_WINDOWS_HPP
#define SHLIBIFDECL_WINDOWS_HPP

#include "windows.h"
#include "winusb.h"

//////////////////////////////////////////////////////////////////////////////////////////
//                          FTDI SPECIFIC  DEFINITIONS
//////////////////////////////////////////////////////////////////////////////////////////

#define SIO_DISABLE_FLOW_CTRL                       0x00
#define SIO_SET_FLOW_CTRL_REQUEST                   0x02
#define SIO_SET_BAUDRATE_REQUEST                    0x03
#define SIO_SET_EVENT_CHAR_REQUEST                  0x06
#define SIO_SET_ERROR_CHAR_REQUEST                  0x07
#define SIO_SET_LATENCY_TIMER_REQUEST               0x09
#define SIO_GET_LATENCY_TIMER_REQUEST               0x0A
#define SIO_SET_BITMODE_REQUEST                     0x0B
#define SIO_READ_PINS_REQUEST                       0x0C
#define BITMODE_BITBANG                             0x01
#define SIO_XON_XOFF_HS                       (0x4 << 8)

//////////////////////////////////////////////////////////////////////////////////////////
//                    USB COMMUNICATION SPECIFIC  DEFINITIONS
//////////////////////////////////////////////////////////////////////////////////////////

/** \brief Internal defines
*/
#define STRING_MAX_LEN                              256
#define INTERFACES_COUNT                            38

/** \brief Standard USB descriptor types. Section 9-5 of the USB 3.0 specifications.
*/
#define USB_DESCRIPTOR_TYPE_DEVICE                  0x01 /* Device descriptor type. */


/** \brief Device list initialization flags.
*/
typedef enum ListingFlags_e_
{
    LSTFLAG_NONE,                                    /** \brief no flags */
    LSTFLAG_RAWGUID,                                 /** \brief only raw device interface GUID */
    LSTFLAG_DISCONNECT                               /** \brief all devices, inclusive the disconnected ones */
} ListingFlags_e;


/** \brief Synchronization flags
*/
typedef enum SyncFlags_e_
{
    ENUM_SYNCFLAG_NONE             = 0L,             /** \brief Cleared/invalid state. */
    ENUM_SYNCFLAG_UNCHANGED        = 0x0001,         /** \brief Unchanged state, */
    ENUM_SYNCFLAG_ADDED            = 0x0002,         /** \brief Added (Arrival) state, */
    ENUM_SYNCFLAG_REMOVED          = 0x0004,         /** \brief Removed (Unplugged) state, */
    ENUM_SYNCFLAG_CONNECT_CHANGE   = 0x0008,         /** \brief Connect changed state. */
    ENUM_SYNCFLAG_MASK             = 0x000F,         /** \brief All states. */
} SyncFlag_e;


/** \brief Common usb device information structure
 */
typedef struct DeviceInfoCommon_s_
{
    INT Vid;                                         /** \brief VendorID */
    INT Pid;                                         /** \brief ProductID*/
    INT MI;                                          /** \brief Composite interface number; set to -1 for devices that do not have the composite parent driver. */
    CHAR InstanceID[STRING_MAX_LEN];                 /** \brief An ID that uniquely identifies an USB device. */
} DeviceInfoCommon_s;

/** Supported drivers
 */
typedef enum DriverId_e_
{
    DRVID_LIBUSBK,        // libusbK.sys
    DRVID_LIBUSB0,        // libusb0.sys
    DRVID_WINUSB,         // WinUSB.sys
    DRVID_LIBUSB0_FILTER, // libusb0.sys filter
    DRVID_LAST
} e_DriverId;

/** \brief Device info structure definition
 */
typedef struct DeviceInfo_s_
{
    DeviceInfoCommon_s Common;                       /** \brief Common usb device information */
    INT DriverID;                                    /** \brief Driver id this device element is using */
    CHAR DeviceInterfaceGUID[STRING_MAX_LEN];        /** \brief Device interface GUID */
    CHAR DeviceID[STRING_MAX_LEN];                   /** \brief Device instance ID. */
    CHAR ClassGUID[STRING_MAX_LEN];                  /** \brief Class GUID. */
    CHAR Mfg[STRING_MAX_LEN];                        /** \brief Manufacturer name as specified in the INF file. */
    CHAR DeviceDesc[STRING_MAX_LEN];                 /** \brief Device description as specified in the INF file. */
    CHAR Service[STRING_MAX_LEN];                    /** \brief Driver service name. */
    CHAR SymbolicLink[STRING_MAX_LEN];               /** \brief Unique identifier. */
    CHAR DevicePath[STRING_MAX_LEN];                 /** \brief physical device filename used with the Windows CreateFile() */
    INT LUsb0FilterIndex;                            /** \brief libusb-win32 filter index id. */
    BOOL Connected;                                  /** \brief Indicates the devices connection state. */
    SyncFlag_e eSyncFlags;                           /** \brief Synchronization flags. (internal use only) */
    INT BusNumber;                                   /** \brief Bus number */
    INT DeviceAddress;                               /** \brief Device address */
    CHAR SerialNumber[STRING_MAX_LEN];               /** \brief Serial number */
} DeviceInfo_s;


/** \brief Prototypes of the used interfaces
*/
typedef BOOL (WINAPI *pfDevInfoCB_t)     ( VOID* pvDeviceList, DeviceInfo_s *psDeviceInfo, PVOID Context);
typedef BOOL (WINAPI *pfInit_t)          ( VOID* pvInterfaceHandle, DeviceInfo_s* pDevInfo);
typedef BOOL (WINAPI *pfFree_t)          ( VOID* pvInterfaceHandle);
typedef BOOL (WINAPI *pfCtrlTransfer_t)  ( VOID* pvInterfaceHandle, WINUSB_SETUP_PACKET sSetupPacket, PUCHAR Buffer, UINT BufferLength, PUINT LengthTransferred, LPOVERLAPPED Overlapped);
typedef BOOL (WINAPI *pfWritePipe_t)     ( VOID* pvInterfaceHandle, UCHAR PipeID, PUCHAR Buffer, UINT BufferLength, PUINT LengthTransferred, LPOVERLAPPED Overlapped);
typedef BOOL (WINAPI *pfUnused_t)        ( VOID );


/** \brief USB core driver API information structure. Contains driver and user specific information.
*/
typedef struct DriverApiInfo_s_
{
    INT DriverID;                                    /* Driver id of the driver API. */
    INT FunctionCount;                               /* Number of valid functions contained in the driver API.*/
} DriverApiInfo_s;


/** \brief Driver API function set structure (patched).
    \note  This structure has a fixed size of 512 bytes.
*/
typedef struct DriverApi_s_
{
    DriverApiInfo_s Info;

    pfInit_t         Init;             /* 00  Init */
    pfFree_t         Free;             /* 01  Free */
    pfUnused_t       reserved1[5];     /* 02..06 Unused */
    pfCtrlTransfer_t ControlTransfer;  /* 07  ControlTransfer */
    pfUnused_t       reserved2[17];    /* 08..24 Unused */
    pfWritePipe_t    WritePipe;        /* 25  WritePipe */
    pfUnused_t       reserved3[12];    /* 26..37 Unused */
    UCHAR z_F_i_x_e_d[512 - sizeof(DriverApiInfo_s) -  sizeof(UINT_PTR) * INTERFACES_COUNT - (INTERFACES_COUNT & (sizeof(UINT_PTR) - 1) ? (INTERFACES_COUNT & (~(sizeof(UINT_PTR) - 1))) + sizeof(UINT_PTR) : INTERFACES_COUNT)];
    UCHAR z_FuncSupported[(INTERFACES_COUNT & (sizeof(UINT_PTR) - 1) ? (INTERFACES_COUNT & (~(sizeof(UINT_PTR) - 1))) + sizeof(UINT_PTR) : INTERFACES_COUNT)];
} DriverApi_s;


/** \brief Prototypes of the interfaces to be accessed in libusbK.dll
*/
BOOL WINAPI ExtIF_LoadDrvApi   ( DriverApi_s *pDriverAPI, INT DriverID);
BOOL WINAPI ExtIF_LstInit      ( void** ppvDeviceList, ListingFlags_e eFlags);
BOOL WINAPI ExtIF_LstCount     ( void*  pvDeviceList, PUINT Count);
BOOL WINAPI ExtIF_LstFree      ( void*  pvDeviceList );
BOOL WINAPI ExtIF_LstEnumerate ( void*  pvDeviceList, pfDevInfoCB_t EnumDevListCB, PVOID Context);
BOOL WINAPI ExtIF_LstMoveNext  ( void*  pvDeviceList, DeviceInfo_s** ppDeviceInfo );
VOID WINAPI ExtIF_LstMoveReset ( void*  pvDeviceList);

#endif // SHLIBIFDECL_WINDOWS_HPP
