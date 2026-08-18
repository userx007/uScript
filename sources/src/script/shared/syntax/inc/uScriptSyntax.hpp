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

// validate BITSTREAM / BYTESTREAM statements:
//   name ?= BITSTREAM  offset:length:value [offset:length:value ...] [| REVERSE_BIT|REVERSE_BYTE]
//   name ?= BYTESTREAM byte_offset:length:value [byte_offset:length:value ...] [| REVERSE_BIT|REVERSE_BYTE]
//
// Each of offset/length/value may be a literal (the same integer notations
// REPEAT accepts: decimal, 0x/0b/0o, sign — sign is accepted here for lexical
// consistency with SCRIPT_RX_NUMERIC_TOKEN, but a negative or non-integer
// value is rejected later, at execution time, when it fails to parse as a
// uint64_t) or a "$macroname"/"$arrayname.SIZE" reference, resolved at
// runtime exactly like a REPEAT range value. At least one field is required;
// the exact field-splitting and every numeric/overlap/fit check is done by
// ScriptValidator::m_HandleBitstreamStmt()/m_HandleBytestreamStmt() (uses
// parseStreamStatement(), shared with the interactive shell) and by
// ScriptInterpreter's BITSTREAM/BYTESTREAM execution — this pattern only
// enforces the lexical shape.
//
// Examples:
//   cfg ?= BITSTREAM 64:1:1 34:4:7 19:2:3
//   cfg ?= BITSTREAM $off:$len:$val | REVERSE_BIT
//   cfg ?= BYTESTREAM 0:8:0xAA 1:4:3 | REVERSE_BYTE
inline bool m_isBitstreamStmt(const std::string& expression)
{
    static const std::string tok =
        std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";
    static const std::string field  = tok + "\\s*:\\s*" + tok + "\\s*:\\s*" + tok;
    static const std::regex  pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*BITSTREAM\\s+" + field +
        "(?:\\s+" + field + ")*(?:\\s*\\|\\s*(?:REVERSE_BIT|REVERSE_BYTE)\\s*)?$");
    return std::regex_match(expression, pattern);
}

inline bool m_isBytestreamStmt(const std::string& expression)
{
    static const std::string tok =
        std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";
    static const std::string field  = tok + "\\s*:\\s*" + tok + "\\s*:\\s*" + tok;
    static const std::regex  pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*BYTESTREAM\\s+" + field +
        "(?:\\s+" + field + ")*(?:\\s*\\|\\s*(?:REVERSE_BIT|REVERSE_BYTE)\\s*)?$");
    return std::regex_match(expression, pattern);
}

// validate BITSTREAMVAL / BYTESTREAMVAL statements — the read-side
// counterpart of BITSTREAM/BYTESTREAM above:
//   name ?= <hex_source> | BITSTREAMVAL  bit_offset:value_size
//   name ?= <hex_source> | BYTESTREAMVAL byte_offset:bit_offset:value_size
//
// Unlike BITSTREAM/BYTESTREAM (keyword right after '?='), the keyword here
// comes *after* the pipe — the shape is "name ?= source | KEYWORD field",
// the same shape FORMAT uses ("name ?= FORMAT input | pattern") with the
// keyword moved to the other side of the pipe. <hex_source> may be a
// literal hex string or a "$macroname" reference (anything up to the
// pipe); bit_offset/value_size (and byte_offset for BYTESTREAMVAL) accept
// the same literal-or-$macro tokens BITSTREAM's own fields do. Exactly one
// field is allowed — there is no field list and no REVERSE_BIT/
// REVERSE_BYTE suffix, since a getter produces one value, not a buffer. For
// one-or-more fields extracted from the same source in a single statement,
// see m_isBitstreamValArrayStmt()/m_isBytestreamValArrayStmt() below (the
// "name [= ..." array form) instead.
//
// The exact field-splitting and every numeric/range/fit check (does
// bit_offset fit the source buffer, does a BYTESTREAMVAL field cross a
// byte boundary, does value_size exceed 64) is done by
// ScriptValidator::m_HandleBitstreamValStmt()/m_HandleBytestreamValStmt()
// (uses parseStreamValStatement(), shared with the interactive shell) and
// by ScriptInterpreter's BITSTREAMVAL/BYTESTREAMVAL execution — this
// pattern only enforces the lexical shape.
//
// Examples:
//   v ?= 1122334455667788 | BITSTREAMVAL 64:1
//   v ?= $frame | BITSTREAMVAL $off:$len
//   v ?= $frame | BYTESTREAMVAL 2:5:3
inline bool m_isBitstreamValStmt(const std::string& expression)
{
    static const std::string tok =
        std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";
    static const std::string field = tok + "\\s*:\\s*" + tok;
    static const std::regex  pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*\\S[^|]*\\|\\s*BITSTREAMVAL\\s+" + field + "\\s*$");
    return std::regex_match(expression, pattern);
}

