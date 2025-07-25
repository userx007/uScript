#ifndef PLUGINSCRIPTVALIDATOR_HPP
#define PLUGINSCRIPTVALIDATOR_HPP

#include "IScriptValidator.hpp"
#include "IScriptItemValidator.hpp"
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

        explicit PluginScriptValidator(std::shared_ptr<IScriptItemValidator<PToken>> shpItemValidator)
            : m_shpItemValidator(std::move(shpItemValidator))
        {}

        bool validateScript(std::vector<std::string>& vstrScriptLines, PluginScriptEntriesType& sScriptEntries) override
        {
            PToken token;
            m_sScriptEntries = &sScriptEntries;

            bool bRetVal = std::all_of(vstrScriptLines.begin(), vstrScriptLines.end(),
                [&](std::string& item) {

                    // replace the macros declared so far
                    ustring::replaceMacros(item, m_sScriptEntries->mapMacros, SCRIPT_MACRO_MARKER);

                    // validate as macro
                    if (true == m_isConstantMacro(item)) {
                        std::vector<std::string> vstrTokens;
                        ustring::tokenize(item, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);
                        m_sScriptEntries->mapMacros.emplace(vstrTokens[0], vstrTokens[1]);
                        return true;
                    }

                    // validate as command
                    if (true == m_shpItemValidator->validateItem(item, token)) {
                        m_sScriptEntries->vCommands.emplace_back(token);
                        return true;
                    }

                    // none of expected
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate ["); LOG_STRING(item); LOG_STRING("]"));
                    return false;

                });

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));
            return bRetVal;
        }

    private:

        bool m_isConstantMacro(const std::string& expression )
        {
            static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*:=\s*\S.*$)");
            return std::regex_match(expression, pattern);
        }

        std::shared_ptr<IScriptItemValidator<PToken>> m_shpItemValidator;
        PluginScriptEntriesType *m_sScriptEntries = nullptr;

};

#endif //PLUGINSCRIPTVALIDATOR_HPP