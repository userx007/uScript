#ifndef SCRIPTDATATYPES_HPP
#define SCRIPTDATATYPES_HPP

#include <string>
#include <vector>
#include <variant>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

#include "uNumeric.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                               DATATYPES                                     //
/////////////////////////////////////////////////////////////////////////////////


// forward declaration
struct PluginDataType;

// ---------------------------------------------------------------------------
// extractIsThreaded — strip the trailing " &" suffix from strParams and
// return true if the command should be launched as a joinable std::jthread.
//
// Suffix syntax (last token of the params string, space-separated):
//   PLUGIN.CMD args      →  false  (sequential, unchanged behaviour)
//   PLUGIN.CMD args &    →  true   (joinable thread)
//
// The suffix is stripped from strParams before storing it into the IR node.
// MacroCommand (?= capture) with bThreaded=true is rejected at validation
// time because getData() is meaningless after an asynchronous dispatch.
// ---------------------------------------------------------------------------
inline bool extractIsThreaded(std::string& strParams)
{
    if (strParams.size() >= 2 &&
        strParams.compare(strParams.size() - 2, 2, " &") == 0)
    {
        strParams.erase(strParams.size() - 2);
        return true;
    }
    return false;
}

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
    bool        bThreaded = false;
};

struct Command {
    std::string strPlugin;
    std::string strCommand;
    std::string strParams;
    bool        bThreaded = false;
};

struct Condition {
    std::string strCondition;
    std::string strLabelName;
};

struct Label {
    std::string strLabelName;
};

// ---------------------------------------------------------------------------
// A single bound/step value of a REPEAT range (begin, end, or step).
// Either a literal number — resolved once, at validation time — or a
// deferred "$macroname" reference, re-resolved every time the loop is
// (re-)entered at runtime.
//
// Accepted literal notations: decimal integer, hex (0x/0X), binary (0b/0B),
// octal (0o/0O), and decimal floating-point (with optional sign/exponent).
// bIsInteger records which of llValue/dValue holds the resolved value; it is
// only meaningful when bIsMacro is false (deferred macro values are re-typed
// at runtime, see parseRepeatNumber()).
// ---------------------------------------------------------------------------
struct RepeatRangeValue {
    std::string strExpr;             // raw literal text, or "$macroname" (deferred)
    bool        bIsMacro   = false;  // true => strExpr is "$macroname", resolved at runtime
    bool        bIsInteger = true;   // true => integer literal; false => floating-point literal
    long long   llValue    = 0;      // resolved integer value (valid when !bIsMacro && bIsInteger)
    double      dValue     = 0.0;    // resolved double  value (valid when !bIsMacro && !bIsInteger)
};

// Repeat over the numeric range [begin, end) with the given step; body is delimited
// by the matching RepeatEnd with the same label. This generalises the original
// "repeat N times" form, which is equivalent to begin=0, step=1:
//
//   REPEAT label end               ->  begin=0,     end=end, step=1
//   REPEAT label begin, end        ->  begin=begin, end=end, step=1
//   REPEAT label begin, end, step  ->  begin=begin, end=end, step=step
//
// Direction is inferred from the sign of step:
//   step > 0  ->  loop continues while current <  end
//   step < 0  ->  loop continues while current >  end
// A step of exactly zero is rejected (would never reach <end>).
// If the range is empty (e.g. begin >= end with a positive step) the loop body
// runs zero times.
//
// strVarMacroName: if non-empty, the current loop value is written to this
// variable macro at the start of every iteration and is accessible via
// $strVarMacroName. When all of begin/end/step resolve to integers the value
// is rendered as a plain integer string; otherwise it is rendered as a double.
struct RepeatTimes {
    std::string      strLabel;
    RepeatRangeValue begin;          // defaults to literal "0" when only <end> is given
    RepeatRangeValue end;
    RepeatRangeValue step;           // defaults to literal "1" when no <step> is given
    std::string      strVarMacroName;    // iteration-value capture macro (empty = no capture)
};

