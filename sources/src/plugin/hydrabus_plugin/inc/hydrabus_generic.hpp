#pragma once

#include "ICommDriver.hpp"
#include "uLogger.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"
#include "uFile.hpp"

#include <vector>
#include <map>
#include <span>
#include <functional>
#include <string>
#include <cstdint>

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR   "HB_GENERIC |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//              LOCAL DEFINES AND DATA TYPES                     //
///////////////////////////////////////////////////////////////////

#define HB_WRITE_MAX_CHUNK_SIZE  ((size_t)(4096U))
#define HB_BULK_MAX_BYTES        ((size_t)(16U))

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

/* ============================================================================================
   generic_module_list_commands  –  print available subcommands for a module
============================================================================================ */
template <typename T>
bool generic_module_list_commands(const T* pOwner, const std::string& strModule)
{
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

/* ============================================================================================
   generic_module_dispatch  –  find and call a named handler inside a module map
============================================================================================ */
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
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule);
              LOG_STRING(": command not supported:"); LOG_STRING(strCmd));
    return false;
}

/* ============================================================================================
   generic_module_dispatch  –  split "cmd args" and dispatch
============================================================================================ */
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

    // "help" and "mode" are single-token commands
    if (parts.size() == 1 && (parts[0] == "help" || parts[0] == "mode")) {
        return generic_module_dispatch<T>(pOwner, strModule, parts[0], "");
    }

    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": expected [cmd args]"));
        return false;
    }

    if (!pOwner->isEnabled()) return true;  // dry-run: validation only

    return generic_module_dispatch<T>(pOwner, strModule, parts[0], parts[1]);
}

/* ============================================================================================
   generic_module_set_speed  –  look up a speed string and dispatch the speed command
============================================================================================ */
template <typename T>
bool generic_module_set_speed(const T* pOwner,
                               const std::string& strModule,
                               const std::string& args)
{
    const ModuleSpeedMap* pSpeedMap = pOwner->getModuleSpeedsMap(strModule);
    if (!pSpeedMap) return false;

    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": available speeds:"));
        for (const auto& s : *pSpeedMap) {
            std::string line = s.first + " -> index " + std::to_string(s.second);
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(line));
        }
        return true;
    }

    auto it = pSpeedMap->find(args);
    if (it == pSpeedMap->end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule);
                  LOG_STRING(": unknown speed:"); LOG_STRING(args));
        return false;
    }

    return pOwner->setModuleSpeed(strModule, it->second);
}

/* ============================================================================================
   generic_write_data  –  parse a hex string and call a write callback (1..16 bytes)
============================================================================================ */
template <typename T>
using WriteCbk = bool (T::*)(std::span<const uint8_t>) const;

template <typename T>
bool generic_write_data(const T* pOwner, const std::string& args, WriteCbk<T> cbk)
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write AABBCC..  (hex, 1-16 bytes)"));
        return true;
    }

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data)) return false;

    if (data.empty() || data.size() > HB_BULK_MAX_BYTES) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1..16 bytes, got:");
                  LOG_SIZET(data.size()));
        return false;
    }

    return (pOwner->*cbk)(data);
}

/* ============================================================================================
   generic_write_read_data  –  parse "hexdata:readlen" or ":readlen" and call wrrd
============================================================================================ */
template <typename T>
using WrRdCbk = bool (T::*)(std::span<const uint8_t>, size_t) const;

template <typename T>
bool generic_write_read_data(const T* pOwner, const std::string& args, WrRdCbk<T> cbk)
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: [hexdata][:rdlen]  e.g. DEADBEEF:4 | :4 | DEADBEEF"));
        return true;
    }

    std::vector<uint8_t> request;
    size_t readLen = 0;

    if (args[0] == ':') {
        if (!numeric::str2sizet(args.substr(1), readLen)) return false;
    } else {
        std::vector<std::string> parts;
        ustring::tokenize(args, CHAR_SEPARATOR_COLON, parts);
        if (parts.empty()) return false;
        if (!hexutils::stringUnhexlify(parts[0], request)) return false;
        if (parts.size() == 2) {
            if (!numeric::str2sizet(parts[1], readLen)) return false;
        }
    }

    return (pOwner->*cbk)(request, readLen);
}

/* ============================================================================================
   generic_write_read_file  –  read a file and call wrrd in chunks
============================================================================================ */
template <typename T>
bool generic_write_read_file(const T* pOwner,
                              const std::string& args,
                              WrRdCbk<T> cbk,
                              const std::string& artefactsPath)
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: filename[:wrchunk][:rdchunk]"));
        return true;
    }

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_COLON, parts);
    if (parts.empty()) return false;

    std::string path;
    ufile::buildFilePath(artefactsPath, parts[0], path);

    if (!ufile::fileExistsAndNotEmpty(path)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("File not found or empty:"); LOG_STRING(path));
        return false;
    }

    size_t wrChunk = HB_WRITE_MAX_CHUNK_SIZE;
    size_t rdChunk = HB_WRITE_MAX_CHUNK_SIZE;
    if (parts.size() >= 2 && !numeric::str2sizet(parts[1], wrChunk)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid write chunk size:"); LOG_STRING(parts[1]));
        return false;
    }
    if (parts.size() >= 3 && !numeric::str2sizet(parts[2], rdChunk)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid read chunk size:"); LOG_STRING(parts[2]));
        return false;
    }
    if (wrChunk == 0) wrChunk = HB_WRITE_MAX_CHUNK_SIZE;
    if (rdChunk == 0) rdChunk = HB_WRITE_MAX_CHUNK_SIZE;

    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) return false;

    auto fileSize   = ufile::getFileSize(path);
    size_t nChunks  = static_cast<size_t>(fileSize / wrChunk);
    size_t lastSize = static_cast<size_t>(fileSize % wrChunk);

    for (size_t i = 0; i < nChunks; ++i) {
        std::vector<uint8_t> buf(wrChunk);
        fin.read(reinterpret_cast<char*>(buf.data()), wrChunk);
        if (!(pOwner->*cbk)(buf, rdChunk)) return false;
    }
    if (lastSize > 0) {
        std::vector<uint8_t> buf(lastSize);
        fin.read(reinterpret_cast<char*>(buf.data()), lastSize);
        if (!(pOwner->*cbk)(buf, std::min(rdChunk, lastSize))) return false;
    }
    return true;
}
