#ifndef PLUGINSCRIPTVALIDATOR_HPP
#define PLUGINSCRIPTVALIDATOR_HPP

#include "IScriptValidator.hpp"
#include "IItemValidator.hpp"
#include "PluginScriptItemValidator.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            PUBLIC INTERFACES                                //
/////////////////////////////////////////////////////////////////////////////////


class PluginScriptValidator : public IScriptValidator
{
    public:

        explicit PluginScriptValidator(std::shared_ptr<IItemValidator<PToken>> shpItemValidator)
            : m_shpItemValidator(std::move(shpItemValidator))
        {}

        bool validateScript(std::vector<std::string>& vstrScriptLines, ScriptEntriesType& sScriptEntries) override
        {
            PToken token;

            m_sScriptEntries = &sScriptEntries;

            return std::all_of(vstrScriptLines.begin(), vstrScriptLines.end(),
                [&](std::string& command) {
                    if (!m_shpItemValidator->validateItem(command, token)) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate ["); LOG_STRING(command); LOG_STRING("]"));
                        return false;
                    }
                    return m_preprocessScriptItems(command, token);
                });
        }

    private:

        // placeholder for further extensions
        bool m_preprocessScriptItems( const std::string& command, const PToken token ) noexcept
        {
            return true;
        }

        std::shared_ptr<IItemValidator<PToken>> m_shpItemValidator;
        ScriptEntriesType *m_sScriptEntries = nullptr;

};

#endif //PLUGINSCRIPTVALIDATOR_HPP