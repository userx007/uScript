#ifndef SCRIPTDATATYPES_HPP
#define SCRIPTDATATYPES_HPP

#include <string>
#include <vector>
#include <variant>
#include <unordered_map>

/////////////////////////////////////////////////////////////////////////////////
//                               DATATYPES                                     //
/////////////////////////////////////////////////////////////////////////////////


// forward declaration
struct PluginDataType;

// Tokens type
enum class Token {
    LOAD_PLUGIN,    // LOAD_PLUGIN UART [<= v1.0.1.3]
    CONSTANT_MACRO, // PORT := COM3
    ARRAY_MACRO,    // NAME [= elem1, elem2, ...
    VARIABLE_MACRO, // RESULT ?= UART.READ <args>
    COMMAND,        // UART.WRITE <args>
    IF_GOTO_LABEL,  // IF <cond> GOTO <label>
    LABEL,          // LABEL <label>
    REPEAT,         // REPEAT <label> <count>  |  REPEAT <label> UNTIL <condition>
    END_REPEAT,     // END_REPEAT <label>
    BREAK_LOOP,     // BREAK    <loop-label>
    CONTINUE_LOOP,  // CONTINUE <loop-label>
    PRINT_STMT,     // PRINT <text>
    DELAY_STMT,     // DELAY    <value> <unit>   (us | ms | sec)
    BREAKPOINT_STMT,// BREAKPOINT [label]           (interactive suspend)
    MATH_STMT,      // name ?= MATH <expression>   (arithmetic evaluator)
    VAR_MACRO_INIT, // name ?=  <string value> (direct initialisation)
    FORMAT_STMT,    // name ?= FORMAT input | format_pattern
    INVALID
};

// ---------------------------------------------------------------------------
// Reader output: one entry per non-blank, non-comment line in the source file.
// iLineNumber is the 1-based line number in the original .script file so that
// every downstream component (validator, frontend) can refer back to it.
// ---------------------------------------------------------------------------
struct ScriptRawLine {
    int         iLineNumber = 0;
    std::string strContent;
};

struct MacroCommand {
    std::string strPlugin;
    std::string strCommand;
    std::string strParams;
    std::string strVarMacroName;
};

struct Command {
    std::string strPlugin;
    std::string strCommand;
    std::string strParams;
};

struct Condition {
    std::string strCondition;
    std::string strLabelName;
};

struct Label {
    std::string strLabelName;
};

// Repeat <count> times; body is delimited by the matching RepeatEnd with the same label.
// strVarMacroName: if non-empty, the current 0-based iteration index is written to this
// variable macro at the start of each iteration and is accessible via $strVarMacroName.
struct RepeatTimes {
    std::string strLabel;
    int         iCount;             // number of iterations (>= 1)
    std::string strCountExpr;       // raw "$macroname" — empty for literal counts
    std::string strVarMacroName;    // iteration-index capture macro (empty = no capture)
};

// Repeat until <condition> becomes true (do-while semantics: body always runs at least once).
// The condition is evaluated at END_REPEAT after each iteration.
// strVarMacroName: if non-empty, an internal 0-based iteration counter is written to this
// variable macro at the start of each iteration and is accessible via $strVarMacroName.
struct RepeatUntil {
    std::string strLabel;
    std::string strCondition;       // raw expression (may contain $macros, expanded at run time)
    std::string strVarMacroName;    // iteration-counter capture macro (empty = no capture)
};

// Closing marker shared by both REPEAT counted and REPEAT UNTIL.
struct RepeatEnd {
    std::string strLabel;
};

// BREAK <loop-label>
// Immediately exits the named enclosing loop. All loops between the current
// innermost and the named target are also unwound (their LoopStates are popped).
struct LoopBreak {
    std::string strLabel;       // label of the enclosing loop to exit
};

