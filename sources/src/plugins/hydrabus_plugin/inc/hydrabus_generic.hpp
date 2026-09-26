#ifndef HYDRABUS_GENERIC_HPP
#define HYDRABUS_GENERIC_HPP
#include "ICommDriver.hpp"
#include "uCommScriptClient.hpp"
#include "uFile.hpp"
#include "uHexlify.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uString.hpp"
#include "uUart.hpp" // for the global ::UART (ICommDriver-derived) used by generic_execute_script

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR                  "HB_GENERIC  |"
#define LOG_HDR                 LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//              LOCAL DEFINES AND DATA TYPES                     //
///////////////////////////////////////////////////////////////////

#define HB_WRITE_MAX_CHUNK_SIZE ((size_t)(4096U))
#define HB_BULK_MAX_BYTES       ((size_t)(16U))

template <typename T>
using MCFP = bool (T::*)(const std::string &args, std::stop_token st) const;

template <typename T>
using ModuleCommandsMap = std::map<const std::string, MCFP<T>>;

using ModuleSpeedMap    = std::map<const std::string, const size_t>;
using SpeedsMapsMap     = std::map<const std::string, ModuleSpeedMap *>;

template <typename T>
using CommandsMapsMap = std::map<const std::string, ModuleCommandsMap<T> *>;

/////////////////////////////////////////////////////////////////////////////////
//                 GENERIC TEMPLATE HELPERS                                    //
/////////////////////////////////////////////////////////////////////////////////

/* =================================================================================
   generic_module_list_commands  –  print available subcommands for a module
================================================================================= */
template <typename T>
bool generic_module_list_commands(const T *pOwner, const std::string &strModule)
{
    ModuleCommandsMap<T> *pMap = pOwner->getModuleCmdsMap(strModule);

    if (pMap && !pMap->empty()) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": available commands:"));
        for (const auto &cmd : *pMap) {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("  -"); LOG_STRING(cmd.first));
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
bool generic_module_dispatch(const T *pOwner,
                             const std::string &strModule,
                             const std::string &strCmd,
                             const std::string &strArgs,
                             std::stop_token st = {})
{
    ModuleCommandsMap<T> *pMap = pOwner->getModuleCmdsMap(strModule);
    auto it                    = pMap->find(strCmd);
    if (it != pMap->end()) {
        return (pOwner->*it->second)(strArgs, st);
    }
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule);
              LOG_STRING(": command not supported:"); LOG_STRING(strCmd));
    return false;
}

/* ============================================================================================
   generic_module_dispatch  –  split "cmd args" and dispatch
============================================================================================ */
template <typename T>
bool generic_module_dispatch(const T *pOwner,
                             const std::string &strModule,
                             const std::string &strArgs,
                             std::stop_token st = {})
{
    std::vector<std::string> parts;
    ustring::splitAtFirst(strArgs, CHAR_SEPARATOR_SPACE, parts);

    if (parts.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": expected [help] or [cmd strArgs]"));
        return false;
    }

    // "help" and "mode" are single-token commands
    if (parts.size() == 1 && (parts[0] == "help" || parts[0] == "mode")) {
        return generic_module_dispatch<T>(pOwner, strModule, parts[0], "", st);
    }

    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": expected [cmd strArgs]"));
        return false;
    }

    return generic_module_dispatch<T>(pOwner, strModule, parts[0], parts[1], st);
}

/* ============================================================================================
   generic_module_set_speed  –  look up a speed string and dispatch the speed command
============================================================================================ */
template <typename T>
bool generic_module_set_speed(const T *pOwner,
                              const std::string &strModule,
                              const std::string &strArgs)
{
    const ModuleSpeedMap *pSpeedMap = pOwner->getModuleSpeedsMap(strModule);
    if (!pSpeedMap) {
        return false;
    }

    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING(strModule); LOG_STRING(": available speeds:"));
        for (const auto &s : *pSpeedMap) {
            std::string line = s.first + " -> index " + std::to_string(s.second);
            LOG_PRINT(LOG_EMPTY, LOG_STRING(line));
        }
        return true;
    }

    auto it = pSpeedMap->find(strArgs);
    if (it == pSpeedMap->end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule);
                  LOG_STRING(": unknown speed:"); LOG_STRING(strArgs));
        return false;
    }

    return pOwner->setModuleSpeed(strModule, it->second);
}

/* =================================================================================
   generic_write_data  –  parse a hex string and call a write callback (1..16 bytes)
================================================================================= */
template <typename T>
using WriteCbk = bool (T::*)(std::span<const uint8_t>) const;

