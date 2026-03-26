#ifndef CP2112_GENERIC_HPP
#define CP2112_GENERIC_HPP

#include "ICommDriver.hpp"
#include "uCommScriptClient.hpp"
#include "uLogger.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"
#include "uFile.hpp"

#include <vector>
#include <map>
#include <span>
#include <string>
#include <cstdint>
#include <fstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CP2112_GEN  |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//              CP2112-SPECIFIC SIZE CONSTANTS                   //
///////////////////////////////////////////////////////////////////

// Default chunk size for wrrdf write AND read phases.
// Must be <= CP2112::MAX_I2C_READ_LEN (512) or the driver returns INVALID_PARAM.
#define CP2112_WRITE_CHUNK_SIZE  ((size_t)(512U))

// Upper bound for a single generic_write_data call.
// The CP2112 has no hard write limit (driver auto-chunks at 61 B per HID
// report), but 4 KB is a generous and realistic ceiling for an I2C bridge.
#define CP2112_BULK_MAX_BYTES    ((size_t)(4096U))

///////////////////////////////////////////////////////////////////
//              LOCAL DEFINES AND DATA TYPES                     //
///////////////////////////////////////////////////////////////////

template <typename T>
using MCFP = bool (T::*)(const std::string& args) const;

template <typename T>
using ModuleCommandsMap = std::map<const std::string, MCFP<T>>;

using ModuleSpeedMap = std::map<const std::string, const size_t>;
using SpeedsMapsMap  = std::map<const std::string, ModuleSpeedMap*>;

template <typename T>
using CommandsMapsMap = std::map<const std::string, ModuleCommandsMap<T>*>;

///////////////////////////////////////////////////////////////////
//                 GENERIC TEMPLATE HELPERS                      //
///////////////////////////////////////////////////////////////////

/* ============================================================
   generic_module_list_commands
============================================================ */
template <typename T>
bool generic_module_list_commands(const T* pOwner, const std::string& strModule)
{
    // dry validation ends here
    if (!pOwner->isEnabled()) {
        return true;
    }

    ModuleCommandsMap<T>* pMap = pOwner->getModuleCmdsMap(strModule);
    if (pMap && !pMap->empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": available commands:"));
        for (const auto& cmd : *pMap) {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("  -"); LOG_STRING(cmd.first));
        }
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": no commands available"));
    }
    
    return true;
}

/* ============================================================
   generic_module_dispatch  (named cmd + args already split)
============================================================ */
template <typename T>
bool generic_module_dispatch(const T* pOwner,
                              const std::string& strModule,
                              const std::string& strCmd,
                              const std::string& args)
{
    ModuleCommandsMap<T>* pMap = pOwner->getModuleCmdsMap(strModule);
    auto it = pMap->find(strCmd);
    if (it != pMap->end()) {
        return (pOwner->*it->second)(args);
    }
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": command not supported:"); LOG_STRING(strCmd));
    return false;
}

/* ============================================================
   generic_module_dispatch  (single "cmd args" string)
============================================================ */
template <typename T>
bool generic_module_dispatch(const T* pOwner,
                              const std::string& strModule,
                              const std::string& args)
{
    std::vector<std::string> parts;
    ustring::splitAtFirst(args, CHAR_SEPARATOR_SPACE, parts);

    if (parts.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": expected [help] or [cmd args]"));
        return false;
    }

    // Single-token commands that take no arguments
    if (parts.size() == 1) {
        const std::string& cmd = parts[0];
        if (cmd == "help" || cmd == "close" || cmd == "scan" || cmd == "read") {

            // dry validation ends here
            if (!pOwner->isEnabled()) {
                return true;
            }

            return generic_module_dispatch<T>(pOwner, strModule, cmd, "");                
        }
    }

    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": expected [cmd args]"));
        return false;
    }

    // dry validation ends here
    if (!pOwner->isEnabled()) {
        return true;
    }

    return generic_module_dispatch<T>(pOwner, strModule, parts[0], parts[1]);
}

