#ifndef U_SCRIPT_SYNTAX_HPP
#define U_SCRIPT_SYNTAX_HPP


#include <string>
#include <regex>
#include "uSharedScriptRegex.hpp"

namespace usyntax
{

// validate a load plugin expression
// Supports plain names (UART) and instanced names (UART:1, UART:2, …).
// The instance suffix :N is a positive integer with no leading zeros.
inline bool m_isLoadPlugin(const std::string& expression )
{
    static const std::regex pattern(
        "^LOAD_PLUGIN\\s+" SCRIPT_RX_PLUGIN_TYPE_NAME SCRIPT_RX_INSTANCE_SUFFIX
        "\\s*(\\s+(<=|<|>=|>|==)\\s+" SCRIPT_RX_LOAD_PLUGIN_VERSION ")?$");
    return std::regex_match(expression, pattern);
}

// validate a constant macro expression
inline bool m_isConstantMacro(const std::string& expression )
{
    static const std::regex pattern("^" SCRIPT_RX_IDENT "\\s*:=\\s*\\S.*$");
    return std::regex_match(expression, pattern);
}

// validate an array macro expression:  NAME [= elem1, elem2, ...
// At least one element (non-empty content after [=) is required.
inline bool m_isArrayMacro(const std::string& expression)
{
    static const std::regex pattern("^" SCRIPT_RX_IDENT "\\s*\\[=\\s*\\S.*$");
    return std::regex_match(expression, pattern);
}

// validate a variable macro expression
// Supports plain plugin names and instanced names (UART:1.READ).
inline bool m_isVariableMacro(const std::string& expression )
{
    static const std::regex pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*" SCRIPT_RX_UPPER_IDENT SCRIPT_RX_INSTANCE_SUFFIX
        "\\.(" SCRIPT_RX_UPPER_IDENT ").*$");
    return std::regex_match(expression, pattern);
}

// validate a direct variable macro initialisation:  name ?= <string value>
// This form is recognised only when VARIABLE_MACRO does NOT match — i.e. the
// right-hand side is not a PLUGIN.COMMAND pattern.  The value may be empty
// (bare "name ?=") which initialises the macro to an empty string.
inline bool m_isVarMacroInit(const std::string& expression)
{
    static const std::regex pattern("^" SCRIPT_RX_IDENT "\\s*\\?=(\\s.*)?$");
    return std::regex_match(expression, pattern);
}

// validate a FORMAT statement:  name ?= FORMAT input | format_pattern
//
// Syntax:
//   <identifier> ?= FORMAT <input_text> | <format_template>
//
// Both <input_text> and <format_template> may contain $macros (expanded at
// runtime).  The pipe character '|' is mandatory and separates the two
// operands.  At least one non-whitespace character must follow FORMAT.
//
// Examples (after $macro expansion):
//   out ?= FORMAT Hello world from Paris | I salute from %3 to the %1 with %0
//   out ?= FORMAT $words | %2 %1 %0
inline bool m_isFormatStmt(const std::string& expression)
{
    // name ?= FORMAT <something> | <something>
    // Both sides of | must have at least one non-ws character.
    static const std::regex pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*FORMAT\\s+\\S[^|]*\\|\\s*\\S.*$");
    return std::regex_match(expression, pattern);
}

// validate a MATH statement:  name ?= MATH <expression>
//
// Syntax:
//   <identifier> ?= MATH <arithmetic-expression>
//
// The expression may contain $macros (expanded at runtime before evaluation),
// numbers, operators, and built-in functions supported by Calculator.
// At least one non-whitespace character must follow MATH.
//
// Examples:
//   result ?= MATH 2 + 3
//   result ?= MATH $x * $y + 1
//   result ?= MATH sqrt($val) + pi
//   result ?= MATH ($a + $b) / 2
inline bool m_isMathStmt(const std::string& expression)
{
    static const std::regex pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*MATH\\s+\\S.*$");
    return std::regex_match(expression, pattern);
}

// validate simple command
// Supports plain plugin names (UART.SCRIPT) and instanced names (UART:1.SCRIPT).
inline bool m_isCommand(const std::string& expression )
{
    static const std::regex pattern(
        "^" SCRIPT_RX_UPPER_IDENT SCRIPT_RX_INSTANCE_SUFFIX "\\.(" SCRIPT_RX_UPPER_IDENT ")\\s*.*$");
    return std::regex_match(expression, pattern);
}

// validate "IF .. GOTO .." or "GOTO .." conditions
inline bool m_isIfGoToCondition(const std::string& expression)
{
    static const std::regex pattern("^(?:IF\\s+\\S(?:.*\\S)?\\s+)?GOTO\\s+" SCRIPT_RX_IDENT "$");
    return std::regex_match(expression, pattern);
}

