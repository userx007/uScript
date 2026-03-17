#ifndef U_SCRIPT_VALIDATOR_HPP
#define U_SCRIPT_VALIDATOR_HPP

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

        bool validateScript(std::vector<ScriptRawLine>& vRawLines, ScriptEntriesType& sScriptEntries) override;

    private:

        bool m_validateScriptStatements(std::vector<ScriptRawLine>& vRawLines) noexcept;
        bool m_HandleLoadPlugin    ( const std::string& command ) noexcept;
        bool m_HandleConstantMacro ( const std::string& command ) noexcept;
        bool m_HandleArrayMacro    ( const std::string& command ) noexcept;
        bool m_HandleVariableMacro ( const std::string& command ) noexcept;
        bool m_HandleVarMacroInit  ( const std::string& command ) noexcept;
        bool m_HandleFormatStmt    ( const std::string& command ) noexcept;
        bool m_HandleCommand       ( const std::string& command ) noexcept;
        bool m_HandleCondition     ( const std::string& command ) noexcept;
        bool m_HandleLabel         ( const std::string& command ) noexcept;
        bool m_HandleRepeat        ( const std::string& command ) noexcept;
        bool m_HandleEndRepeat     ( const std::string& command ) noexcept;
        bool m_HandleBreak         ( const std::string& command ) noexcept;
        bool m_HandleContinue      ( const std::string& command ) noexcept;
        bool m_HandlePrint         ( const std::string& command ) noexcept;

        bool m_preprocessScriptStatements( const std::string& command, const Token token ) noexcept;
        bool m_validateConditions() noexcept;
        bool m_validateLoops()      noexcept;
        bool m_validatePlugins ()   noexcept;

        bool m_ListStatements () noexcept;

        // Parses a comma-separated element list (the part after [=).
        // Elements may be quoted with " to include commas inside them.
        // Leading/trailing whitespace of each element is trimmed.
        // Quoted delimiters are stripped from the stored value.
        static bool m_parseArrayElements( const std::string& strList,
                                          std::vector<std::string>& vElements ) noexcept;

        std::shared_ptr<IScriptCommandValidator<Token>> m_shpCommandValidator;
        ScriptEntriesType *m_sScriptEntries    = nullptr;
        int                m_iCurrentSourceLine = 0;    // source line of the statement being compiled
};

#endif // U_SCRIPT_VALIDATOR_HPP
