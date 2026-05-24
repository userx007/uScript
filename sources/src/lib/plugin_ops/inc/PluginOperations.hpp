#ifndef PLUGIN_OPERATIONS_HPP
#define PLUGIN_OPERATIONS_HPP

#include "uSharedConfig.hpp"
#include "uBoolEvaluator.hpp"
#include "uLogger.hpp"

#include <string>
#include <map>
#include <stop_token>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "PLUGIN_OPS  |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                 EXTERN DATA DECLARATIONS                      //
///////////////////////////////////////////////////////////////////

struct PluginDataGet;

///////////////////////////////////////////////////////////////////
//                 PRIVATE DATA DECLARATIONS                     //
///////////////////////////////////////////////////////////////////

/**
 * \brief template based definition of the command handler function pointer
 */
template <typename T>
using MFP = bool (T::*)( const std::string&, std::stop_token ) const;


/**
 * \brief Per-command entry: handler function pointer + blocking flag.
 *
 * bBlocking = true  → command may run indefinitely (endless loop).
 *             The script engine enforces that it must be launched with '&'.
 * bBlocking = false → command always returns in finite time (default).
 */
template <typename T>
struct PluginCommandEntry {
    MFP<T> handler;
    bool   bBlocking;
};


/**
 * \brief Map of command name → PluginCommandEntry
 */
template <typename T>
using PluginCommandsMap = std::map<const std::string, PluginCommandEntry<T>>;


///////////////////////////////////////////////////////////////////
//                 PUBLIC INTERFACES DEFINITIONS                 //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief template based generic doDispatch implementation
 *
 * The stop_token st is forwarded to the command handler so that blocking
 * commands can poll st.stop_requested() and exit their loop cooperatively.
 * For sequential (non-threaded) calls the default-constructed token is passed
 * whose stop_requested() always returns false — existing behaviour unchanged.
 */
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_dispatch( const T *pOwner, const std::string& strCmd,
                       const std::string& strParams, std::stop_token st = {} )
{
    bool bRetVal = true;

    typename PluginCommandsMap<T>::const_iterator itPlugin = pOwner->getMap()->find(strCmd);

    if (itPlugin != pOwner->getMap()->end()) {
        bool bIsInitialized   = pOwner->isInitialized();
        bool bIsFaultTolerant = pOwner->isFaultTolerant();

        if ((true == bIsInitialized) || (true == bIsFaultTolerant)) {
            if (false == bIsInitialized) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(strCmd);
                    LOG_STRING(": Plugin not initialized but in fault tolerant mode -> run accepted"));
            }
            // Forward stop_token to the handler
            bRetVal = (pOwner->*(itPlugin->second.handler))(strParams, st);

            if ((false == bRetVal) && (true == bIsFaultTolerant)) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(strCmd);
                    LOG_STRING(": Execution failed but in fault tolerant mode -> continue"));
                bRetVal = true;
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Plugin not initialized!"));
            bRetVal = false;
        }
    } else {
        bRetVal = false;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Command");
            LOG_STRING(strCmd); LOG_STRING("not supported by plugin"));
    }

    if ((false == bRetVal) && (pOwner->isFaultTolerant())) {
        bRetVal = true;
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed but continue [fault-tolerant mode]"));
    }

    return bRetVal;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief template based generic implementation of getParams.
 *
 * Populates both vstrPluginCommands (as before) and mapBlockingCommands
 * so the interpreter can detect at validation time if a blocking command
 * was scheduled without the '&' threading suffix.
 */
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
void generic_getparams( const T *pOwner, PluginDataGet *psGetParams )
{
    for (const auto& entry : *pOwner->getMap()) {
        psGetParams->vstrPluginCommands.push_back(entry.first);
        if (entry.second.bBlocking) {
            psGetParams->mapBlockingCommands.emplace(entry.first, true);
        }
    }
    psGetParams->strPluginVersion.assign(pOwner->getVersion());
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief template based generic setParams — unchanged from original
 */
/*--------------------------------------------------------------------------------------------------------*/

template <typename T>
bool generic_setparams( const T *pOwner, const PluginDataSet *psSetParams,
                        bool *pbIsFaultTolerant, bool *pbIsPrivileged )
{
    bool bRetVal = true;

    setLogger(psSetParams->shpLogger);

    if (!psSetParams->mapSettings.empty()) {
        do {
            if (psSetParams->mapSettings.count(PLUGIN_INI_FAULT_TOLERANT) > 0) {
                BoolExprEvaluator beEvaluator;
                if (true == (bRetVal = beEvaluator.evaluate(
                        psSetParams->mapSettings.at(PLUGIN_INI_FAULT_TOLERANT),
                        *pbIsFaultTolerant))) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("FaultTolerant :");
                        LOG_BOOL(*pbIsFaultTolerant));
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                        LOG_STRING("Failed to evaluate boolean value for");
                        LOG_STRING(PLUGIN_INI_FAULT_TOLERANT));
                    bRetVal = false;
                    break;
                }
            }

            if (psSetParams->mapSettings.count(PLUGIN_INI_PRIVILEGED) > 0) {
                BoolExprEvaluator beEvaluator;
                if (true == (bRetVal = beEvaluator.evaluate(
                        psSetParams->mapSettings.at(PLUGIN_INI_PRIVILEGED),
                        *pbIsPrivileged))) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Privileged :");
                        LOG_BOOL(*pbIsPrivileged));
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                        LOG_STRING("Failed to evaluate boolean value for");
                        LOG_STRING(PLUGIN_INI_PRIVILEGED));
                    bRetVal = false;
                    break;
                }
            }
        } while(false);
    } else {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("No specific settings in .ini (empty)"));
    }

    return bRetVal;
}


#endif /* PLUGIN_OPERATIONS_HPP */