/* ============================================================
   generic_module_set_speed
============================================================ */
template <typename T>
bool generic_module_set_speed(const T* pOwner,
                               const std::string& strModule,
                               const std::string& args)
{
    const ModuleSpeedMap* pSpeedMap = pOwner->getModuleSpeedsMap(strModule);
    if (!pSpeedMap) {
        size_t hz = 0;
        if (!numeric::str2sizet(args, hz)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule);
                      LOG_STRING(": pass a raw Hz value (e.g. 400000)"));
            return false;
        }
        return pOwner->setModuleSpeed(strModule, hz);
    }

    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING(strModule); LOG_STRING(": available speeds:"));
        for (const auto& s : *pSpeedMap) {
            std::string line = s.first + " -> " + std::to_string(s.second) + " Hz";
            LOG_PRINT(LOG_EMPTY, LOG_STRING(line));
        }
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Or pass a raw Hz value directly"));
        return true;
    }

    auto it = pSpeedMap->find(args);
    if (it != pSpeedMap->end()) {
        return pOwner->setModuleSpeed(strModule, it->second);
    }

    size_t hz = 0;
    if (numeric::str2sizet(args, hz)) {
        return pOwner->setModuleSpeed(strModule, hz);
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule);
              LOG_STRING(": unknown speed:"); LOG_STRING(args));
    return false;
}

/* ============================================================
   generic_write_data  — parse hex string and call write callback
   Validates against CP2112_BULK_MAX_BYTES (4096)
============================================================ */
template <typename T>
using WriteCbk = bool (T::*)(std::span<const uint8_t>) const;

template <typename T>
bool generic_write_data(const T* pOwner, const std::string& args, WriteCbk<T> cbk)
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABBCC..  (hex bytes, up to 4096)"));
        return true;
    }

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data)) return false;

    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected at least 1 byte"));
        return false;
    }
    if (data.size() > CP2112_BULK_MAX_BYTES) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Too many bytes (max 4096):"); LOG_SIZET(data.size()));
        return false;
    }

    // dry validation ends here
    if (!pOwner->isEnabled()) {
        return true;
    }    

    return (pOwner->*cbk)(data);
}

/* ============================================================
   generic_write_read_data  — parse "hexdata:readlen" and call wrrd
============================================================ */
template <typename T>
using WrRdCbk = bool (T::*)(std::span<const uint8_t>, size_t) const;

template <typename T>
bool generic_write_read_data(const T* pOwner, const std::string& args, WrRdCbk<T> cbk)
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: [hexdata][:rdlen]  e.g. DEADBEEF:4 | :4 | DEADBEEF"));
        return true;
    }

    std::vector<uint8_t> request;
    size_t readLen = 0;

    if (args[0] == ':') {
        if (!numeric::str2sizet(args.substr(1), readLen)) {
            return false;
        }
    } else {
        std::vector<std::string> parts;
        ustring::tokenize(args, CHAR_SEPARATOR_COLON, parts);
        
        if (parts.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid arguments"));
            return false;
        }

        if (!hexutils::stringUnhexlify(parts[0], request)) {
            return false;
        }

        if (parts.size() == 2) {
            if (!numeric::str2sizet(parts[1], readLen)) {
                return false;
            }
        }
    }

    // dry validation ends here
    if (!pOwner->isEnabled()) {
        return true;
    }    

    return (pOwner->*cbk)(request, readLen);
}