// CONTINUE <loop-label>
// Skips the remainder of the current body and resumes at END_REPEAT of the
// named enclosing loop, which runs its normal exit-or-loop-back logic.
// All loops between the current innermost and the target are also unwound.
struct LoopContinue {
    std::string strLabel;       // label of the enclosing loop to continue
};

// PRINT <text>
// Native print statement — no plugin required.
// The text is stored verbatim (with $macros unexpanded); macro substitution
// is performed at runtime immediately before output, so volatile macro values
// and loop index macros are always reflected correctly.
// An empty PRINT (bare keyword with no text) prints a blank line.
struct PrintStatement {
    std::string strText;        // raw text template (may contain $macros)
};

// name ?= <string value>
// Direct variable macro initialisation — no plugin command involved.
// The value template is stored verbatim; $macro substitution is performed at
// execution time so that the initial value can reference other macros, loop
// indices, or array elements (e.g.  done ?= FALSE,  copy ?= $other,
// first ?= $ARRAY.$0).
// An empty value is valid and initialises the macro to an empty string.
// Like MacroCommand, writes to m_RuntimeVarMacros at execution time, so the
// value is immediately visible to all subsequent $macro lookups.
struct VarMacroInit {
    std::string strName;        // macro name (identifier)
    std::string strValueTpl;    // raw value template (may contain $macros)
};

// name ?= FORMAT input | format_pattern
// Pure built-in string formatting — no plugin involved.
// Tokenises the (already macro-expanded) input by whitespace into items[0..N],
// then walks the format template substituting every %N placeholder with the
// corresponding item.  Items may be reordered, repeated, or omitted freely.
// Both the input and the format template may contain $macros; expansion is
// deferred to execution time.
// Stores the result string in m_RuntimeVarMacros[strName].
struct FormatStatement {
    std::string strName;        // destination macro name (identifier)
    std::string strInputTpl;    // raw input template   (may contain $macros)
    std::string strFormatTpl;   // raw format template  (may contain $macros and %N)
};

// Time unit for a DELAY statement.
enum class DelayUnit { US, MS, SEC };

// DELAY <value> <unit>
// Native busy-wait / sleep — no plugin required.
// The value and unit are fully resolved at validation time; the interpreter
// simply calls the appropriate utime::delay_* function.
// Syntax:   DELAY 300 ms   |   DELAY 50 us   |   DELAY 2 sec
struct DelayStatement {
    size_t    szValue;   // delay amount (>= 1)
    DelayUnit eUnit;     // US | MS | SEC
};

// name ?= MATH <expression>
// Native arithmetic evaluator — no plugin required.
// The expression template is stored verbatim; $macro substitution is performed
// at execution time so that variable macro values and loop indices are always
// current.  After expansion the resulting string is fed to Calculator::evaluate()
// and the returned double is converted to a string and stored in
// m_RuntimeVarMacros[strName].
//
// The expression may use the full Calculator syntax: +, -, *, /, //, %, **,
// comparison and logical operators, bitwise operators, the ternary operator,
// all built-in functions (sin, cos, sqrt, abs, min, max, …) and the constants
// pi, e, tau, phi, inf, nan.
// Variable assignments inside the expression (e.g. MATH x = 3 + 2) also work
// and are persisted in the shared Calculator variable map.
//
// Syntax:   result ?= MATH 2 + 3
//           result ?= MATH $x * $y + 1
//           result ?= MATH sqrt($val) + pi
struct MathStatement {
    std::string strName;       // destination macro name (identifier)
    std::string strExprTpl;    // raw expression template (may contain $macros)
    bool        bHexOutput = false;
};

// BREAKPOINT [label]
// Native interactive suspend — no plugin required.
// Halts script execution at this point and waits for user input via
// CheckContinue.  An optional label string is displayed in the log prompt
// to identify which breakpoint was hit.  $macros in the label are expanded
// at execution time so loop indices and variable values are current.
//
// User responses:
//   a/A + y/Y  → abort: command returns false → script execution fails
//   Space      → skip this breakpoint, continue normally (bSkip = true)
//   any other  → continue normally
//
// During the dry-run validation pass the node is silently skipped.
// Inside a GOTO/BREAK/CONTINUE skip region it is also transparent.
struct BreakpointStatement {
    std::string strLabelTpl;  // optional label template (may contain $macros; may be empty)
};

