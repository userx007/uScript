
#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "bithandling.h"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uFile.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sys/stat.h>
#if !defined(_MSC_VER)
    #include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BP_GENERIC :"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

/* ============================================================================================
    BuspiratePlugin::getModuleCmdsMap
============================================================================================ */

ModuleCommandsMap<BuspiratePlugin>* BuspiratePlugin::getModuleCmdsMap ( const std::string& strModule ) const
{
    ModuleCommandsMap<BuspiratePlugin> *pCmdMap = nullptr;
    typename CommandsMapsMap<BuspiratePlugin>::const_iterator it = m_mapCommandsMaps.find(strModule.c_str());

    if( it != m_mapCommandsMaps.end() )
    {
        pCmdMap = it->second;
    }

    return pCmdMap;

} /* getModuleCmdsMap() */


/* ============================================================================================
    BuspiratePlugin::getModuleSpeedsMap
============================================================================================ */

ModuleSpeedMap* BuspiratePlugin::getModuleSpeedsMap ( const std::string& strModule ) const
{
    ModuleSpeedMap *pSpeedMap = nullptr;

    for( auto it1 : m_mapSpeedsMaps ) {
        if (it1.first == strModule) {
            pSpeedMap = it1.second;
        }
    }

    return pSpeedMap;

} /* getModuleSpeedsMap() */


/* ============================================================================================
 0100wxyz – Configure peripherals w=power, x=pullups, y=AUX, z=CS

Enable (1) and disable (0) Bus Pirate peripherals and pins.
w enables the power supplies,
x toggles the on-board pull-up resistors,
y sets the state of the auxiliary pin,
z sets the chip select pin.
Features not present in a specific hardware version are ignored. Bus Pirate responds 0×01 on success.

Note: CS pin always follows the current HiZ pin configuration.
AUX is always a normal pin output (0=GND, 1=3.3volts).
============================================================================================ */

bool BuspiratePlugin::generic_set_peripheral(const std::string &args) const
{
    bool bRetVal = true;
    static uint8_t request = 0x40;

    if (args.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Invalid args"));
        bRetVal = false;
    } else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("w/W - power supply: w(off) W(on)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("p/P - pull-ups resistors: p(off) P(on)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("a/A - AUX: a(GND) A(3.3V)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("c/C - CS: c C"));
    } else if ("?" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Peripheral:"); LOG_UINT8(request));
    }  else {
        // power
        if (ustring::containsChar(args, 'W') ) { BIT_SET(request,   3); }
        if (ustring::containsChar(args, 'w') ) { BIT_CLEAR(request, 3); }
        // pull-ups
        if (ustring::containsChar(args, 'P') ) { BIT_SET(request,   2); }
        if (ustring::containsChar(args, 'p') ) { BIT_CLEAR(request, 2); }
        // AUX
        if (ustring::containsChar(args, 'A') ) { BIT_SET(request,   1); }
        if (ustring::containsChar(args, 'a') ) { BIT_CLEAR(request, 1); }
        // CS
        if (ustring::containsChar(args, 'C') ) { BIT_SET(request,   0); }
        if (ustring::containsChar(args, 'c') ) { BIT_CLEAR(request, 0); }

        uint8_t answer = 0x01;
        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(answer));
    }

    return bRetVal;

} /* generic_set_peripheral() */


/* ============================================================================================
    BuspiratePlugin::generic_write_read_data
============================================================================================ */

bool BuspiratePlugin::generic_write_read_data( const uint8_t u8Cmd, const std::string &args ) const
{
    bool bRetVal = true;

    if (args.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Invalid args"));
        bRetVal = false;
    } else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: [data][:rdsize]. Example: DEADCODE | BAADFOOD:7 | :7"));
    } else {
        std::vector<std::string> vectParams;
        std::vector<uint8_t> request;
        uint16_t iWriteSize = 0;
        uint16_t iReadSize  = 0;

        if (CHAR_SEPARATOR_COLON == args[0]) {    // only read
            iReadSize = atoi((args.substr(1)).c_str());
        } else {                                    // write and read
            ustring::tokenize(args, CHAR_SEPARATOR_COLON, vectParams);
            if (vectParams.size() >= 1) {
                if(true == (bRetVal = hexutils::stringUnhexlify(vectParams[0], request))){
                    iWriteSize = (uint16_t)request.size();
                    iReadSize  = (2 == vectParams.size()) ? atoi(vectParams[1].c_str()) : 0;
                }
            }
        }
        if( true == bRetVal ){
            bRetVal = generic_internal_write_read_data(u8Cmd, iWriteSize, iReadSize, request);
        }
    }

    return bRetVal;

} /* generic_write_read_data() */


