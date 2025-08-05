
#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "bithandling.h"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uFile.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sys/stat.h>
#if !defined(_MSC_VER)
    #include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////
//                 LOG DEFINES                                   //
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

ModuleCommandsMap<BuspiratePlugin>* BuspiratePlugin::getModuleCmdsMap ( const std::string& strModule) const
{
    ModuleCommandsMap<BuspiratePlugin> *pCmdMap = nullptr;
    typename CommandsMapsMap<BuspiratePlugin>::const_iterator it = m_mapCommandsMaps.find(strModule);

    if (it != m_mapCommandsMaps.end() )
    {
        pCmdMap = it->second;
    }

    return pCmdMap;

} /* getModuleCmdsMap() */


/* ============================================================================================
    BuspiratePlugin::getModuleSpeedsMap
============================================================================================ */

ModuleSpeedMap* BuspiratePlugin::getModuleSpeedsMap ( const std::string& strModule) const
{
    ModuleSpeedMap *pSpeedMap = nullptr;

    for (auto it1 : m_mapSpeedsMaps) {
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

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("w/W - power supply: w(off) W(on)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("p/P - pull-ups resistors: p(off) P(on)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("a/A - AUX: a(GND) A(3.3V)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("c/C - CS: c C"));
    } else if ("?" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Peripheral:"); LOG_UINT8(request));
    }  else {
        // power
        if (ustring::containsChar(args, 'W')) { BIT_SET(request,   3); }
        if (ustring::containsChar(args, 'w')) { BIT_CLEAR(request, 3); }
        // pull-ups
        if (ustring::containsChar(args, 'P')) { BIT_SET(request,   2); }
        if (ustring::containsChar(args, 'p')) { BIT_CLEAR(request, 2); }
        // AUX
        if (ustring::containsChar(args, 'A')) { BIT_SET(request,   1); }
        if (ustring::containsChar(args, 'a')) { BIT_CLEAR(request, 1); }
        // CS
        if (ustring::containsChar(args, 'C')) { BIT_SET(request,   0); }
        if (ustring::containsChar(args, 'c')) { BIT_CLEAR(request, 0); }

        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(m_positive_response));
    }

    return bRetVal;

} /* generic_set_peripheral() */


/* ============================================================================================
    BuspiratePlugin::generic_write_read_data
============================================================================================ */

bool BuspiratePlugin::generic_write_read_data(const uint8_t u8Cmd, const std::string &args) const
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
        std::vector<uint8_t> response;
        size_t szReadSize = 0;

        if (CHAR_SEPARATOR_COLON == args[0]) {  // only read
            bRetVal = numeric::str2sizet(args.substr(1), szReadSize);
        } else {  // write and optional read
            ustring::tokenize(args, CHAR_SEPARATOR_COLON, vectParams);
            if (!vectParams.empty()) {
                bRetVal = hexutils::stringUnhexlify(vectParams[0], request);
                if (bRetVal && vectParams.size() == 2) {
                    bRetVal = numeric::str2sizet(vectParams[1], szReadSize);
                }
            }
        }

        if (bRetVal) {
            response.resize(szReadSize);  // allocate response buffer
            bRetVal = generic_internal_write_read_data(
                u8Cmd,
                std::span<const uint8_t>{request},
                std::span<uint8_t>{response}
           );
        }
    }

    return bRetVal;
}


/* ============================================================================================
    BuspiratePlugin::generic_write_read_file
============================================================================================ */

bool BuspiratePlugin::generic_write_read_file( const uint8_t u8Cmd, const std::string &args) const
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
            size_t szWriteChunkSize = BP_WRITE_MAX_CHUNK_SIZE;
            size_t szReadChunkSize  = BP_WRITE_MAX_CHUNK_SIZE;

            if (vectParams.size() >= 2) {
                size_t szWriteSize = 0;
                if (true == (bRetVal = numeric::str2sizet(vectParams[1], szWriteSize))) {
                    if (0 != szWriteSize) {
                        szWriteChunkSize = szWriteSize;
                        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Write chunk size:"); LOG_SIZET(szWriteChunkSize));
                    } else {
                        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Invalid write chunk size. Use default:"); LOG_SIZET(szWriteChunkSize));
                    }

                    if (3 == vectParams.size()) {
                        size_t szReadSize = 0;
                        if (true == (bRetVal = numeric::str2sizet(vectParams[2], szReadSize))) {
                            if (0 != szReadSize) {
                                szReadChunkSize = szReadSize;
                                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read chunk size:"); LOG_SIZET(szReadChunkSize));
                            } else {
                                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Invalid read chunk size. Use default:"); LOG_SIZET(szReadChunkSize));
                            }
                        }
                    } else {
                        szReadChunkSize = 0;
                        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read chunk size(unset):"); LOG_SIZET(szReadChunkSize));
                    }
                }
            }
            bRetVal = generic_internal_write_read_file(u8Cmd, vectParams[0], szWriteChunkSize, szReadChunkSize);
        }
    }

    return bRetVal;

} /* generic_write_read_file() */