// ---------------------------------------------------------------------------
// Numeric literal helpers for REPEAT ranges.
// Shared between the validator (parses literal begin/end/step tokens once,
// at compile time) and the interpreter (re-parses the macro-expanded string
// of any deferred "$macroname" bound, once per loop entry).
// ---------------------------------------------------------------------------

// Parse a signed integer literal in decimal, hex (0x/0X), binary (0b/0B), or
// octal (0o/0O) notation. Returns true and sets outValue on a full match.
//
// Thin delegate to numeric::string_to_signed<long long> (uNumeric.hpp), which
// now owns this grammar as the single source of truth: explicit-only base
// prefixes (no legacy implicit octal on a bare leading zero), with the sign
// recognised before the prefix so "-0x10" parses correctly. This matches the
// grammar this function has always documented, so the delegation is behaviour-
// preserving for this file.
inline bool tryParseRepeatInteger(const std::string& strTok, long long& outValue) noexcept
{
    return numeric::string_to_signed<long long>(strTok, outValue);
}

// Parse a signed decimal floating-point literal (optional sign, fractional
// part, and/or exponent). Returns true and sets outValue on a full match.
//
// Delegates to numeric::str2double (uNumeric.hpp): it trims whitespace and
// requires the entire (trimmed) token to be consumed, which matches this
// function's "full match or fail" contract. The only behavioural difference
// is hex-float notation (e.g. "0x1.8p3"), which str2double's istringstream-
// based parser does not accept — irrelevant here since REPEAT range literals
// never use hex floats (hex notation is reserved for the integer path).
inline bool tryParseRepeatDouble(const std::string& strTok, double& outValue) noexcept
{
    if (strTok.empty()) { return false; }
    return numeric::str2double(strTok, outValue);
}

// Parse an already macro-expanded, whitespace-trimmed token into either an
// integer or a double, choosing the representation based on its notation:
// hex/binary/octal are always integers; a plain decimal token is an integer
// unless it contains a '.' or an exponent, in which case it is a double.
// Returns false if the token matches neither notation.
inline bool parseRepeatNumber(const std::string& strTok, bool& bIsInteger,
                               long long& llOut, double& dOut) noexcept
{
    // Delegates prefix detection to numeric::has_explicit_base_prefix (uNumeric.hpp) rather than
    // re-deriving it by hand — same unified grammar tryParseRepeatInteger now consumes below.
    if (numeric::has_explicit_base_prefix(strTok)) {
        if (tryParseRepeatInteger(strTok, llOut)) {
            bIsInteger = true;
            dOut       = static_cast<double>(llOut);
            return true;
        }
        return false;
    }

    const bool bLooksFloat = (strTok.find('.') != std::string::npos) ||
                              (strTok.find_first_of("eE") != std::string::npos);

    if (!bLooksFloat && tryParseRepeatInteger(strTok, llOut)) {
        bIsInteger = true;
        dOut       = static_cast<double>(llOut);
        return true;
    }
    if (tryParseRepeatDouble(strTok, dOut)) {
        bIsInteger = false;
        llOut      = static_cast<long long>(dOut);
        return true;
    }
    return false;
}

// Render a REPEAT loop's current double value for exposure via $strVarMacroName.
// Uses a generous but finite precision and the stream's default (shortest
// reasonable) float format, so integral doubles print as "3" not "3.000000".
inline std::string formatRepeatDouble(double dValue) noexcept
{
    std::ostringstream oss;
    oss.precision(15);
    oss << dValue;
    return oss.str();
}

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