template <typename T>
bool generic_write_data(const T *pOwner, const std::string &strArgs, WriteCbk<T> cbk)
{
    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABBCC..  (hex, 1-16 bytes)"));
        return true;
    }

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(strArgs, data)) {
        return false;
    }

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
using WrRdCbk = bool (T::*)(std::span<const uint8_t>, size_t, std::stop_token) const;

template <typename T>
bool generic_write_read_data(const T *pOwner, const std::string &strArgs, WrRdCbk<T> cbk, std::stop_token st = {})
{
    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: [hexdata][:rdlen]  e.g. DEADBEEF:4 | :4 | DEADBEEF"));
        return true;
    }

    std::vector<uint8_t> request;
    size_t readLen = 0;

    if (strArgs[0] == ':') {
        if (!numeric::str2sizet(strArgs.substr(1), readLen)) {
            return false;
        }
    } else {
        std::vector<std::string> parts;
        ustring::tokenize(strArgs, CHAR_SEPARATOR_COLON, parts);
        if (parts.empty()) {
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

    return (pOwner->*cbk)(request, readLen, st);
}

/* ============================================================================================
   generic_write_read_file  –  read a file and call wrrd in chunks
============================================================================================ */
template <typename T>
bool generic_write_read_file(const T *pOwner,
                             const std::string &strArgs,
                             WrRdCbk<T> cbk,
                             const std::string &strArtefactsPath,
                             std::stop_token st = {})
{
    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: filename[:wrchunk][:rdchunk]"));
        return true;
    }

    std::vector<std::string> parts;
    ustring::tokenize(strArgs, CHAR_SEPARATOR_COLON, parts);
    if (parts.empty()) {
        return false;
    }

    std::string path;
    ufile::buildFilePath(strArtefactsPath, parts[0], path);

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
    if (wrChunk == 0) {
        wrChunk = HB_WRITE_MAX_CHUNK_SIZE;
    }
    if (rdChunk == 0) {
        rdChunk = HB_WRITE_MAX_CHUNK_SIZE;
    }

    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        return false;
    }

    auto fileSize   = ufile::getFileSize(path);
    size_t nChunks  = static_cast<size_t>(fileSize / wrChunk);
    size_t lastSize = static_cast<size_t>(fileSize % wrChunk);

    for (size_t i = 0; i < nChunks; ++i) {
        std::vector<uint8_t> buf(wrChunk);
        fin.read(reinterpret_cast<char *>(buf.data()), wrChunk);
        if (!(pOwner->*cbk)(buf, rdChunk, st)) {
            return false;
        }
    }
    if (lastSize > 0) {
        std::vector<uint8_t> buf(lastSize);
        fin.read(reinterpret_cast<char *>(buf.data()), lastSize);
        if (!(pOwner->*cbk)(buf, std::min(rdChunk, lastSize), st)) {
            return false;
        }
    }
    return true;
}

/* ============================================================================================
   generic_execute_script  –  run a CommScriptClient script via the raw UART driver
   (Bus Pirate / HydraBus binary protocol style).

   pOwner must expose:
     mutable UART drvUart  (public)
     friend const IniValues* getAccessIniValues(const T&)
============================================================================================ */
template <typename T>
bool generic_execute_script(const T *pDriver, const std::string &strPluginName, const std::string &strScriptName, std::stop_token st = {})
{
    const auto *ini = getAccessIniValues(*pDriver);

    if (strScriptName == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: <scriptname>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/scriptname"));
        return true;
    }

    std::string strPath;
    ufile::buildFilePath(ini->strArtefactsPath, strScriptName, strPath);
    if (!ufile::fileExistsAndNotEmpty(strPath)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found:"); LOG_STRING(strPath));
        return false;
    }

    // Build a non-owning shared_ptr alias around the raw UART driver
    auto spUart = std::shared_ptr<::UART>(std::shared_ptr<::UART>{}, &pDriver->drvUart);
    try {
        CommScriptClient<::UART> client(strPath, spUart,
                                        strPluginName,
                                        HB_BULK_MAX_BYTES,
                                        ini->u32ReadTimeout,
                                        ini->u32ScriptDelay,
                                        typename CommScriptClient<::UART>::SendFunc{},
                                        typename CommScriptClient<::UART>::RecvFunc{},
                                        st);
        bool bEnabled = getEnabledStatus(*pDriver);
        return client.execute(bEnabled);
    } catch (const std::exception &e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script failed:"); LOG_STRING(e.what()));
    }
    return false;
}

#endif // HYDRABUS_GENERIC_HPP