/* ============================================================================================
    BuspiratePlugin::generic_wire_write_data (rawwire onewire)
============================================================================================ */
bool BuspiratePlugin::generic_wire_write_data(std::span<const uint8_t> data) const
{
    static constexpr size_t szBufflen = 17;

    if (data.size() + 1 >= szBufflen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Length too big (max 16):"); LOG_SIZET(data.size()));
        return false;
    }

    std::array<uint8_t, szBufflen> request = {};  // zero-initialized

    request[0] = 0x10 | static_cast<uint8_t>(data.size() - 1);
    std::copy(data.begin(), data.end(), request.begin() + 1);

    return generic_uart_send_receive(std::span<uint8_t>{request.data(), data.size() + 1});

} /* generic_wire_write_data() */



/* ============================================================================================
    BuspiratePlugin::generic_uart_send_receive

    std::array<uint8_t, 8> response = {0xA1, 0xB2, 0xC3}; // expected first 3 bytes
    bRetVal = generic_uart_send_receive(numeric::byte2span(request), response);

    Even if device sends 5 bytes, you get them all in `response`
    Comparison is done only against first 3 bytes

============================================================================================ */
bool BuspiratePlugin::generic_uart_send_receive( std::span<uint8_t> request, std::span<uint8_t> response, bool strictCompare) const
{
    // Determine if we should send
    bool shouldSend = std::any_of(request.begin(), request.end(), [](uint8_t b) { return b != 0x00; });

    // Determine if we should receive
    bool shouldReceive = response.size() > 0;

    // Determine if we should compare
    size_t expectedSize = std::count_if(response.begin(), response.end(), [](uint8_t b) { return b != 0x00; });
    bool shouldCompare = strictCompare && expectedSize > 0;

    // Send
    if (shouldSend) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Sending Request:"));
        hexutils::HexDump2(request.data(), request.size());

        if (UART::Status::SUCCESS != drvUart.timeout_write(m_u32WriteTimeout, request)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART write failed"));
            return false;
        }
    } else {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Request not initialized — skipping send"));
    }

    // Receive
    if (shouldReceive) {
        size_t szBytesRead = 0;
        if (UART::Status::SUCCESS != drvUart.timeout_read(m_u32ReadTimeout, response, szBytesRead)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART read failed"));
            return false;
        }

        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Received Answer:"));
        hexutils::HexDump2(response.data(), szBytesRead);

        // Compare
        if (shouldCompare) {
            std::vector<uint8_t> expected(response.begin(), response.begin() + expectedSize);

            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Expected Answer:"));
            hexutils::HexDump2(expected.data(), expected.size());

            if (szBytesRead < expectedSize) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Received fewer bytes than expected"));
                return false;
            }

            if (!std::equal(response.begin(), response.begin() + expectedSize, expected.begin())) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Received data does not match expected"));
                return false;
            }

            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Expected data matched successfully"));
        } else {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Comparison skipped"));
        }
    } else {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("No response buffer — skipping receive"));
    }

    return true;
}


