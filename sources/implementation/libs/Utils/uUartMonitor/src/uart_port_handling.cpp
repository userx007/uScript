
#include "uart_port_handling.hpp"
#include "string_handling.hpp"
#include "time_handling.hpp"
#include "device_handling.hpp"
#include "dlt.h"

#ifdef _WIN32
#include <cstring>
#include <windows.h>
#endif


///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
///////////////////////////////////////////////////////////////////

#undef  LT_HDR
#define LT_HDR     "UART_PORT:"
#undef  DLT_HDR
#define DLT_HDR     DLT_STRING(LT_HDR)

DLT_DECLARE_CONTEXT(UartPortCtx)

///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

#define TARGET_PATH_SIZE        256U
#define DEEP_DEBUG_MODE         0


///////////////////////////////////////////////////////////////////
//            LOCAL VARIABLES DECLARATION                        //
///////////////////////////////////////////////////////////////////

#ifndef _WIN32
static const std::vector<std::string> g_vstrPatterns{ "/dev/ttyACM*", "/dev/ttyUSB*" };
#endif /* _WIN32 */

///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES DECLARATION                     //
///////////////////////////////////////////////////////////////////

static bool priv_uart_scan_ports              ( device_handling_s *psInst, const bool bContext, char *pstrItem, char *pstrTargetPath, const bool bOptype );
static void priv_uart_scan_ports              ( std::vector<std::string> &vstrPortList );
static bool priv_uart_wait_port               ( device_handling_s *psInst, char *pstrItem, const uint32_t uiItemSize, const uint32_t uiTimeout, const uint32_t uiPollingInterval, const bool bOptype );
static bool priv_uart_monitor                 ( device_handling_s *psInst, const uint32_t uiPollingInterval, std::atomic<bool> & bRun );
#ifdef _WIN32
static void priv_uart_scan_test_buffer_error  ( void );
#endif // _WIN32

///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

/**
 * \brief Function to wait for UART port insertion
 * \param[out] pstrItem buffer where to return the name of the inserted port (COMn)
 * \param[in] uiItemSize size of the buffer  where to return the name of the inserted port
 * \param[in] uiTimeout timeout to wait for insertion (if 0 then wait forewer)
 * \param[in] uiPollingInterval poling interval to wait for USB insertion
 * \return true on changes, false otherwise
 */

bool uart_wait_port_insert( char *pstrItem, const uint32_t uiItemSize, const uint32_t uiTimeout, const uint32_t uiPollingInterval )
{
    device_handling_s sDeviceHdl = { 0 };

    return priv_uart_wait_port( &sDeviceHdl, pstrItem, uiItemSize, uiTimeout, uiPollingInterval, OP_ITEM_INSERT );

}


/**
 * \brief Function to wait for UART port removal
 * \param[out] pstrItem buffer where to return the name of the removed port (COMn, n)
 * \param[in] uiItemSize size of the buffer  where to return the name of the removed port
 * \param[in] uiTimeout timeout to wait for removal (if 0 then wait forewer)
 * \param[in] uiPollingInterval poling interval to wait for USB removal
 * \return true on changes, false otherwise
 */

bool uart_wait_port_remove( char *pstrItem, const uint32_t uiItemSize, const uint32_t uiTimeout, const uint32_t uiPollingInterval )
{
    device_handling_s sDeviceHdl = { 0 };

    return priv_uart_wait_port( &sDeviceHdl, pstrItem, uiItemSize, uiTimeout, uiPollingInterval, OP_ITEM_REMOVE );

}


/**
 * \brief Function to monitorize the UART port insertion and removal
 * \param[in] uiPollingInterval poling interval to wait for port insertion
 * \param[in] bRun flag used to end the monitoring loop
 * \return true on changes, false otherwise
 */

bool uart_monitor( const uint32_t uiPollingInterval, std::atomic<bool> &bRun )
{
    device_handling_s sDeviceHdl = { 0 };

    return priv_uart_monitor( &sDeviceHdl, uiPollingInterval, bRun );

}


/**
 * \brief Print the list of UART ports
 * \param none
 * \return void
 */