/* ============================================================================================
    BuspiratePlugin::generic_write_read_file
============================================================================================ */

bool BuspiratePlugin::generic_write_read_file( const uint8_t u8Cmd, const std::string &args ) const
{
    bool bRetVal = true;

    if (args.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Invalid args"));
        bRetVal = false;
    } else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: filename[:wrsize][:rdsize]. Example: file | file:100 | file:100:100"));
    } else {
        std::vector<std::string> vectParams;
        ustring::tokenize(args, CHAR_SEPARATOR_COLON, vectParams);

        if (vectParams.size() >= 1) {
            uint16_t iWriteChunkSize = BP_WRITE_MAX_CHUNK_SIZE;
            uint16_t iReadChunkSize  = BP_WRITE_MAX_CHUNK_SIZE;

            if (vectParams.size() >= 2) {
                uint16_t iWrSize = atoi(vectParams[1].c_str());

                if (0 != iWrSize ) {
                    iWriteChunkSize = iWrSize;
                    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Write chunk size:"); LOG_UINT16(iWriteChunkSize));
                } else {
                    LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Invalid write chunk size. Use default:"); LOG_UINT16(iWriteChunkSize));
                }

                if (3 == vectParams.size() ) {
                    uint16_t iRdSize = atoi(vectParams[2].c_str());

                    if (0 != iRdSize ) {
                        iReadChunkSize = iRdSize;
                        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read chunk size:"); LOG_UINT16(iReadChunkSize));
                    } else {
                        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Invalid read chunk size. Use default:"); LOG_UINT16(iReadChunkSize));
                    }
                } else {
                    iReadChunkSize = 0;
                    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read chunk size(unset):"); LOG_UINT16(iReadChunkSize));
                }
            }
            bRetVal = generic_internal_write_read_file(u8Cmd, vectParams[0].c_str(), iWriteChunkSize, iReadChunkSize );
        }
    }

    return bRetVal;

} /* generic_write_read_file() */


/* ============================================================================================
    BuspiratePlugin::generic_wire_write_data (rawwire onewire)
============================================================================================ */

bool BuspiratePlugin::generic_wire_write_data( const uint8_t *pu8Data, const int iLen ) const
{
    uint8_t vcBuf[17] = { 0 };

    vcBuf[0]= 0x10 | (iLen - 1);
    memcpy(&vcBuf[1], pu8Data, iLen);

    return generic_uart_send_receive(vcBuf, (iLen + 1));

} /* generic_wire_write_data() */


/* ============================================================================================
    BuspiratePlugin::generic_internal_write_read_data
============================================================================================ */

bool BuspiratePlugin::generic_internal_write_read_data (const uint8_t u8Cmd, const int iWriteSize, const int iReadSize, std::vector<uint8_t>& data) const
{
    bool bRetVal = true;

    if (( iWriteSize > BP_WRITE_MAX_CHUNK_SIZE) || ( iReadSize > BP_WRITE_MAX_CHUNK_SIZE ) ) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid length(s). Write:"); LOG_UINT16(iWriteSize); LOG_STRING("Read:"); LOG_UINT16(iReadSize); LOG_STRING("Abort!"));
        bRetVal = false;
    } else {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Write:"); LOG_UINT16(iWriteSize); LOG_STRING("Read:"); LOG_UINT16(iReadSize));
        std::vector<uint8_t> cmd {  u8Cmd,
                                    ((uint8_t)((iWriteSize >> 8) & 0xff)),
                                    ((uint8_t)(iWriteSize & 0xff)),
                                    ((uint8_t)((iReadSize >> 8) & 0xff)),
                                    ((uint8_t)(iReadSize & 0xff))
                                 };

        data.insert( data.begin(), cmd.begin(), cmd.end() );

        if( true == (bRetVal = generic_uart_send_receive(data, g_positive_answer)) ) {
            char *pstrReadBuffer = new char[iReadSize];
            if( nullptr != pstrReadBuffer){
                size_t szBytesRead = 0;
                if (true == (bRetVal = (UART::Status::SUCCESS == drvUart.timeout_read(m_u32ReadTimeout, pstrReadBuffer, iReadSize, &szBytesRead)))) {
                    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read buffer:"));
                    hexutils::HexDump2(reinterpret_cast<const uint8_t*>(pstrReadBuffer), iReadSize);
                }
            } else {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Faiiled to allocate read buffer, bytes:"); LOG_UINT16(iReadSize));
                bRetVal = false;
            }
        }
    }

    return bRetVal;

} /* generic_internal_write_read_data() */