/* ============================================================
   generic_write_read_file  — read file and call wrrd in chunks.
   Both wrChunk and rdChunk default to CP2112_WRITE_CHUNK_SIZE (512),
   which equals MAX_I2C_READ_LEN and is safe for both I2C directions.
   An explicit rdChunk > 512 is rejected before hitting the driver.
============================================================ */
template <typename T>
bool generic_write_read_file(const T* pOwner,
                              const std::string& args,
                              WrRdCbk<T> cbk,
                              const std::string& artefactsPath)
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: filename[:wrchunk][:rdchunk]"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Default chunk sizes: 512 bytes (= CP2112 MAX_I2C_READ_LEN)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  rdchunk must not exceed 512"));
        return true;
    }

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_COLON, parts);
    if (parts.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid arguments"));
        return false;
    }

    std::string path;
    ufile::buildFilePath(artefactsPath, parts[0], path);

    if (!ufile::fileExistsAndNotEmpty(path)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("File not found or empty:"); LOG_STRING(path));
        return false;
    }

    size_t wrChunk = CP2112_WRITE_CHUNK_SIZE;
    size_t rdChunk = CP2112_WRITE_CHUNK_SIZE;

    if (parts.size() >= 2 && !numeric::str2sizet(parts[1], wrChunk)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid write chunk size:"); LOG_STRING(parts[1]));
        return false;
    }

    if (parts.size() >= 3 && !numeric::str2sizet(parts[2], rdChunk)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid read chunk size:"); LOG_STRING(parts[2]));
        return false;
    }

    if (wrChunk == 0) {
        wrChunk = CP2112_WRITE_CHUNK_SIZE;
    }

    if (rdChunk == 0) {
        rdChunk = CP2112_WRITE_CHUNK_SIZE;
    }

    // Hard-cap rdChunk at the driver limit before it becomes a driver error
    if (rdChunk > CP2112_WRITE_CHUNK_SIZE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("rdChunk exceeds CP2112 MAX_I2C_READ_LEN (512):"); LOG_SIZET(rdChunk));
        return false;
    }

    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("File can't be open:"); LOG_STRING(path));        
        return false;
    }

    auto fileSize = ufile::getFileSize(path);
    if(0 == fileSize) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("File is empty:"); LOG_STRING(path));        
        return false;
    }

    // dry validation ends here
    if (!pOwner->isEnabled()) {
        return true;
    }    

    size_t nChunks  = static_cast<size_t>(fileSize / wrChunk);
    size_t lastSize = static_cast<size_t>(fileSize % wrChunk);

    for (size_t i = 0; i < nChunks; ++i) {
        std::vector<uint8_t> buf(wrChunk);
        fin.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(wrChunk));

        if (!(pOwner->*cbk)(buf, rdChunk)) {
            return false;
        }
    }
    if (lastSize > 0) {
        std::vector<uint8_t> buf(lastSize);
        fin.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(lastSize));

        if (!(pOwner->*cbk)(buf, std::min(rdChunk, lastSize))) {
            return false;
        }
    }

    return true;
}



/* ============================================================
   generic_execute_script  — execute a CommScriptClient script
                             through an already-open ICommDriver.

   The driver (TDriver) must be open before calling.
   A non-owning shared_ptr alias is created so CommScriptClient
   can hold it without taking ownership from the plugin's unique_ptr.

   INI keys consumed:  READ_TIMEOUT, SCRIPT_DELAY
   Searched in:        ARTEFACTS_PATH / scriptName
============================================================ */
template <typename TDriver>
bool generic_execute_script(
    TDriver*           pDriver,
    const std::string& scriptName,
    const std::string& artefactsPath,
    size_t             szMaxRecvSize,
    uint32_t           u32ReadTimeout,
    uint32_t           u32ScriptDelay,
    bool               bEnabled)
{
    if (!pDriver || !pDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Driver not open — run 'open' first"));
        return false;
    }

    std::string strPath;
    ufile::buildFilePath(artefactsPath, scriptName, strPath);

    if (!ufile::fileExistsAndNotEmpty(strPath)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strPath));
        return false;
    }

    // Create a non-owning alias: CommScriptClient holds shared_ptr but
    // the unique_ptr in the plugin remains the real owner.
    auto spDriver = std::shared_ptr<TDriver>(std::shared_ptr<TDriver>{}, pDriver);

    try {
        CommScriptClient<TDriver> client(strPath, spDriver,
                                          szMaxRecvSize,
                                          u32ReadTimeout,
                                          u32ScriptDelay);
        return client.execute(bEnabled);
    } catch (const std::bad_alloc& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("OOM allocating script client:"); LOG_STRING(e.what()));
    } catch (const std::exception& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script execution failed:"); LOG_STRING(e.what()));
    }
    return false;
}

#endif // CP2112_GENERIC_HPP