// Output format requested by an optional "| HEX..." MATH post-processor.
// NONE means no hex post-processing (the raw numeric result is stored as-is).
// Width is the zero-padded byte count of the rendered hex string; HEX_8 has
// no endianness since a single byte has none. See MathStatement below.
enum class HexOutputFormat {
    NONE,
    HEX_8,
    HEX_16_LE,  HEX_16_BE,
    HEX_32_LE,  HEX_32_BE,
    HEX_64_LE,  HEX_64_BE,
    HEX_128_LE, HEX_128_BE
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
//
// Optional "| HEX[_<width>][_<endian>]" post-processor (eHexFormat != NONE):
// the integer result is rendered as a fixed-width, zero-padded hex string
// (see getHexFormatByteWidth / isHexFormatBigEndian below) instead of being
// stored as a plain decimal string.
//
// Syntax:   result ?= MATH 255          | HEX           (-> "FF")
//           result ?= MATH 255          | HEX_16_BE      (-> "00FF")
//           result ?= MATH 255          | HEX_16_LE      (-> "FF00")
struct MathStatement {
    std::string     strName;       // destination macro name (identifier)
    std::string     strExprTpl;    // raw expression template (may contain $macros)
    HexOutputFormat eHexFormat = HexOutputFormat::NONE;
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

// ---------------------------------------------------------------------------
// HexOutputFormat support: name lookup (for logging) plus byte-width /
// endianness accessors consumed at execution time. Deliberately dependency-
// free (no hexutils/uString include) since this header is an INTERFACE-only
// library — the interpreter combines these with hexutils::intToHexStringFixed()
// (uHexlify.hpp), which it already links against, to do the actual rendering.
// ---------------------------------------------------------------------------

inline const std::string& getHexFormatName(HexOutputFormat eFmt)
{
    switch(eFmt)
    {
        case HexOutputFormat::NONE:      { static const std::string name = "none";       return name; }
        case HexOutputFormat::HEX_8:     { static const std::string name = "HEX_8";      return name; }
        case HexOutputFormat::HEX_16_LE: { static const std::string name = "HEX_16_LE";  return name; }
        case HexOutputFormat::HEX_16_BE: { static const std::string name = "HEX_16_BE";  return name; }
        case HexOutputFormat::HEX_32_LE: { static const std::string name = "HEX_32_LE";  return name; }
        case HexOutputFormat::HEX_32_BE: { static const std::string name = "HEX_32_BE";  return name; }
        case HexOutputFormat::HEX_64_LE: { static const std::string name = "HEX_64_LE";  return name; }
        case HexOutputFormat::HEX_64_BE: { static const std::string name = "HEX_64_BE";  return name; }
        case HexOutputFormat::HEX_128_LE:{ static const std::string name = "HEX_128_LE"; return name; }
        case HexOutputFormat::HEX_128_BE:{ static const std::string name = "HEX_128_BE"; return name; }
        default:                         { static const std::string name = "UNKNOWN";    return name; }
    }
}

// Zero-padded byte width for a given hex output format (1, 2, 4, 8, or 16).
// Returns 0 for HexOutputFormat::NONE.
inline size_t getHexFormatByteWidth(HexOutputFormat eFmt) noexcept
{
    switch(eFmt)
    {
        case HexOutputFormat::HEX_8:                                          return 1;
        case HexOutputFormat::HEX_16_LE:  case HexOutputFormat::HEX_16_BE:    return 2;
        case HexOutputFormat::HEX_32_LE:  case HexOutputFormat::HEX_32_BE:    return 4;
        case HexOutputFormat::HEX_64_LE:  case HexOutputFormat::HEX_64_BE:    return 8;
        case HexOutputFormat::HEX_128_LE: case HexOutputFormat::HEX_128_BE:   return 16;
        default:                                                              return 0;
    }
}

// True if the given hex output format renders big-endian (MSB first).
// HEX_8 and NONE are not endian-specific and return false.
inline bool isHexFormatBigEndian(HexOutputFormat eFmt) noexcept
{
    switch(eFmt)
    {
        case HexOutputFormat::HEX_16_BE:
        case HexOutputFormat::HEX_32_BE:
        case HexOutputFormat::HEX_64_BE:
        case HexOutputFormat::HEX_128_BE:
            return true;
        default:
            return false;
    }
}

#endif // SCRIPTDATATYPES_HPP