void uart_list_ports( const char *pstrCaption )
{
    std::vector<std::string> vstrPorts;

    priv_uart_scan_ports(vstrPorts);
    string_print_vector_content( ((nullptr == pstrCaption)  ? "Ports" : pstrCaption), LT_HDR, DLT_LOG_INFO, vstrPorts, " | " );

}


/**
 * \brief Get the number of UART ports found in the system at the moment of the call
 * \param none
 * \return number of UART ports found at the moment of the call
 */

uint32_t uart_get_available_ports_number( void )
{
    uint32_t uPortCount = 0;

#ifdef _WIN32

    char  pstrTargetPath[TARGET_PATH_SIZE] = { 0 };
    char  pstrPortName[MAX_ITEM_SIZE] = { 0 };

    for( int i = 1; i <= 255; ++i )
    {
        snprintf(pstrPortName, sizeof(pstrPortName), "COM%d", i);

        if( 0 != QueryDosDevice( pstrPortName, pstrTargetPath, TARGET_PATH_SIZE) )
        {
            ++uPortCount;
        }
        priv_uart_scan_test_buffer_error();
    }

#else // linux

    for( auto pstrPortName : glob(g_vstrPatterns) )
    {
        ++uPortCount;
    }

#endif

    return uPortCount;

}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////

#ifdef _WIN32

/**
 * \brief show error in case of buffer too small
 * \param none
 * \return none
 */

static void priv_uart_scan_test_buffer_error( void )
{
    if( ERROR_INSUFFICIENT_BUFFER == GetLastError() )
    {
        DLT_LOG(UartPortCtx, DLT_LOG_INFO, DLT_HDR; DLT_STRING("Targetpath buffer too small:"); DLT_UINT32(TARGET_PATH_SIZE));
    }

}


#endif //_WIN32

/**
 * \brief Generic function to wait for UART port handling
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] pstrItem buffer where to return the name of the handled port (COMn)
 * \param[in] uiItemSize size of the buffer  where to return the name of the handled port
 * \param[in] uiTimeout timeout to wait for handling (if 0 then wait forewer)
 * \param[in] uiPollingInterval poling interval to wait for USB handling
 * \param[in] bOptype flag describing the type of monitoring, insert or remove
 * \return true on changes, false otherwise
 */

