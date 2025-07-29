#ifndef BUSPIRATE_GENERIC_HPP
#define BUSPIRATE_GENERIC_HPP

#include "CommonSettings.hpp"

#include "uLogger.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"

#include <vector>
#include <cstring>
#include <iostream>
#include <map>
#include <functional>


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
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

#define BP_WRITE_MAX_CHUNK_SIZE ((int)(4096U))

template <typename T>
using WRITE_DATA_CB = bool (T::*)(const uint8_t*, const int) const;

template <typename T>
using MCFP = bool (T::*)(const std::string &args) const;

template <typename T>
using ModuleCommandsMap = std::map <const std::string, MCFP<T>>;

using ModuleSpeedMap = std::map <const std::string, const int>;

using SpeedsMapsMap = std::map<const std::string, ModuleSpeedMap*>;

template <typename T>
using CommandsMapsMap = std::map<const char *, ModuleCommandsMap<T>*>;

///////////////////////////////////////////////////////////////////
//            TEMPLATE INTERFACES DEFINITION                     //
///////////////////////////////////////////////////////////////////


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
    if ( itModule != pModCommandsMap->end() )
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

        if ( 2 != vstrArgs.size() )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strModule); LOG_STRING("Expected 2 args(cmd args).Abort!"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if( false == pOwner->isEnabled() )
        {
            bRetVal = true;
            break;
        }

        bRetVal = generic_module_dispatch<T>( pOwner, strModule, vstrArgs[0].c_str(), vstrArgs[1].c_str() );

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

    if( nullptr != pModSpeedMap ) {
        if ("help"== args) {
            bShowHelp = true;
            bRetVal = true;
        } else {
            typename ModuleSpeedMap::const_iterator itSpeed = pModSpeedMap->find( args );
            if ( itSpeed != pModSpeedMap->end() )
            {
                char request = 0x60 + ((char)(itSpeed->second));
                char answer  = 0x01 ;
                bRetVal = pOwner->generic_uart_send_receive(&request, sizeof(request), &answer, sizeof(answer));
            } else {
                bShowHelp = true;
            }
        }
    }
    if( true == bShowHelp )
    {
        std::string strModeList;
        for( const auto &it : *pModSpeedMap ) {
            strModeList += it.first;
            strModeList += " -> ";
            strModeList += std::to_string(it.second);
            strModeList += " | ";
        }
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use:"); LOG_STRING(strModeList));
    }

    return bRetVal;

} /* generic_module_set_speed() */


/* ============================================================================================
    generic_write_data
============================================================================================ */

template <typename T>
bool generic_write_data (const T *pOwner, const std::string &args, WRITE_DATA_CB<T> pFctCbk)
{
    bool bRetVal = true;

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write 1122BBEFAA.."));
    } else {
        std::vector<uint8_t> data;
        if( true == (bRetVal = hexutils::stringUnhexlify(args, data)) ) {
            uint8_t u8WriteBytes = (uint8_t)(data.size());

            if ((u8WriteBytes > 16) || (0 == u8WriteBytes) ) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Write::too many/less bytes"); LOG_UINT8(u8WriteBytes); LOG_STRING("Expected 1..16"));
                bRetVal = false;
            } else {
                bRetVal = (pOwner->*pFctCbk)(data.data(),(int)data.size());
            }
        }
    }

    return bRetVal;

} /* generic_write_data() */

#endif //BUSPIRATE_GENERIC_HPP