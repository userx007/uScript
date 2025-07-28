#ifndef BUSPIRATE_GENERIC_HPP
#define BUSPIRATE_GENERIC_HPP

#include "uLogger.hpp"

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
using MCFP = bool (T::*)( const char *pstrArgs ) const;

template <typename T>
using ModuleCommandsMap = std::map <const std::string, MCFP<T>>;

using ModuleSpeedMap = std::map <const std::string, const int>;

using SpeedsMapsMap = std::map<const char*, ModuleSpeedMap*>;

template <typename T>
using CommandsMapsMap = std::map<const char*, ModuleCommandsMap<T>*>;

///////////////////////////////////////////////////////////////////
//            TEMPLATE INTERFACES DEFINITION                     //
///////////////////////////////////////////////////////////////////


/* ============================================================================================
    generic_module_dispatch
============================================================================================ */

template <typename T>
bool generic_module_dispatch( const T *pOwner, const char *pstrModule, const char *pstrCmd, const char *pstrArgs )
{
    bool bRetVal = false;

    ModuleCommandsMap<T> *pModCommandsMap = pOwner->getModuleCmdsMap(pstrModule);
    typename ModuleCommandsMap<T>::const_iterator itModule = pModCommandsMap->find( pstrCmd );

    // check if the command is supported by module
    if ( itModule != pModCommandsMap->end() )
    {
        bRetVal = (pOwner->*itModule->second)(pstrArgs);
    }
    else
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrModule); LOG_STRING(": Command"); LOG_STRING(pstrCmd); LOG_STRING("not supported"));
    }

    return bRetVal;

}


/* ============================================================================================
    generic_module_dispatch
============================================================================================ */

template <typename T>
bool generic_module_dispatch( const T *pOwner, const char *pstrModule, const char *pstrArgs )
{
    bool bRetVal = false;

    do {

        std::vector<std::string> vstrArgs;
        string_split(pstrArgs, STRING_SEPARATOR_SPACE, vstrArgs);

        if ( 2 != vstrArgs.size() )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrModule); LOG_STRING("Expected 2 args(cmd args).Abort!"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if( false == pOwner->isEnabled() )
        {
            bRetVal = true;
            break;
        }

        bRetVal = generic_module_dispatch<T>( pOwner, pstrModule, vstrArgs[0].c_str(), vstrArgs[1].c_str() );

    } while(false);

    return bRetVal;

}


/* ============================================================================================
    generic_module_set_speed
============================================================================================ */

template <typename T>
bool generic_module_set_speed( const T *pOwner, const char *pstrModule, const char *pstrArgs )
{
    bool bRetVal = false;
    bool bShowHelp = false;
    const ModuleSpeedMap *pModSpeedMap = pOwner->getModuleSpeedsMap(pstrModule);

    if( nullptr != pModSpeedMap ) {
        if (0 == strcmp("help", pstrArgs)) {
            bShowHelp = true;
            bRetVal = true;
        } else {
            typename ModuleSpeedMap::const_iterator itSpeed = pModSpeedMap->find( pstrArgs );
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
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use:"); LOG_STRING(strModeList.c_str()));
    }

    return bRetVal;

} /* generic_module_set_speed() */


/* ============================================================================================
    generic_write_data
============================================================================================ */

template <typename T>
bool generic_write_data( const T *pOwner, const char *pstrArgs, WRITE_DATA_CB<T> pFctCbk )
{
    bool bRetVal = true;

    if (0 == strcmp("help", pstrArgs)) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write 1122BBEFAA.."));
    } else {
        std::vector<uint8_t> data;
        if( true == (bRetVal = string_unhexlify<uint8_t>(pstrArgs, data)) ) {
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