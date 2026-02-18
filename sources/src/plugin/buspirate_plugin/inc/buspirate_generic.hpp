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
bool generic_module_dispatch (const T *pOwner, const std::string& strModule, const std::string& strCmd, const std::string &args)
{
    bool bRetVal = false;

    ModuleCommandsMap<T> *pModCommandsMap = pOwner->getModuleCmdsMap(strModule);
    typename ModuleCommandsMap<T>::const_iterator itModule = pModCommandsMap->find(strCmd);

    // check if the command is supported by module
    if (itModule != pModCommandsMap->end())
    {
        bRetVal = (pOwner->*itModule->second)(args);
    }
    else
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING(": Command"); LOG_STRING(strCmd); LOG_STRING("not supported"));
    }

    return bRetVal;

}


/* ============================================================================================
    generic_module_dispatch
============================================================================================ */

template <typename T>
bool generic_module_dispatch (const T *pOwner, const std::string& strModule, const std::string &args)
{
    bool bRetVal = false;

    do {

        std::vector<std::string> vstrArgs;
        ustring::splitAtFirst(args, CHAR_SEPARATOR_SPACE, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if ((vstrArgs.size() != 2) && !(vstrArgs.size() == 1 && ((vstrArgs[0] == "help") || (vstrArgs[0] == "mode"))))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING("Expected [help/mode] or [cmd args]"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == pOwner->isEnabled() )
        {
            bRetVal = true;
            break;
        }

        bRetVal = generic_module_dispatch<T> (pOwner, strModule, vstrArgs[0], vstrArgs[1]);

    } while(false);

    return bRetVal;

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
            typename ModuleSpeedMap::const_iterator itSpeed = pModSpeedMap->find (args);
            if ( itSpeed != pModSpeedMap->end() )
            {
                uint8_t request = 0x60 + ((uint8_t)(itSpeed->second));
                bRetVal = pOwner->generic_uart_send_receive(numeric::byte2span(request), std::span<uint8_t>{}, numeric::byte2span(pOwner->m_positive_response));
            } else {
                bShowHelp = true;
            }
        }
    }
    if (true == bShowHelp) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(strModule); LOG_STRING("available speeds:"));
        std::string strModeList;
        for (const auto &it : *pModSpeedMap) {
            strModeList.clear();
            strModeList += std::to_string(it.second);
            strModeList += " -> ";
            strModeList += it.first;
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(strModeList));
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
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write 1122BBEFAA.."));
    } else {
        std::vector<uint8_t> data;
        if (true == (bRetVal = hexutils::stringUnhexlify(args, data))) {
            size_t szWriteSize = data.size();

            if ((szWriteSize > 16) || (0 == szWriteSize)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Write too many/less bytes"); LOG_SIZET(szWriteSize); LOG_STRING("Expected 1..16"));
                bRetVal = false;
            } else {
                bRetVal = (pOwner->*pFctWriteCbk)(data);
            }
        }
    }

    return bRetVal;

} /* generic_write_data() */


/* ============================================================================================
    generic_execute_script
============================================================================================ */

template <typename T>
bool generic_execute_script(const T *pOwner, const std::string &args, WRITE_DATA_CB<T> pFctWriteCbk, READ_DATA_CB<T> pFctReadCbk)
{
    bool bRetVal = false;
    std::string strScriptPathName;

    auto *pIniValues = getAccessIniValues(*pOwner);

    ufile::buildFilePath(pIniValues->strArtefactsPath, args, strScriptPathName);

    // Check file existence and size
    if (false == ufile::fileExistsAndNotEmpty(strScriptPathName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
    } else {
        try {
            // Create UART driver (same pattern as uart_plugin.cpp)
            auto shpDriver = std::make_shared<UART>(pIniValues->strUartPort, pIniValues->u32UartBaudrate);

            // Check if driver opened successfully
            if (shpDriver->is_open()) {
                CommScriptClient<UART> client(
                    strScriptPathName,
                    shpDriver,
                    pIniValues->u32UartReadBufferSize,  // szMaxRecvSize
                    pIniValues->u32ReadTimeout,          // u32DefaultTimeout
                    pIniValues->u32ScriptDelay           // szDelay
                );
                bRetVal = client.execute();
            } else {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open UART port:"); LOG_STRING(pIniValues->strUartPort));
            }
        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }
    }

    return bRetVal;
}

#endif //BUSPIRATE_GENERIC_HPP