// validate LABEL
inline bool m_isLabel(const std::string& expression )
{
    static const std::regex pattern("^LABEL\\s+" SCRIPT_RX_IDENT "$");
    return std::regex_match(expression, pattern);
}

// validate REPEAT <label> <end>
//       or REPEAT <label> <begin>, <end>
//       or REPEAT <label> <begin>, <end>, <step>
//       or REPEAT <label> UNTIL <condition>
// and their index-capture forms:  varname ?= REPEAT <label> <.. / UNTIL cond>
// Both forms share the same token; the handler in the validator distinguishes them.
//
// Each of <begin>/<end>/<step> may be:
//   - a signed decimal integer            (123, -7)
//   - a signed hex / binary / octal integer (0x1F, 0b1010, 0o17 — any sign, any size)
//   - a signed decimal floating-point value (3.14, -0.5, 1e9)
//   - a "$macroname" reference, resolved at runtime
//   - a "$arrayname.SIZE" reference — the element count of a declared
//     ARRAY_MACRO, resolved at runtime by the same "$macroname" machinery
//     (ScriptInterpreter::m_replaceVariableMacros() special-cases the .SIZE
//     suffix). arrayname must be declared with "NAME [= elem1, elem2, ..."
//     somewhere in the script (forward references are fine — the whole file
//     is parsed before any array is used); using .SIZE on a name that is
//     not a declared array macro is a validation-time error, checked by
//     ScriptValidator::m_validateArraySizeUsage() after the full command
//     list is built. Usable anywhere in the range: end-only, begin+end, or
//     begin+end+step, mixed freely with literals and plain macros, e.g.
//       i ?= REPEAT lbl $cfgs.SIZE
//       i ?= REPEAT lbl 0, $cfgs.SIZE
//       i ?= REPEAT lbl 0, $cfgs.SIZE, 2
// Exact numeric parsing/typing and range-count validation happens in the validator;
// this pattern only enforces the lexical shape (1 to 3 comma-separated tokens).
inline bool m_isRepeat(const std::string& expression)
{
    // Number-or-macro token shared by <begin>/<end>/<step>.
    static const std::string strNumTok =
        std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";

    // Optional capture prefix:  [varname ?=]
    // Counted / ranged form:    [varname ?=] REPEAT label end[, end][, step]
    static const std::regex counted(
        std::string(SCRIPT_RX_REPEAT_PREFIX) +
        strNumTok + "(?:\\s*,\\s*" + strNumTok + "){0,2}$");
    // Conditional form:         [varname ?=] REPEAT label UNTIL cond
    static const std::regex until(std::string(SCRIPT_RX_REPEAT_PREFIX) + "UNTIL\\s+\\S.*$");
    return std::regex_match(expression, counted) || std::regex_match(expression, until);
}

// validate END_REPEAT <label>
inline bool m_isEndRepeat(const std::string& expression)
{
    static const std::regex pattern("^END_REPEAT\\s+" SCRIPT_RX_IDENT "$");
    return std::regex_match(expression, pattern);
}

// validate BREAK <loop-label>
inline bool m_isBreak(const std::string& expression)
{
    static const std::regex pattern("^BREAK\\s+" SCRIPT_RX_IDENT "$");
    return std::regex_match(expression, pattern);
}

// validate CONTINUE <loop-label>
inline bool m_isContinue(const std::string& expression)
{
    static const std::regex pattern("^CONTINUE\\s+" SCRIPT_RX_IDENT "$");
    return std::regex_match(expression, pattern);
}

// validate PRINT [text]
inline bool m_isPrint(const std::string& expression)
{
    static const std::regex pattern(R"(^PRINT(\s.*)?$)");
    return std::regex_match(expression, pattern);
}

// validate DELAY <value> <unit>
// <value> : positive integer (>= 1)
// <unit>  : us | ms | sec   (case-sensitive)
inline bool m_isDelay(const std::string& expression)
{
    static const std::regex pattern("^DELAY\\s+[1-9][0-9]*\\s+" SCRIPT_RX_TIME_UNITS "$");
    return std::regex_match(expression, pattern);
}

// validate BREAKPOINT [label]
// A bare BREAKPOINT (no label) or BREAKPOINT followed by any text used
// as a label.  The label may contain $macros — expanded at runtime.
inline bool m_isBreakpoint(const std::string& expression)
{
    static const std::regex pattern(R"(^BREAKPOINT(\s.*)?$)");
    return std::regex_match(expression, pattern);
}

}; //namespace usyntax


#endif //U_SCRIPT_SYNTAX_HPP