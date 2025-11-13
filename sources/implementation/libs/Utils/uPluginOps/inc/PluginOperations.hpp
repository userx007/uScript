#ifndef PLUGIN_OPERATIONS_HPP
#define PLUGIN_OPERATIONS_HPP

#include "CommonSettings.hpp"
#include "uBoolExprEvaluator.hpp"
#include "uLogger.hpp"

#include <string>
#include <map>


///////////////////////////////////////////////////////////////////
//                 LOG DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#undef  LOG_HDR
#define LOG_HDR     LOG_STRING("PLUGOPS    :");

///////////////////////////////////////////////////////////////////
//                 EXTERN DATA DECLARATIONS                      //
///////////////////////////////////////////////////////////////////

struct PluginDataGet;

///////////////////////////////////////////////////////////////////
//                 PRIVATE DATA DECLARATIONS                     //
///////////////////////////////////////////////////////////////////

/**
 * \brief template based definition of the command associated callback
 */
template <typename T>
using MFP = bool (T::*)( const std::string & ) const;


/**
 * \brief template based definition of the map containg the pair <cmd_name(string), cmd_function_pointer>
 */
template <typename T>
using PluginCommandsMap = std::map <const std::string, MFP<T>>;


///////////////////////////////////////////////////////////////////
//                 PUBLIC INTERFACES DEFINITIONS                 //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief template based generic do_dispatch implementation
 * \param[in] pOwner pointer to the template type used to access the class private members
 * \param[in] strArgs string containing the arguments list as space separated string
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_dispatch( const T *pOwner, const std::string& strCmd, const std::string& strParams )
{
    bool bRetVal = true;

    // search the command in the plugin's map
    typename PluginCommandsMap<T>::const_iterator itPlugin = pOwner->getMap()->find(strCmd);

    // check if the command is supported by the plugin
    if (itPlugin != pOwner->getMap()->end() ) {
        bool bIsInitialized = pOwner->is_initialized();
        bool bIsFaultTolerant = pOwner->is_fault_tolerant();

        // if either initialized or fault tolerant execute the command
        if ((true == bIsInitialized) || (true == bIsFaultTolerant)) {
            if (false == bIsInitialized) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(strCmd); LOG_STRING(": plugin not initialized but in fault tolerant mode -> run accepted"));
            }
            // execute the command passing to it the arguments resulted in the split above
            bRetVal = (pOwner->*itPlugin->second)(strParams);
            // if fault tolerant then return true even if failed
            if ((false == bRetVal) && (true == bIsFaultTolerant)) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(strCmd); LOG_STRING(": execution failed but in fault tolerant mode -> continue"));
                bRetVal = true;
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Plugin not initialized!"));
            bRetVal = false;
        }
    } else {
        bRetVal = false;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Command"); LOG_STRING(strCmd); LOG_STRING("not supported by plugin"));
    }

    // in fault tolerant mode override the result and let it continue
    if ((false == bRetVal) && (pOwner->is_fault_tolerant())) {
        bRetVal = true;
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed but continue [fault-tolerant mode]"));
    }

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief template based generic implementation of the function used to retrive plugin's parameters
 * \param[in] pOwner pointer to the object instance ( object's "this" pointer )
 * \param[out] psGetParams pointer to a structure where the retrived parameters are stored
 * \return void
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
void generic_getparams ( const T *pOwner, PluginDataGet *psGetParams )
{
    // retrieve the name of the commands supported by plugin (push them in a vector of strings)
    for ( auto i : *pOwner->getMap() ) {
        (psGetParams->vstrPluginCommands).push_back(i.first);
    }

    // retrieve the version of the plugin
    (psGetParams->strPluginVersion).assign(pOwner->getVersion());

}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief template based generic implementation of the function used to set plugin's parameters
 * \param[in] pOwner pointer to the object instance ( object's "this" pointer )
 * \param[out] psSetParams pointer to a structure whith the parameters to be set
 * \return void
*/
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_setparams ( const T *pOwner, const PluginDataSet *psSetParams, bool *pbIsFaultTolerant, bool *pbIsPrivileged )
{
    bool bRetVal = true;

    setLogger(psSetParams->shpLogger);

    if (!psSetParams->mapSettings.empty()) {
        do {
            // try to get value for PLUGIN_INI_FAULT_TOLERANT
            if (psSetParams->mapSettings.count(PLUGIN_INI_FAULT_TOLERANT) > 0) {
                BoolExprEvaluator beEvaluator;
                if (true == (bRetVal = beEvaluator.evaluate(psSetParams->mapSettings.at(PLUGIN_INI_FAULT_TOLERANT), *pbIsFaultTolerant))) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("m_bIsFaultTolerant :"); LOG_BOOL(*pbIsFaultTolerant));
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("failed to evaluate boolean value for"); LOG_STRING(PLUGIN_INI_FAULT_TOLERANT));
                    bRetVal = false;
                    break;
                }
            }

            // try to get value for PRIVILEGED
            if (psSetParams->mapSettings.count(PLUGIN_INI_PRIVILEGED) > 0) {
                BoolExprEvaluator beEvaluator;
                if (true == (bRetVal = beEvaluator.evaluate(psSetParams->mapSettings.at(PLUGIN_INI_PRIVILEGED), *pbIsPrivileged))) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("m_bIsPrivileged :"); LOG_BOOL(*pbIsPrivileged));
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("failed to evaluate boolean value for"); LOG_STRING(PLUGIN_INI_PRIVILEGED));
                    bRetVal = false;
                    break;
                }
            }
        } while(false);

    } else {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("no specific settings in .ini (empty)"));
    }

    return bRetVal;
}


#endif /* PLUGIN_OPERATIONS_HPP */