inline bool m_isBytestreamValStmt(const std::string& expression)
{
    static const std::string tok =
        std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";
    static const std::string field = tok + "\\s*:\\s*" + tok + "\\s*:\\s*" + tok;
    static const std::regex  pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*\\S[^|]*\\|\\s*BYTESTREAMVAL\\s+" + field + "\\s*$");
    return std::regex_match(expression, pattern);
}

// validate BITSTREAMVAL / BYTESTREAMVAL *array* statements — the
// one-or-more-fields counterpart of m_isBitstreamValStmt()/
// m_isBytestreamValStmt() above:
//   name [= <hex_source> | BITSTREAMVAL  bit_offset1:value_size1 [bit_offset2:value_size2 ...]
//   name [= <hex_source> | BYTESTREAMVAL byte_offset1:bit_offset1:value_size1 [...]
//
// Same "source | KEYWORD field..." shape as the scalar ("?=") form, with
// two differences: the destination is introduced with the array-macro
// operator "[=" (NAME [= elem1, elem2, ... — see m_isArrayMacro() above)
// rather than "?=", and one-or-more (not exactly one) whitespace-separated
// fields are accepted after the keyword, each field having the same shape
// the scalar form uses (bit_offset:value_size for BITSTREAMVAL,
// byte_offset:bit_offset:value_size for BYTESTREAMVAL). Every extracted
// value becomes one element of the destination array macro, in field
// order — see StreamValArrayStatement's doc comment (uScriptDataTypes.hpp).
//
// The exact field-splitting and every numeric/range/fit check is done by
// ScriptValidator::m_HandleBitstreamValArrayStmt()/
// m_HandleBytestreamValArrayStmt() (uses parseStreamValArrayStatement(),
// shared with the interactive shell) and by ScriptInterpreter's
// BITSTREAMVAL_ARRAY/BYTESTREAMVAL_ARRAY execution — this pattern only
// enforces the lexical shape.
//
// Examples:
//   v [= 1122334455667788 | BITSTREAMVAL 64:1
//   v [= $frame | BITSTREAMVAL 64:1 34:4 19:2
//   v [= $frame | BYTESTREAMVAL 0:7:8 1:7:8 2:7:8
inline bool m_isBitstreamValArrayStmt(const std::string& expression)
{
    static const std::string tok =
        std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";
    static const std::string field = tok + "\\s*:\\s*" + tok;
    static const std::regex  pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\[=\\s*\\S[^|]*\\|\\s*BITSTREAMVAL\\s+" + field +
        "(?:\\s+" + field + ")*\\s*$");
    return std::regex_match(expression, pattern);
}

inline bool m_isBytestreamValArrayStmt(const std::string& expression)
{
    static const std::string tok =
        std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";
    static const std::string field = tok + "\\s*:\\s*" + tok + "\\s*:\\s*" + tok;
    static const std::regex  pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\[=\\s*\\S[^|]*\\|\\s*BYTESTREAMVAL\\s+" + field +
        "(?:\\s+" + field + ")*\\s*$");
    return std::regex_match(expression, pattern);
}

