#ifndef PLUGINSCRIPTVALIDATOR_HPP
#define PLUGINSCRIPTVALIDATOR_HPP

#include "IScriptValidator.hpp"
#include "IItemValidator.hpp"
#include "PluginScriptDataTypes.hpp"
#include "PluginScriptItemValidator.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "PSVALIDATOR:"
#define LOG_HDR    LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

class PluginScriptValidator : public IScriptValidator<PluginScriptEntriesType>
{
    public:

        explicit PluginScriptValidator(std::shared_ptr<IItemValidator<PToken>> shpItemValidator)
            : m_shpItemValidator(std::move(shpItemValidator))
        {}

        bool validateScript(std::vector<std::string>& vstrScriptLines, PluginScriptEntriesType& sScriptEntries) override
        {
            PToken token;
            m_sScriptEntries = &sScriptEntries;

            bool bRetVal = std::all_of(vstrScriptLines.begin(), vstrScriptLines.end(),
                [&](std::string& command) {
                    if (false == m_shpItemValidator->validateItem(command, token)) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate ["); LOG_STRING(command); LOG_STRING("]"));
                        return false;
                    }
                    return m_preprocessScriptItems(command, token);
                });

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));
            return bRetVal;
        }

    private:

        // placeholder for further extensions
        bool m_preprocessScriptItems( const std::string& command, const PToken token ) noexcept
        {
            return true;
        }

        std::shared_ptr<IItemValidator<PToken>> m_shpItemValidator;
        PluginScriptEntriesType *m_sScriptEntries = nullptr;

};

#endif //PLUGINSCRIPTVALIDATOR_HPP