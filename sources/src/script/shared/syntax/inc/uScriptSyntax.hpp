#ifndef U_SCRIPT_SYNTAX_HPP
#define U_SCRIPT_SYNTAX_HPP


#include <string>
#include <regex>

namespace usyntax
{

// validate a load plugin expression
// Supports plain names (UART) and instanced names (UART:1, UART:2, …).
// The instance suffix :N is a positive integer with no leading zeros.
inline bool m_isLoadPlugin(const std::string& expression )
{
    static const std::regex pattern(R"(^LOAD_PLUGIN\s+[A-Za-z][A-Za-z0-9_]*(?::[1-9][0-9]*)?\s*(\s+(<=|<|>=|>|==)\s+v\d+\.\d+\.\d+\.\d+)?$)");
    return std::regex_match(expression, pattern);
}

// validate a constant macro expression
inline bool m_isConstantMacro(const std::string& expression )
{
    static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*:=\s*\S.*$)");
    return std::regex_match(expression, pattern);
}

// validate an array macro expression:  NAME [= elem1, elem2, ...
// At least one element (non-empty content after [=) is required.
inline bool m_isArrayMacro(const std::string& expression)
{
    static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*\[=\s*\S.*$)");
    return std::regex_match(expression, pattern);
}

// validate a variable macro expression
// Supports plain plugin names and instanced names (UART:1.READ).
inline bool m_isVariableMacro(const std::string& expression )
{
    static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*[A-Z][A-Z0-9_]*(?::[1-9][0-9]*)?\.([A-Z][A-Z0-9_]*).*$)");
    return std::regex_match(expression, pattern);
}

// validate a direct variable macro initialisation:  name ?= <string value>
// This form is recognised only when VARIABLE_MACRO does NOT match — i.e. the
// right-hand side is not a PLUGIN.COMMAND pattern.  The value may be empty
// (bare "name ?=") which initialises the macro to an empty string.
inline bool m_isVarMacroInit(const std::string& expression)
{
    static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*\?=(\s.*)?$)");
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
        R"(^[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*FORMAT\s+\S[^|]*\|\s*\S.*$)");
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
        R"(^[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*MATH\s+\S.*$)");
    return std::regex_match(expression, pattern);
}

// validate simple command
// Supports plain plugin names (UART.SCRIPT) and instanced names (UART:1.SCRIPT).
inline bool m_isCommand(const std::string& expression )
{
    static const std::regex pattern(R"(^[A-Z][A-Z0-9_]*(?::[1-9][0-9]*)?\.([A-Z][A-Z0-9_]*)\s*.*$)");
    return std::regex_match(expression, pattern);
}

// validate "IF .. GOTO .." or "GOTO .." conditions
inline bool m_isIfGoToCondition(const std::string& expression)
{
    static const std::regex pattern(R"(^(?:IF\s+\S(?:.*\S)?\s+)?GOTO\s+[A-Za-z_][A-Za-z0-9_]*$)");
    return std::regex_match(expression, pattern);
}

// validate LABEL
inline bool m_isLabel(const std::string& expression )
{
    static const std::regex pattern(R"(^LABEL\s+[A-Za-z_][A-Za-z0-9_]*$)");
    return std::regex_match(expression, pattern);
}

// validate REPEAT <label> <count>  or  REPEAT <label> UNTIL <condition>
// and their index-capture forms:  varname ?= REPEAT <label> <count / UNTIL cond>
// Both forms share the same token; the handler in the validator distinguishes them.
inline bool m_isRepeat(const std::string& expression)
{
    // Optional capture prefix:  [varname ?=]
    // Counted form (literal):   [varname ?=] REPEAT label N
    static const std::regex counted(R"(^(?:[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*)?REPEAT\s+[A-Za-z_][A-Za-z0-9_]*\s+[1-9][0-9]*$)");
    // Counted form (macro ref): [varname ?=] REPEAT label $macroname
    static const std::regex macro  (R"(^(?:[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*)?REPEAT\s+[A-Za-z_][A-Za-z0-9_]*\s+\$[A-Za-z_][A-Za-z0-9_]*$)");
    // Conditional form:         [varname ?=] REPEAT label UNTIL cond
    static const std::regex until  (R"(^(?:[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*)?REPEAT\s+[A-Za-z_][A-Za-z0-9_]*\s+UNTIL\s+\S.*$)");
    return std::regex_match(expression, counted) || std::regex_match(expression, macro) || std::regex_match(expression, until);
}

// validate END_REPEAT <label>
inline bool m_isEndRepeat(const std::string& expression)
{
    static const std::regex pattern(R"(^END_REPEAT\s+[A-Za-z_][A-Za-z0-9_]*$)");
    return std::regex_match(expression, pattern);
}

// validate BREAK <loop-label>
inline bool m_isBreak(const std::string& expression)
{
    static const std::regex pattern(R"(^BREAK\s+[A-Za-z_][A-Za-z0-9_]*$)");
    return std::regex_match(expression, pattern);
}

// validate CONTINUE <loop-label>
inline bool m_isContinue(const std::string& expression)
{
    static const std::regex pattern(R"(^CONTINUE\s+[A-Za-z_][A-Za-z0-9_]*$)");
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
    static const std::regex pattern(R"(^DELAY\s+[1-9][0-9]*\s+(us|ms|sec)$)");
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