static bool priv_uart_wait_port( device_handling_s *psInst, char *pstrItem, const uint32_t uiItemSize, const uint32_t uiTimeout, const uint32_t uiPollingInterval, const bool bOptype )
{
    bool bRetVal = false;

#ifdef _WIN32
    char pstrTargetPath[TARGET_PATH_SIZE] = { 0 };
#else
    char *pstrTargetPath = NULL;
#endif

    do {

        if( (NULL == pstrItem) || (0 == uiItemSize) || (uiItemSize > MAX_ITEM_SIZE) )
        {
            DLT_LOG(UartPortCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Invalid buffer/size"));
            break;
        }

        if( (0 == uiPollingInterval) || ((0 != uiTimeout) && (uiTimeout < uiPollingInterval)) )
        {
            DLT_LOG(UartPortCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Invalid timeout(s)"));
            break;
        }

        // cleanup the lists
        device_handling_init(psInst);

        // read the existing ports
        if( false == priv_uart_scan_ports(psInst, CTX_INITIAL_DETECTION, pstrItem, pstrTargetPath, OP_ITEM_INSERT) )
        {
            break;
        }

        // clean the buffer
        memset(pstrItem, 0, MAX_ITEM_SIZE);

        // if timeout value was provided then wait with timeout otherwise wait forever for device handling
        if( 0 != uiTimeout )
        {
            uint32_t uiMaxCycles = uiTimeout / uiPollingInterval;
            DLT_LOG(UartPortCtx, DLT_LOG_VERBOSE, DLT_HDR; DLT_STRING("Timeout[ms]:"); DLT_UINT32(uiTimeout); DLT_STRING("Polling[ms]:"); DLT_UINT32(uiPollingInterval); DLT_STRING("Cycles:"); DLT_UINT32(uiMaxCycles));
            do {
                --uiMaxCycles;
                time_sleep(uiPollingInterval);

                if( OP_ITEM_REMOVE == bOptype )
                {
                    device_handling_reset_all_flags(psInst);
                }

                bRetVal = priv_uart_scan_ports(psInst, CTX_RUNTIME_DETECTION, pstrItem, pstrTargetPath, bOptype);
            } while ( (false == bRetVal) && (uiMaxCycles > 0) );

            // if timeout occurs then return true and an empty port
            if( 0 == uiMaxCycles )
            {
                *pstrItem = '\0';
                bRetVal = true;
                break;
            }
        }
        else
        {
            do {
                time_sleep(uiPollingInterval);
                bRetVal = priv_uart_scan_ports(psInst, CTX_RUNTIME_DETECTION, pstrItem, pstrTargetPath, bOptype);
            } while ( false == bRetVal );
        }

    } while(false);

    return bRetVal;

}


/**
 * \brief Generic function to monitorize UART port insertion / removal
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] uiPollingInterval poling interval to wait for USB handling
 * \param[in] bRun flag used to end the monitoring loop
 * \return true on changes, false otherwise
 */

static bool priv_uart_monitor( device_handling_s *psInst, const uint32_t uiPollingInterval, std::atomic<bool> &bRun )
{
    bool bRetVal = false;
    char pstrItem[MAX_ITEM_SIZE] = { 0 };     // buffer to store device name, i.e. windows: COM1 ... COM255, linux: /dev/ttyUSB0 .. 255 /dev/ttyACM0 .. 255
#ifdef _WIN32
    char pstrTargetPath[TARGET_PATH_SIZE] = { 0 };
#else
    char *pstrTargetPath = NULL;
#endif

    do {

        if( 0 == uiPollingInterval )
        {
            DLT_LOG(UartPortCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("uiPollingInterval : can't be 0"));
            break;
        }

        // cleanup the lists
        device_handling_init(psInst);

        // read the existing ports
        if( false == priv_uart_scan_ports(psInst, CTX_INITIAL_DETECTION, pstrItem, pstrTargetPath, OP_ITEM_INSERT) )
        {
            break;
        }

        // clean the buffer
        memset(pstrItem, 0, MAX_ITEM_SIZE);

        do {

            time_sleep(uiPollingInterval);
            device_handling_reset_all_flags(psInst);

            // check for UART insertion
            if( true == priv_uart_scan_ports(psInst, CTX_RUNTIME_DETECTION, pstrItem, pstrTargetPath, OP_ITEM_INSERT) )
            {
                std::string strCaption = "(T) Inserted [" + std::string(pstrItem) + "] =>";
                uart_list_ports(strCaption.c_str());
            }

            // check for UART removal
            if( true == priv_uart_scan_ports(psInst, CTX_RUNTIME_DETECTION, pstrItem, pstrTargetPath, OP_ITEM_REMOVE) )
            {
                std::string strCaption = "(T) Removed  [" + std::string(pstrItem) + "] =>";
                uart_list_ports(strCaption.c_str());
            }

        } while ( true == bRun.load() );

    } while(false);

    return bRetVal;

}


#ifdef _WIN32


/**
 * \brief Function to scan UART USB ports in order to detect changes
 * \param[in] psInst pointer to a structure containing the working context
 * \param[in] bContext context of call (initial call or cyclical runtime calls)
 * \param[in] pstrItem buffer where to return the changed port
 * \param[in] pstrTargetPath COM port path
 * \param[in] bOptype type of the operation to be checked, insertion/removal
 * \return true on changes, false otherwise
 *
 */

static bool priv_uart_scan_ports( device_handling_s *psInst, const bool bContext, char *pstrItem, char *pstrTargetPath, const bool bOptype )
{
    bool bRetVal = false;
    char pstrPortName[MAX_ITEM_SIZE] = { 0 };

    for( int i = 1; i <= 255; ++i )
    {
        snprintf(pstrPortName, sizeof(pstrPortName), "COM%d", i);

        if( 0 != QueryDosDevice( pstrPortName, pstrTargetPath, TARGET_PATH_SIZE) )
        {
            bRetVal = device_handling_process( psInst, pstrPortName, pstrItem, bOptype);

            if (CTX_RUNTIME_DETECTION == bContext)
            {
                if( OP_ITEM_INSERT == bOptype )
                {
                    if( true == bRetVal )
                    {
#if (1 == DEEP_DEBUG_MODE )
                        DLT_LOG(UartPortCtx, DLT_LOG_INFO, DLT_HDR; DLT_STRING(pstrPortName);DLT_STRING(":"); DLT_STRING(pstrTargetPath));
#endif //(1 == DEEP_DEBUG_MODE )
                        break;
                    }
                }
            }
            else
            {
#if (1 == DEEP_DEBUG_MODE )
                DLT_LOG(UartPortCtx, DLT_LOG_INFO, DLT_HDR; DLT_STRING(pstrPortName);DLT_STRING(":"); DLT_STRING(pstrTargetPath));
#endif //(1 == DEEP_DEBUG_MODE )
            }
        }
        // check for error
        priv_uart_scan_test_buffer_error();
    }

    // it's OK to wait for insertion if no device was found at start
    if( (CTX_INITIAL_DETECTION == bContext) && (OP_ITEM_INSERT == bOptype) && (false == bRetVal) )
    {
        bRetVal = true;
    }

    // report the removed device at runtime
    else if( (CTX_RUNTIME_DETECTION == bContext) && (OP_ITEM_REMOVE == bOptype) )
    {
        bRetVal = device_handling_get_removed(psInst, pstrItem);
    }

    return bRetVal;

}


/**
 * \brief Function to scan UART USB ports in order to return a list of them
 * \param[in] vstrPortList vector where to store the list of available ports
 * \return true on changes, false otherwise
 *
 */

static void priv_uart_scan_ports( std::vector<std::string> &vstrPortList )
{
    char  pstrTargetPath[TARGET_PATH_SIZE] = { 0 };
    char  pstrPortName[MAX_ITEM_SIZE] = { 0 };

    for( int i = 1; i <= 255; ++i )
    {
        snprintf(pstrPortName, sizeof(pstrPortName), "COM%d", i);

        if( 0 != QueryDosDevice( pstrPortName, pstrTargetPath, TARGET_PATH_SIZE) )
        {
            vstrPortList.push_back(pstrPortName);
        }
        priv_uart_scan_test_buffer_error();
    }

}


#else /* linux */

static bool priv_uart_scan_ports( device_handling_s *psInst, const bool bContext, char *pstrItem, char *pstrTargetPath, const bool bOptype )
{
    bool bRetVal = false;

    for( auto pstrPortName : glob(g_vstrPatterns) )
    {
        bRetVal = device_handling_process( psInst, pstrPortName.c_str(), pstrItem, bOptype);

        if (CTX_RUNTIME_DETECTION == bContext)
        {
            if( OP_ITEM_INSERT == bOptype )
            {
                if( true == bRetVal )
                {
#if (1 == DEEP_DEBUG_MODE )
                    DLT_LOG(UartPortCtx, DLT_LOG_INFO, DLT_HDR; DLT_STRING(pstrPortName););
#endif //(1 == DEEP_DEBUG_MODE )
                    break;
                }
            }
        }
        else
        {
#if (1 == DEEP_DEBUG_MODE )
            DLT_LOG(UartPortCtx, DLT_LOG_INFO, DLT_HDR; DLT_STRING(pstrPortName));
#endif //(1 == DEEP_DEBUG_MODE )
        }
    }

    // it's OK to wait for insertion if no device was found at start
    if( (CTX_INITIAL_DETECTION == bContext) && (OP_ITEM_INSERT == bOptype) && (false == bRetVal) )
    {
        bRetVal = true;
    }

    // report the removed device at runtime
    else if( (CTX_RUNTIME_DETECTION == bContext) && (OP_ITEM_REMOVE == bOptype) )
    {
        bRetVal = device_handling_get_removed(psInst, pstrItem);
    }

    return bRetVal;

}


/**
 * \brief light version used to only list the ports
 */
static void priv_uart_scan_ports( std::vector<std::string> &vstrPortList )
{
    for( auto pstrPortName : glob(g_vstrPatterns) )
    {
        vstrPortList.push_back(pstrPortName);
    }

}


#endif /* _WIN32 */