// ---------------------------------------------------------------------------
// IR command entry: pairs every compiled command with the 1-based source line
// it was read from.  Keeping the line number in the wrapper (rather than in
// every individual IR struct) means visitors and execution logic are unchanged
// and the frontend can always read iLineNumber without visiting the variant.
// ---------------------------------------------------------------------------
using ScriptCommandType = std::variant<MacroCommand, Command, Condition, Label,
                                       RepeatTimes, RepeatUntil, RepeatEnd,
                                       LoopBreak, LoopContinue, PrintStatement,
                                       VarMacroInit, FormatStatement, DelayStatement,
                                       MathStatement, BreakpointStatement>;

struct ScriptLine {
    int               iLineNumber = 0;
    ScriptCommandType command;
};

using CommandsStorageType   = std::vector<ScriptLine>;
using MacroStorageType      = std::unordered_map<std::string, std::string>;
using PluginStorageType     = std::vector<PluginDataType>;

// Array macros: NAME [= elem0, elem1, ...
// Stored as a map of name → element vector so elements are accessible via
// the $NAME.$index_macro syntax at runtime.
using ArrayMacroStorageType = std::unordered_map<std::string, std::vector<std::string>>;

struct ScriptEntries {
    PluginStorageType     vPlugins;
    MacroStorageType      mapMacros;
    ArrayMacroStorageType mapArrayMacros;
    CommandsStorageType   vCommands;
};

using ScriptEntriesType = ScriptEntries;

/////////////////////////////////////////////////////////////////////////////////
//                 DATATYPES LOGGING SUPPORT (type to string)                  //
/////////////////////////////////////////////////////////////////////////////////

inline const std::string& getTokenTypeName(Token type)
{
    switch(type)
    {
        case Token::LOAD_PLUGIN:    { static const std::string name = "LOAD_PLUGIN";    return name; }
        case Token::CONSTANT_MACRO: { static const std::string name = "CONST_MACRO";    return name; }
        case Token::ARRAY_MACRO:    { static const std::string name = "ARRAY_MACRO";    return name; }
        case Token::VARIABLE_MACRO: { static const std::string name = "VAR_MACRO";      return name; }
        case Token::COMMAND:        { static const std::string name = "COMMAND";        return name; }
        case Token::IF_GOTO_LABEL:  { static const std::string name = "IF_GOTO_LABEL";  return name; }
        case Token::LABEL:          { static const std::string name = "LABEL";          return name; }
        case Token::REPEAT:         { static const std::string name = "REPEAT";         return name; }
        case Token::END_REPEAT:     { static const std::string name = "END_REPEAT";     return name; }
        case Token::BREAK_LOOP:     { static const std::string name = "BREAK";          return name; }
        case Token::CONTINUE_LOOP:  { static const std::string name = "CONTINUE";       return name; }
        case Token::PRINT_STMT:     { static const std::string name = "PRINT";          return name; }
        case Token::DELAY_STMT:     { static const std::string name = "DELAY";          return name; }
        case Token::BREAKPOINT_STMT:{ static const std::string name = "BREAKPOINT";     return name; }
        case Token::MATH_STMT:      { static const std::string name = "MATH";           return name; }
        case Token::VAR_MACRO_INIT: { static const std::string name = "VAR_MACRO_INIT"; return name; }
        case Token::FORMAT_STMT:    { static const std::string name = "FORMAT";         return name; }
        case Token::INVALID:        { static const std::string name = "INVALID";        return name; }
        default:                    { static const std::string name = "UNKNOWN";        return name; }
    }
}

#endif // SCRIPTDATATYPES_HPP