/* ============================================================================================
    BuspiratePlugin::generic_internal_write_read_data
============================================================================================ */
bool BuspiratePlugin::generic_internal_write_read_data(const uint8_t u8Cmd, std::span<const uint8_t> request, std::span<uint8_t> response, bool strictCompare) const
{
    const size_t szWriteSize = request.size();
    const size_t szReadSize = response.size();

    if ((szWriteSize > BP_WRITE_MAX_CHUNK_SIZE) || (szReadSize > BP_WRITE_MAX_CHUNK_SIZE)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid length(s). Write:"); LOG_SIZET(szWriteSize); LOG_STRING("Read:"); LOG_SIZET(szReadSize));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Write:"); LOG_SIZET(szWriteSize); LOG_STRING("Read:"); LOG_SIZET(szReadSize));

    // Build command header
    std::vector<uint8_t> header {
        u8Cmd,
        static_cast<uint8_t>((szWriteSize >> 8) & 0xFF),
        static_cast<uint8_t>(szWriteSize & 0xFF),
        static_cast<uint8_t>((szReadSize >> 8) & 0xFF),
        static_cast<uint8_t>(szReadSize & 0xFF)
    };

    // Combine header + request
    std::vector<uint8_t> fullRequest;
    fullRequest.reserve(header.size() + request.size());
    fullRequest.insert(fullRequest.end(), header.begin(), header.end());
    fullRequest.insert(fullRequest.end(), request.begin(), request.end());

    // Send command + request, expect acknowledgment
    if (!generic_uart_send_receive(std::span<uint8_t>(fullRequest), numeric::byte2span(m_positive_response), true)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to send command or receive positive acknowledgment"));
        return false;
    }

    // Read actual response into provided buffer
    if (!generic_uart_send_receive(std::span<uint8_t>{}, response, strictCompare)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to read response data"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read buffer:"));
    hexutils::HexDump2(response.data(), response.size());

    return true;
}


/* ============================================================================================
    BuspiratePlugin::generic_internal_write_read_file
============================================================================================ */
bool BuspiratePlugin::generic_internal_write_read_file( const uint8_t u8Cmd, const std::string& strFileName, const size_t szWriteChunkSize, const size_t szReadChunkSize) const
{
    std::ifstream fin(strFileName, std::ios_base::in | std::ios::binary);
    if (!fin.is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open:"); LOG_STRING(strFileName));
        return false;
    }

    std::uintmax_t lFileSize = ufile::getFileSize(strFileName);
    if (lFileSize == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error or empty file:"); LOG_STRING(strFileName));
        return false;
    }

    size_t szNrChunks = static_cast<size_t>(lFileSize / szWriteChunkSize);
    size_t szLastChunkSize = static_cast<size_t>(lFileSize % szWriteChunkSize);

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Chunk size:"); LOG_SIZET(szWriteChunkSize); LOG_STRING("NrChunks:"); LOG_SIZET(szNrChunks); LOG_STRING("LastChunkSize:"); LOG_SIZET(szLastChunkSize));

    for (size_t i = 0; i < szNrChunks; ++i) {
        std::vector<uint8_t> request(szWriteChunkSize);
        fin.read(reinterpret_cast<char*>(request.data()), szWriteChunkSize);

        std::vector<uint8_t> response(szReadChunkSize, 0x00); // Preallocated read buffer
        if (!generic_internal_write_read_data(u8Cmd, request, response, false)) {
            return false;
        }
    }

    if (szLastChunkSize > 0) {
        std::vector<uint8_t> request(szLastChunkSize);
        fin.read(reinterpret_cast<char*>(request.data()), szLastChunkSize);

        size_t szLastReadSize = (std::min)(szReadChunkSize, szLastChunkSize);
        std::vector<uint8_t> response(szLastReadSize, 0x00);

        if (!generic_internal_write_read_data(u8Cmd, request, response, false)) {
            return false;
        }
    }

    return true;
}


bool BuspiratePlugin::generic_execute_script(const std::string &args) const
{
    std::string strScriptPathName;
    ufile::buildFilePath(m_strArtefactsPath, args, strScriptPathName);

    // Check file existence and size
    if (false == ufile::fileExistsAndNotEmpty(strScriptPathName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
        //break;
    }
#if 0
    try {
        // open the UART port (RAII implementation, the close is done by destructor)
        auto shpDriver = std::make_shared<UART>(m_strUartPort, m_u32UartBaudrate);

        // driver opened successfully
        if (shpDriver->is_open()) {
            PluginScriptClient<ICommDriver> client (
                strScriptPathName,
                shpDriver,

                [this, shpDriver](std::span<const uint8_t> data, std::shared_ptr<ICommDriver>) {
                    return this->m_Send(data, shpDriver);
                },

                [this, shpDriver](std::span<uint8_t> data, size_t& size, ReadType type, std::shared_ptr<ICommDriver>) {
                    return this->m_Receive(data, size, type, shpDriver);
                },

                m_u32ScriptDelay,
                m_u32UartReadBufferSize
           );
            bRetVal = client.execute();
        }
    } catch (const std::bad_alloc& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
    } catch (const std::exception& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
    }
#endif
}



