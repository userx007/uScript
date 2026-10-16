#ifndef U_SCRIPT_VALIDATOR_HPP
#define U_SCRIPT_VALIDATOR_HPP
#include "IScriptCommandValidator.hpp"
#include "IScriptValidator.hpp"
#include "uScriptCommandValidator.hpp"
#include "uScriptDataTypes.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

template <typename TCommand>
class IScriptCommandValidator;

/////////////////////////////////////////////////////////////////////////////////
//                            PUBLIC INTERFACES                                //
/////////////////////////////////////////////////////////////////////////////////

class ScriptValidator : public IScriptValidator<ScriptEntriesType>
{
    public:
        explicit ScriptValidator(std::shared_ptr<IScriptCommandValidator<Token>> shpCommandValidator)
            : m_shpCommandValidator(std::move(shpCommandValidator))
        {
        }

        bool validateScript(std::vector<ScriptRawLine> &vRawLines, ScriptEntriesType &sScriptEntries) override;

    private:
        bool m_validateScriptStatements(std::vector<ScriptRawLine> &vRawLines) noexcept;
        bool m_HandleLoadPlugin(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleConstantMacro(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleArrayMacro(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleVariableMacro(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleVarMacroInit(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleFormatStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleMathStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleBitstreamStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleBytestreamStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleBitstreamValStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleBytestreamValStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleStreamValStmt(const ScriptRawLine &sRawLine, const std::string &strKeyword, bool bByteMode) noexcept;
        bool m_HandleBitstreamValArrayStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleBytestreamValArrayStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleStreamValArrayStmt(const ScriptRawLine &sRawLine, const std::string &strKeyword, bool bByteMode) noexcept;

        // Shared implementation behind both of the above (see uStreamStatementParser.hpp).
        bool m_HandleStreamStmt(const ScriptRawLine &sRawLine, const std::string &strKeyword, bool bByteMode) noexcept;
        bool m_HandleCommand(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleCondition(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleLabel(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleRepeat(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleEndRepeat(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleBreak(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleContinue(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandlePrint(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleDelay(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleBreakpoint(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleGeneratorStmt(const ScriptRawLine &sRawLine) noexcept;
        bool m_HandleKill(const ScriptRawLine &sRawLine) noexcept;

        bool m_preprocessScriptStatements(const ScriptRawLine &sRawLine, const Token eToken) noexcept;
        bool m_validateConditions() noexcept;
        bool m_validateLoops() noexcept;
        bool m_validatePlugins() noexcept;

        // Scans every compiled command's raw $macro templates for NAME.SIZE
        // references and fails validation if NAME is not a declared array
        // macro (mapArrayMacros). Runs after m_validateScriptStatements so
        // that all ARRAY_MACRO declarations — regardless of their position
        // in the file — are already known.
        bool m_validateArraySizeUsage() noexcept;

        // Walks the compiled command list in source (textual) order, collecting
        // the names of every VOLATILE macro that has something running behind it:
        //   - the destination of a threaded variable-capture command
        //     ("name ?= PLUGIN.CMD args &" - this includes "name ?= UART.CYCLIC ... &");
        //   - the destination of a GENERATOR ("name ?= GENERATOR ...").
        // and fails validation on a "[ret ?=] KILL name" whose name has not been
        // launched that way on an earlier line. That catches typos and, above all,
        // an attempt to kill something that does not return a volatile macro:
        // a command launched sequentially (no '&') has already finished, and a
        // command that captures nothing ("PLUGIN.CMD args &") has no name to be
        // killed by - there is no way to kill it short of the end of the script.
        // Unlike a start/stop pairing check a KILL does NOT consume the name:
        // a second KILL of the same name (e.g. on a cleanup path reachable both
        // after a normal KILL and via a GOTO that skipped it) is legitimate and
        // simply stops nothing at run time (result "0"). Being a textual-order
        // check it does not simulate IF/GOTO flow. Runs after
        // m_validateScriptStatements() so the full command list already exists.
        bool m_validateKillTargets() noexcept;

        bool m_ListStatements() noexcept;

        // Parses a comma-separated element list (the part after [=).
        // Elements may be quoted with " to include commas inside them.
        // Leading/trailing whitespace of each element is trimmed.
        // Quoted delimiters are stripped from the stored value.
        static bool m_parseArrayElements(const std::string &strList,
                                         std::vector<std::string> &vElements) noexcept;

        std::shared_ptr<IScriptCommandValidator<Token>> m_shpCommandValidator;
        ScriptEntriesType *m_sScriptEntries = nullptr;
        int m_iCurrentSourceLine            = 0; // source line of the statement being compiled
};

#endif // U_SCRIPT_VALIDATOR_HPP
