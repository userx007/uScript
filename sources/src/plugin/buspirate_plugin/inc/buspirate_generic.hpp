#ifndef BUSPIRATE_GENERIC_HPP
#define BUSPIRATE_GENERIC_HPP

#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"
#include "uFile.hpp"
#include "uUart.hpp"

#include <vector>
#include <cstring>
#include <iostream>
#include <map>
#include <span>
#include <functional>



/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "BP_GENERIC_H|"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

#define BP_WRITE_MAX_CHUNK_SIZE ((int)(4096U))

template <typename T>
using WRITE_DATA_CB = bool (T::*)(std::span<const uint8_t> data) const;

template <typename T>
using READ_DATA_CB = bool (T::*)(std::span<uint8_t> response) const;

template <typename T>
using MCFP = bool (T::*)(const std::string &args) const;

template <typename T>
using ModuleCommandsMap = std::map <const std::string, MCFP<T>>;

using ModuleSpeedMap = std::map <const std::string, const size_t>;

using SpeedsMapsMap = std::map<const std::string, ModuleSpeedMap*>;

template <typename T>
using CommandsMapsMap = std::map<const std::string, ModuleCommandsMap<T>*>;

///////////////////////////////////////////////////////////////////
//            TEMPLATE INTERFACES DEFINITION                     //
///////////////////////////////////////////////////////////////////


/* ============================================================================================
    generic_module_dispatch
============================================================================================ */

template <typename T>
bool generic_module_list_commands(const T* pOwner, const std::string& strModule)
{
    ModuleCommandsMap<T>* pModCommandsMap = pOwner->getModuleCmdsMap(strModule);

    if (pModCommandsMap && !pModCommandsMap->empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": Available commands:"));

        for (const auto& cmd : *pModCommandsMap) {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(" - "); LOG_STRING(cmd.first));
        }
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": No commands available"));
    }

    return true;

}


/* ============================================================================================
    generic_module_dispatch
============================================================================================ */

template <typename T>
bool generic_module_dispatch (const T *pOwner, 
                              const std::string& strModule, 
                              const std::string& strCmd, 
                              const std::string &args)
{
    ModuleCommandsMap<T>* pMap = pOwner->getModuleCmdsMap(strModule);
    auto it = pMap->find(strCmd);
    if (it != pMap->end()) {
        return (pOwner->*it->second)(args);
    }
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": command not supported:"); LOG_STRING(strCmd));
    return false;
}


/* ============================================================================================
    generic_module_dispatch
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

    // Single-token commands that take no arguments
    if (parts.size() == 1) {
        const std::string& cmd = parts[0];
        if (cmd == "help" || cmd == "mode" || cmd == "exit") {
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

    return generic_module_dispatch<T>(pOwner, strModule, parts[0], parts[1]);
}

/* ============================================================================================
    generic_module_set_speed
============================================================================================ */

template <typename T>
bool generic_module_set_speed (const T *pOwner, const std::string& strModule, const std::string &args)
{
    bool bRetVal = false;
    bool bShowHelp = false;
    const ModuleSpeedMap *pModSpeedMap = pOwner->getModuleSpeedsMap(strModule);

    if (nullptr != pModSpeedMap) {
        if ("help"== args) {
            bShowHelp = true;
            bRetVal = true;
        } else {
            auto itSpeed = pModSpeedMap->find (args);
            if (itSpeed != pModSpeedMap->end()) {
                // dry validation ends here
                if (!pOwner->isEnabled()) {
                    bRetVal = true;
                } else {
                    uint8_t request = 0x60 + ((uint8_t)(itSpeed->second));
                    uint8_t ack_response[sizeof(pOwner->m_positive_response)] = {};
                    bRetVal = pOwner->generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(ack_response), numeric::byte2span(pOwner->m_positive_response));
                }
            } else {
                bShowHelp = true;
            }
        }
    }
    if (true == bShowHelp) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING(strModule); LOG_STRING("available speeds:"));
        std::string strModeList;
        for (const auto &it : *pModSpeedMap) {
            strModeList.clear();
            strModeList += std::to_string(it.second);
            strModeList += " -> ";
            strModeList += it.first;
            LOG_PRINT(LOG_EMPTY, LOG_STRING(strModeList));
        }
    }

    return bRetVal;

} /* generic_module_set_speed() */


/* ============================================================================================
    generic_write_data
============================================================================================ */

template <typename T>
bool generic_write_data (const T *pOwner, const std::string &args, WRITE_DATA_CB<T> pFctWriteCbk)
{
    bool bRetVal = true;

    if ("help" == args) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write 1122BBEFAA.."));
    } else {
        std::vector<uint8_t> data;
        if (true == (bRetVal = hexutils::stringUnhexlify(args, data))) {
            size_t szWriteSize = data.size();

            if ((szWriteSize > 16) || (0 == szWriteSize)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Write too many/less bytes:"); LOG_SIZET(szWriteSize); LOG_STRING("Expected 1..16"));
                bRetVal = false;
            } else {
                bRetVal = (pOwner->*pFctWriteCbk)(data);
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to unhexlify input:"); LOG_STRING(args));
        }
    }

    return bRetVal;

} /* generic_write_data() */


/* ============================================================================================
    generic_execute_script
============================================================================================ */

template <typename T, typename TCommDriver>
bool generic_execute_script(const T *pOwner, const std::string &args)
{
    bool bRetVal = false;
    std::string strScriptPathName;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("generic_execute_script:"); LOG_STRING(args));

    // get the values from the configuration file
    auto *pIniValues = getAccessIniValues(*pOwner);

    // build the artefacts path
    ufile::buildFilePath(pIniValues->strArtefactsPath, args, strScriptPathName);

    // Check file existence and size
    if (false == ufile::fileExistsAndNotEmpty(strScriptPathName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
    } else {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Script:"); LOG_STRING(strScriptPathName));
        try {
            bool bEnabled = getEnabledStatus(*pOwner);

            // construct the driver with the outer reference fulfilled
            auto shpDriver = bEnabled ? std::make_shared<TCommDriver>(*pOwner) : nullptr;

            // check if the driver opened successfully only if the plugin is enabled
            if ( bEnabled && shpDriver && !shpDriver->is_open()) {
                throw std::runtime_error(std::string("UART port") + pIniValues->strUartPort + std::string("is not open"));
            }

            CommScriptClient<TCommDriver> client(
                strScriptPathName,
                shpDriver,
                pIniValues->u32UartReadBufferSize,   // szMaxRecvSize
                pIniValues->u32ReadTimeout,          // u32DefaultTimeout
                pIniValues->u32ScriptDelay           // szDelay
            );

            // run it either in dry validation mode or in real mode depending of bEnabled flag
            bRetVal = client.execute(bEnabled);

        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::runtime_error& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }
    }

    return bRetVal;
}

#endif //BUSPIRATE_GENERIC_HPP