/* ============================================================================================
    BuspiratePlugin::generic_internal_write_read_file
============================================================================================ */

bool BuspiratePlugin::generic_internal_write_read_file( const uint8_t u8Cmd, const std::string& strFileName, const int iWriteChunkSize, const int iReadChunkSize ) const
{
    bool bRetVal = true;

    std::ifstream fin(strFileName, std::ios_base::in | std::ios::binary);

    if (false == fin.is_open() ) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open:"); LOG_STRING(strFileName); LOG_STRING("Abort!"));
        bRetVal = false;
    } else {
        long lFileSize = ufile::getFileSize(strFileName);

        if (0 == lFileSize ) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error or empty file:"); LOG_STRING(strFileName); LOG_STRING("Abort!"));
        } else {
            uint16_t iNrChunks = (uint16_t)(lFileSize / iWriteChunkSize);
            uint16_t iLastChunkSize = (uint16_t)(lFileSize % iWriteChunkSize);

            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Chunk size:"); LOG_UINT16(iWriteChunkSize); LOG_STRING("NrChunks:"); LOG_UINT16(iNrChunks); LOG_STRING("LastChunkSize:"); LOG_UINT16(iLastChunkSize));
            uint16_t iVectSize = ((0 == iNrChunks) && (iLastChunkSize > 0)) ? iLastChunkSize : iWriteChunkSize;

            for(uint16_t i = 0; i < iNrChunks; ++i) {
                std::vector<uint8_t> request(iVectSize, 0);
                fin.read( reinterpret_cast<char*>(request.data()), iVectSize );
                if (false == (bRetVal = generic_internal_write_read_data(u8Cmd, iVectSize, iReadChunkSize, request))) { break; }
            }

            if ((true == bRetVal) && (0 != iLastChunkSize) ) {
                uint16_t iLastReadSize = iReadChunkSize > iLastChunkSize ? iLastChunkSize : iReadChunkSize;
                std::vector<uint8_t> request(iLastChunkSize, 0);
                fin.read( reinterpret_cast<char*>(request.data()), iLastChunkSize);
                bRetVal = generic_internal_write_read_data(u8Cmd, iLastChunkSize, iLastReadSize, request);
            }
        }
    }

    return bRetVal;

} /* generic_write_read_file() */


/* ============================================================================================
    BuspiratePlugin::generic_uart_send_receive
============================================================================================ */

bool BuspiratePlugin::generic_uart_send_receive(std::span<uint8_t> request, std::span<uint8_t> expect = {}) const
{
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Request:"));
    hexutils::HexDump2(vectRequest.data(), vectRequest.size());

    if (false == expect.empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Expected Answer:"));
        hexutils::HexDump2(vectExpect.data(), vectExpect.size());
    }

    return (UART::Status::SUCCESS == drvUart.timeout_write(m_u32WriteTimeout, reinterpret_cast<const char*>(vectRequest.data()), vectRequest.size()) &&
           (expect.empty() ? true : (UART::Status::SUCCESS == drvUart.timeout_wait_for_token_buffer(m_u32ReadTimeout, (const char*)vectExpect.data(), vectExpect.size()))));

} /* generic_uart_send_receive() */