// validate a GENERATOR statement:
//   name ?= GENERATOR <count> <unit> <min>:<max>:<step>[:<k>] | WAVEFORM [| ENCODING]
//   name ?= GENERATOR STOP
//
// Syntax:
//   <count> <unit>  — identical grammar to DELAY (SCRIPT_RX_TIME_UNITS: us|ms|sec).
//                      Tick interval; the generator runs forever until stopped.
//   <min>:<max>:<step>[:<k>] — same field shape as a BITSTREAM field (literal or
//                      "$macroname"). The optional 4th field (k) is a curve-
//                      steepness constant, lexically accepted after any
//                      waveform (WAVEFORM itself comes later in the string,
//                      so this regex cannot restrict k to EXP/LOG — that is a
//                      validation-time check, see ScriptValidator::m_HandleGeneratorStmt()).
//   WAVEFORM        — LINEAR | SAWTOOTH | TRIANGLE | SINE | SQUARE | EXP | LOG
//                      (LINEAR is a documented alias for SAWTOOTH).
//   ENCODING        — optional, same "HEX[_<width>][_<endian>]" suffix MATH
//                      already accepts (see m_isMathStmt above); defaults to
//                      plain decimal when absent.
//   STOP            — stops the named generator's background thread. Every
//                      other (non-STOP) form always (re)launches a fresh
//                      thread for `name`, stopping any thread already running
//                      for that name first — see ScriptInterpreter's
//                      GeneratorStatement execution.
//
// All numeric/semantic checks (field count vs. waveform, k only allowed for
// EXP/LOG, SQUARE's step must be a positive integer, START/STOP pairing) are
// deferred to the validator handler and its script-wide pairing pass — this
// pattern only enforces the lexical shape, exactly like BITSTREAM/MATH above.
//
// Examples:
//   ctr   ?= GENERATOR 100 ms 0:255:1 | SAWTOOTH | HEX_8
//   angle ?= GENERATOR 20  ms 0:360:1 | SINE
//   lvl   ?= GENERATOR 50  ms 0:100:5 | TRIANGLE | HEX_16_LE
//   gpio  ?= GENERATOR 500 ms 0:1:1   | SQUARE
//   lvl   ?= GENERATOR STOP
inline bool m_isGeneratorStmt(const std::string& expression)
{
    static const std::string tok   = std::string("(?:") + SCRIPT_RX_NUMERIC_TOKEN + "|" + SCRIPT_RX_MACRO_REF + ")";
    static const std::string range = tok + "\\s*:\\s*" + tok + "\\s*:\\s*" + tok + "(?:\\s*:\\s*" + tok + ")?";
    static const std::regex  pattern(
        "^" SCRIPT_RX_IDENT "\\s*\\?=\\s*GENERATOR\\s+"
        "(?:STOP"
        "|[1-9][0-9]*\\s+" SCRIPT_RX_TIME_UNITS "\\s+" + range +
          "\\s*\\|\\s*(?:LINEAR|SAWTOOTH|TRIANGLE|SINE|SQUARE|EXP|LOG)"
          "(?:\\s*\\|\\s*HEX(?:_(?:8|16|32|64|128|FLOAT|DOUBLE))?(?:_(?:LE|BE))?)?"
        ")\\s*$");
    return std::regex_match(expression, pattern);
}

// validate the bare "GENERATOR STOP ALL" command — stops every currently
// running GENERATOR thread, regardless of destination macro name. Modeled on
// BREAK/CONTINUE's bare-command shape (no "?=", no destination). Requires at
// least one generator to be running at that point in the script — enforced
// at validation time by the same START/STOP pairing pass m_isGeneratorStmt's
// STOP form uses (see ScriptValidator's generator-pairing validation).
inline bool m_isGeneratorStopAll(const std::string& expression)
{
    static const std::regex pattern("^GENERATOR\\s+STOP\\s+ALL$");
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