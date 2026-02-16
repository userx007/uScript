#ifndef SCRIPTVALIDATOR_HPP
#define SCRIPTVALIDATOR_HPP

#include "IScriptValidator.hpp"
#include "IScriptCommandValidator.hpp"
#include "uScriptCommandValidator.hpp"
#include "uScriptDataTypes.hpp"

#include <string>
#include <vector>
#include <variant>
#include <unordered_map>
#include <memory>



/////////////////////////////////////////////////////////////////////////////////
//                            PUBLIC INTERFACES                                //
/////////////////////////////////////////////////////////////////////////////////


class ScriptValidator : public IScriptValidator<ScriptEntriesType>
{
    public:

        explicit ScriptValidator(std::shared_ptr<IScriptCommandValidator<Token>> shpCommandValidator)
            : m_shpCommandValidator(std::move(shpCommandValidator))
        {}

        bool validateScript(std::vector<std::string>& vstrScriptLines, ScriptEntriesType& sScriptEntries) override;

    private:

        bool m_validateScriptStatements(std::vector<std::string>& vstrScriptLines) noexcept;
        bool m_HandleLoadPlugin ( const std::string& command ) noexcept;
        bool m_HandleConstantMacro ( const std::string& command ) noexcept;
        bool m_HandleVariableMacro ( const std::string& command ) noexcept;
        bool m_HandleCommand ( const std::string& command ) noexcept;
        bool m_HandleCondition ( const std::string& command ) noexcept;
        bool m_HandleLabel ( const std::string& command ) noexcept;

        bool m_preprocessScriptStatements( const std::string& command, const Token token ) noexcept;
        bool m_validateConditions() noexcept;
        bool m_validatePlugins () noexcept;

        bool m_ListStatements () noexcept;

        std::shared_ptr<IScriptCommandValidator<Token>> m_shpCommandValidator;
        ScriptEntriesType *m_sScriptEntries = nullptr;
};

#endif // SCRIPTVALIDATOR_HPP
