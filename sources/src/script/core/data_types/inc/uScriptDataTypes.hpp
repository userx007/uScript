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
// MacroCommand (?= capture) with bThreaded=true launches a background thread
// that re-dispatches the underlying PLUGIN.CMD in an endless loop, atomically
// refreshing the captured variable on every successful iteration - see
// ScriptInterpreter::m_executeCommand / m_setRuntimeVarMacro(). This is what
// backs constructs like "VAL ?= UART.CMD < &" (keep receiving and updating
// VAL for as long as the script/thread runs).
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
    BITSTREAM_STMT, // name ?= BITSTREAM  offset:length:value ... [| REVERSE_BIT|REVERSE_BYTE]
    BYTESTREAM_STMT,// name ?= BYTESTREAM byte_offset:length:value ... [| REVERSE_BIT|REVERSE_BYTE]
    BITSTREAMVAL_STMT, // name ?= hex_source | BITSTREAMVAL  bit_offset:value_size
    BYTESTREAMVAL_STMT,// name ?= hex_source | BYTESTREAMVAL byte_offset:bit_offset:value_size
    BITSTREAMVAL_ARRAY_STMT, // name [= hex_source | BITSTREAMVAL  bit_offset1:value_size1 [bit_offset2:value_size2 ...]
    BYTESTREAMVAL_ARRAY_STMT,// name [= hex_source | BYTESTREAMVAL byte_offset1:bit_offset1:value_size1 [...]
    GENERATOR_STMT,          // name ?= GENERATOR <count> <unit> min:max:step[:k] | WAVEFORM [| ENCODING]  |  name ?= GENERATOR STOP
    GENERATOR_STOP_ALL_STMT, // GENERATOR STOP ALL (bare command — stops every running generator)
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
// (re-)entered at runtime. "$arrayname.SIZE" (the element count of a
// declared ARRAY_MACRO) is stored and deferred the same way — strExpr holds
// the full "$arrayname.SIZE" text and bIsMacro is true; nothing else in this
// struct needs to know the difference, since ScriptInterpreter::
// m_replaceVariableMacros() already resolves both forms through the same
// $macro-expansion pass (see its own doc comment).
//
// Accepted literal notations: decimal integer, hex (0x/0X), binary (0b/0B),
// octal (0o/0O), and decimal floating-point (with optional sign/exponent).
// bIsInteger records which of llValue/dValue holds the resolved value; it is
// only meaningful when bIsMacro is false (deferred macro/array-size values
// are re-typed at runtime, see parseRepeatNumber()).
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
//
// HEX_8/16/32/64/128 render the result as a fixed-width two's-complement
// integer (see getHexFormatByteWidth / isHexFormatBigEndian below).
// HEX_FLOAT/HEX_DOUBLE instead render the result as its raw IEEE-754
// bit pattern (binary32 / binary64) — see isHexFormatFloatingPoint below.
enum class HexOutputFormat {
    NONE,
    HEX_8,
    HEX_16_LE,  HEX_16_BE,
    HEX_32_LE,  HEX_32_BE,
    HEX_64_LE,  HEX_64_BE,
    HEX_128_LE, HEX_128_BE,
    HEX_FLOAT_LE,  HEX_FLOAT_BE,
    HEX_DOUBLE_LE, HEX_DOUBLE_BE
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
//           result ?= MATH -1.0         | HEX_FLOAT_BE   (-> "BF800000")
//           result ?= MATH pi           | HEX_DOUBLE_LE  (-> raw IEEE-754 binary64 bytes, little-endian)
struct MathStatement {
    std::string     strName;       // destination macro name (identifier)
    std::string     strExprTpl;    // raw expression template (may contain $macros)
    HexOutputFormat eHexFormat = HexOutputFormat::NONE;
};

// Waveform shape requested by a GENERATOR statement's "| WAVEFORM" field.
// "LINEAR" (accepted at the syntax level, see usyntax::m_isGeneratorStmt) is
// a documented alias for SAWTOOTH — both describe the same ramp-and-reset
// signal, so they share one IR value rather than two behaviourally-identical
// enumerators. EXP/LOG are the only two that consume the optional 4th range
// field (k, see GeneratorStatement below); it is a validation-time error
// (ScriptValidator::m_HandleGeneratorStmt()) to supply k with any other
// waveform.
// RANDOM draws a uniform random sample from [begin,end] (order-independent)
// every tick; it ignores step entirely (a step field is still lexically
// required — see GeneratorStatement's doc comment — but its value is unused).
// When the statement's data source is an array (GeneratorStatement::
// bIsArraySource) only SAWTOOTH/LINEAR (sequential, wrapping), TRIANGLE
// (ping-pong through the elements) and RANDOM (uniform pick of one element)
// are meaningful — SINE/SQUARE/EXP/LOG are rejected for array sources at
// validation time (ScriptValidator::m_HandleGeneratorStmt()).
enum class GeneratorWaveform { SAWTOOTH, TRIANGLE, SINE, SQUARE, EXP, LOG, RANDOM };

// name ?= GENERATOR <count> <unit> <begin>:<end>:<step>[:<k>] | WAVEFORM [| ENCODING]
// name ?= GENERATOR <count> <unit> <elem1>,<elem2>,...                | WAVEFORM [| ENCODING]
// name ?= GENERATOR <count> <unit> $arrayName                        | WAVEFORM [| ENCODING]
// name ?= GENERATOR STOP
//
// Native, self-threading cyclic value generator — no plugin required.
// A non-STOP statement (re)launches a background jthread, keyed by strName,
// that ticks every uIntervalUs microseconds, computes the next sample of the
// requested waveform, and writes it into m_RuntimeVarMacros[strName] via the
// same ScriptInterpreter::m_setRuntimeVarMacro() every other built-in
// ("?=") statement uses — so $strName is immediately usable anywhere
// $macro expansion runs (conditions, PRINT, plugin command params, an
// un-cached CYCLIC session's live $NAME re-resolution via
// uvolatile::VolatileMacroStore, ...), exactly like a threaded
// "VAL ?= PLUGIN.CMD args &" MacroCommand's result.
//
// Restart semantics: EVERY non-STOP GENERATOR statement for a given strName
// unconditionally stops whatever generator thread is already running for
// that name (if any) before launching the new one — there is no attempt to
// detect "same params as before" and skip the relaunch. This is what makes
// "val ?= GENERATOR ..." with different params on each loop iteration behave
// as intended: no explicit STOP is needed between iterations, and the
// waveform's internal state (current/direction/phase/ticksAtLevel/arrIndex)
// always restarts cleanly from <begin> (or from array element 0).
//
// begin/end/step/k (or every array element) are resolved exactly once, at
// the moment the statement (re)launches its thread — identical to how
// RepeatTimes resolves a non-"$macro" begin/end/step once. A "$macroname"
// bound is still deferred lexically (bIsMacro=true) but is only ever re-read
// at that one launch moment, never mid-run — see
// ScriptInterpreter::m_resolveGeneratorRange().
//
// Reverse ranges: unlike the old "min:max" naming, <begin> is not required
// to be numerically smaller than <end> — "20:10:1" is valid and counts down
// from 20 to 10. <step> is always treated as a magnitude (its sign, if any,
// is ignored); the actual direction of travel is derived from comparing
// <begin> and <end> once they are resolved. This applies to SAWTOOTH and
// TRIANGLE; SQUARE keeps interpreting step as a tick count (direction is
// meaningless there — it just toggles between the two levels); SINE/EXP/LOG
// use step as a magnitude too (phase/ramp always advances forward, while
// <begin>/<end> still set the amplitude/direction of the shape itself).
//
// Waveform semantics (current/direction/phaseDeg/ticksAtLevel/arrIndex are
// per-thread state, seeded fresh on every launch — see ScriptInterpreter's
// execution):
//   SAWTOOTH (LINEAR alias): current += signed step; wraps to begin once it
//                             crosses end (sign derived from begin vs end).
//                             Array source: emits elements in order, then
//                             wraps back to element 0.
//   TRIANGLE:                current += step*direction; direction flips
//                             (ping-pong) whenever current reaches the
//                             low/high bound (order-independent — works
//                             the same whether begin < end or begin > end).
//                             Array source: walks the elements forward then
//                             backward (index ping-pong), never repeating
//                             an end element twice in a row.
//   SQUARE:                  step is reinterpreted as "ticks to hold each
//                             level" (must resolve to a positive integer —
//                             checked at validation time for a literal step,
//                             at execution time for a "$macro" step); toggles
//                             between begin and end once every `step` ticks.
//                             Not available for an array source.
//   SINE:                    mid=(begin+end)/2, amp=(end-begin)/2,
//                             phaseDeg += step (degrees/tick, wraps at 360);
//                             value = mid + amp*sin(phaseDeg in radians).
//                             Not available for an array source.
//   EXP / LOG:                exponential/logarithmic ramp over a normalised
//                             [0,1) cycle driven by step, shaped by k
//                             (defaults: EXP k=3.0, LOG k=e-1, when the
//                             optional 4th range field was omitted). Not
//                             available for an array source.
//   RANDOM:                  uniform random sample; range source draws from
//                             [min(begin,end), max(begin,end)] every tick
//                             (a fresh independent draw each time — no
//                             memory of previous samples). Array source
//                             instead SHUFFLES: draws a random permutation
//                             of every element and walks it in order,
//                             emitting each element exactly once before
//                             drawing a fresh permutation — i.e. a shuffled
//                             playlist, not independent picks (so it never
//                             starves an element and never repeats one
//                             immediately, other than by chance across a
//                             reshuffle boundary). step is ignored either way.
//
// Rendering: identical to MathStatement's own conversion (see eHexFormat's
// doc comment above) — no "| ENCODING" -> plain decimal string
// (std::defaultfloat, 15 significant digits); "| HEX_8".."HEX_128_LE/BE" ->
// uint64_t truncation, fixed-width zero-padded hex; "| HEX_FLOAT/HEX_DOUBLE"
// -> raw IEEE-754 bit pattern. Reuses HexOutputFormat / getHexFormatByteWidth
// / isHexFormatBigEndian / isHexFormatFloatingPoint / isHexFormatSinglePrecision
// unchanged. Every array element must resolve, at launch, to a plain number
// (same parseRepeatNumber() convention as begin/end/step) since rendering is
// always numeric.
//
// Array data source (bIsArraySource): instead of a numeric begin:end:step
// range, the statement is fed a fixed list of values, taken either from:
//   - an inline comma list lexically identical to an ARRAY_MACRO's own
//     right-hand side: "1,7,$x,$y,8,9" (each element literal-or-"$macro",
//     same per-element convention as begin/end/step); or
//   - a single "$arrayName" token that names an already-declared ARRAY_MACRO
//     (see ScriptEntries::mapArrayMacros) — expanded at validation time into
//     the exact same per-element list the array macro itself holds, i.e.
//     "gen ?= GENERATOR 1 ms $array | SAWTOOTH" (with "array [= 1,7,9,$x,$y")
//     compiles to the same vArrayValues as
//     "gen ?= GENERATOR 1 ms 1,7,9,$x,$y | SAWTOOTH" written out directly.
//     If the single "$name" token does NOT name a declared array macro it is
//     instead kept as one ordinary (deferred) element — a one-element array.
// vArrayValues always has at least one element when bIsArraySource is true.
//
// bStop selects the "val ?= GENERATOR STOP" form: only strName is meaningful
// then (stops that one name's generator thread; every other field is
// default-initialised and unused). See GeneratorStopAllStatement below for
// the bare, no-destination "GENERATOR STOP ALL" form.
struct GeneratorStatement {
    std::string       strName;                       // destination macro name (identifier)
    bool              bStop        = false;           // true => "val ?= GENERATOR STOP"
    uint64_t          uIntervalUs  = 0;                // tick interval, normalised to microseconds (DELAY-style)
    bool              bIsArraySource = false;          // true => vArrayValues drives the generator, begin/end/step unused
    RepeatRangeValue  begin, end, step;                 // deferred $macro-capable, resolved once at (re)launch. Meaningful only when !bIsArraySource
    std::vector<RepeatRangeValue> vArrayValues;         // >= 1 element, each deferred $macro-capable. Meaningful only when bIsArraySource
    bool              bHasK        = false;            // true => the optional 4th range field (k) was present
    RepeatRangeValue  k;                                // curve-steepness constant; only meaningful when bHasK
    GeneratorWaveform eWaveform    = GeneratorWaveform::SAWTOOTH;
    HexOutputFormat   eHexFormat   = HexOutputFormat::NONE;
};

// GENERATOR STOP ALL — bare command (no destination macro, no "?="),
// modeled on BREAK/CONTINUE's bare-command shape. Stops every currently
// running generator thread. Carries no fields of its own; its mere presence
// in the IR is the instruction. Requires at least one generator to be
// running at that point in the script — enforced at validation time by the
// same START/STOP pairing pass GeneratorStatement's STOP form uses (see
// ScriptValidator's generator-pairing validation).
struct GeneratorStopAllStatement {
};

// Post-processing mirror requested by an optional "| REVERSE_BIT" or
// "| REVERSE_BYTE" suffix on a BITSTREAM/BYTESTREAM statement. Applied to
// the fully-packed byte buffer, after every field has been written and
// before it is hexlified. See StreamStatement below.
enum class StreamReverseMode { NONE, REVERSE_BIT, REVERSE_BYTE };

// One "offset:length:value" field of a BITSTREAM/BYTESTREAM statement.
// All three are stored as raw templates (may contain $macros — constant or
// variable — resolved at execution time, same deferred-macro pattern as
// MathStatement/FormatStatement/RepeatRangeValue) rather than pre-resolved,
// since a variable macro's value is only known once the script is running.
struct StreamField {
    std::string strOffsetTpl;   // BITSTREAM: absolute bit offset. BYTESTREAM: byte offset.
    std::string strLengthTpl;   // number of bits the value occupies
    std::string strValueTpl;    // the value to store — must fit in strLengthTpl bits
};

// name ?= BITSTREAM  offset:length:value [offset:length:value ...] [| REVERSE_BIT|REVERSE_BYTE]
// name ?= BYTESTREAM byte_offset:length:value [byte_offset:length:value ...] [| REVERSE_BIT|REVERSE_BYTE]
// Native bit-packing evaluator — no plugin required. Builds a byte buffer by
// writing each field's value into it at the field's bit position, then
// stores the hexlified buffer (e.g. "AABB23E9FF") in m_RuntimeVarMacros[strName].
//
// Bit numbering is big-endian across the whole buffer: bit 0 is the MSB of
// byte 0; for an N-byte buffer the valid bit range is 0..(8N-1) (e.g. 0..63
// for 8 bytes). Within one field, "offset" names the field's LAST bit — its
// least-significant, right-hand bit — and the field extends backward
// (toward lower bit indices, i.e. toward the MSB) for "length" bits, exactly
// like a Verilog/VHDL descending bit-slice "[offset -: length]". The value
// is written MSB-first across that span, so bit (length-1) of the value
// lands at index (offset-length+1) and bit 0 lands at index "offset" itself.
// This is what makes "the highest offset used, rounded up to a whole byte"
// a reliable, length-independent way to size the output buffer (see
// ScriptInterpreter's BITSTREAM/BYTESTREAM execution for the exact
// algorithm) — with the opposite (offset = first/MSB bit, extending
// forward) a long field could silently need a byte beyond what its offset
// alone would suggest.
//
// BYTESTREAM's "byte_offset:length:value" is the same mechanism at byte
// granularity: byte_offset selects a byte, and the field is anchored at
// that byte's *last* (LSB) bit — i.e. it behaves exactly like BITSTREAM
// with an effective offset of (byte_offset*8 + 7) — and is right-aligned
// (LSB-aligned) within that byte. length is capped at 8 (1-8) since a
// BYTESTREAM field can never cross into a neighbouring byte, unlike
// BITSTREAM's field, which may span any number of bytes.
//
// Any bit not covered by a field is 0. Overlapping fields (two fields that
// claim the same bit index) and a value that doesn't fit in its field's
// length are both execution-time errors, not silently truncated/OR'd.
//
// To force a specific output size without an interesting value anywhere
// else, add a field whose offset is the buffer's last bit and whose value
// is 0, e.g. "63:1:0" forces exactly 8 bytes, "127:1:0" forces exactly 16.
//
// Field order in the statement is irrelevant — fields are sorted by offset
// before packing, purely so the size/overlap logic has one canonical order
// to reason about; it does not change the result.
struct StreamStatement {
    std::string             strName;              // destination macro name (identifier)
    std::vector<StreamField> vFields;              // one or more offset:length:value fields
    StreamReverseMode        eReverse = StreamReverseMode::NONE;
    bool                     bByteMode = false;    // false = BITSTREAM, true = BYTESTREAM
};

// name ?= <hex_source> | BITSTREAMVAL  <bit_offset>:<value_size>
// name ?= <hex_source> | BYTESTREAMVAL <byte_offset>:<bit_offset>:<value_size>
//
// The read-side counterpart of BITSTREAM/BYTESTREAM above: extracts one
// field back out of an already-hexlified byte buffer (typically — but not
// necessarily — one BITSTREAM/BYTESTREAM itself produced; any hexlified
// buffer works, e.g. a CAN frame's 8 data bytes) and stores it as a plain
// decimal uint64_t string in m_RuntimeVarMacros[strName], the same
// convention MathStatement's result uses.
//
// <hex_source> is a template (may contain $macros, resolved at execution
// time, same deferred-macro pattern as every other field here) evaluating
// to a hexlified byte string, e.g. "AABB23E9FF" or "$mystream".
//
// BITSTREAMVAL's <bit_offset> uses *exactly* the same convention as
// BITSTREAM's own "offset": big-endian bit numbering across the whole
// source buffer (bit 0 is the MSB of byte 0), and <bit_offset> names the
// field's LAST (LSB) bit — the field extends backward (toward lower bit
// indices, i.e. toward the MSB) for <value_size> bits — so
// "X ?= $s | BITSTREAMVAL 64:1" always reads back exactly what
// "s ?= BITSTREAM 64:1:X" wrote. <value_size> is 1-64 (the result is a
// uint64_t); reading is capped at 64 bits even though a BITSTREAM buffer
// itself may be arbitrarily wide.
//
// BYTESTREAMVAL's "<byte_offset>:<bit_offset>" locates a starting bit the
// same way BYTESTREAM's setter's fixed anchor does, generalised: byte_offset
// selects a byte, and bit_offset (0-7, 0 = that byte's MSB, matching
// BITSTREAM's own convention scaled down to one byte) names the field's
// last bit *within that byte* — bit_offset=7 (the byte's LSB) with
// value_size=8 is exactly BYTESTREAM's own always-anchor-at-the-last-bit
// behaviour as a special case. The field may not cross into a neighbouring
// byte (bit_offset+1 must be >= value_size), the same restriction
// BYTESTREAM's setter enforces, for the same reason: a byte-relative field
// only makes sense within the one byte it names. Effective global bit
// offset is (byte_offset*8 + bit_offset), then everything else — the
// backward extension, the 1-64 size range — is identical to BITSTREAMVAL.
//
// Both keywords additionally check that the field actually fits inside the
// *source* buffer's real length (bit_offset must be < 8 * (source byte
// count)) — unlike the setter, which grows the buffer to fit every field,
// the getter can only read what's really there, and reading past the end
// of a too-short source is an execution-time error, not silently zero.
//
// Field order matters here in a way it doesn't for the setter: there is
// exactly one field per statement (no field list), since a getter produces
// one value. For more than one field extracted from the same source in a
// single statement, see StreamValArrayStatement below (the "name [= ..."
// array form), which accepts one-or-more fields; this ("name ?= ...") form
// is deliberately restricted to exactly one field so "?=" always yields a
// single scalar and "[=" always yields an array — see parseStreamValStatement()
// (uStreamStatementParser.hpp) which rejects a "?=" line with more than one
// field.
struct StreamValStatement {
    std::string strName;          // destination macro name (identifier)
    std::string strSourceTpl;     // hexlified source buffer (may contain $macros)
    std::string strByteOffsetTpl; // BYTESTREAMVAL only: byte offset. Empty for BITSTREAMVAL.
    std::string strBitOffsetTpl;  // BITSTREAMVAL: absolute bit offset. BYTESTREAMVAL: bit offset within the byte (0-7).
    std::string strValueSizeTpl;  // number of bits to extract (1-64)
    bool        bByteMode = false;// false = BITSTREAMVAL, true = BYTESTREAMVAL
};

// One "<bit_offset>:<value_size>" (BITSTREAMVAL) or
// "<byte_offset>:<bit_offset>:<value_size>" (BYTESTREAMVAL) field of a
// StreamValArrayStatement. Same templates/conventions as StreamValStatement's
// own strByteOffsetTpl/strBitOffsetTpl/strValueSizeTpl, just repeated once
// per array element instead of exactly once per statement.
struct StreamValField {
    std::string strByteOffsetTpl; // BYTESTREAMVAL only: byte offset. Empty for BITSTREAMVAL.
    std::string strBitOffsetTpl;  // BITSTREAMVAL: absolute bit offset. BYTESTREAMVAL: bit offset within the byte (0-7).
    std::string strValueSizeTpl;  // number of bits to extract (1-64)
};

// name [= <hex_source> | BITSTREAMVAL  <bit_offset1>:<value_size1> [<bit_offset2>:<value_size2> ...]
// name [= <hex_source> | BYTESTREAMVAL <byte_offset1>:<bit_offset1>:<value_size1> [...]
//
// The array counterpart of StreamValStatement: extracts one-or-more fields
// from the same hex source in a single statement — the same per-field
// resolution/range/fit rules as StreamValStatement's single field apply to
// each of vFields independently (each field may name a different offset/
// size, and one field failing its checks fails the whole statement) — and
// stores every result, in field order, as an element of the array macro
// `strName`, exactly as if `strName` had been declared
// "strName [= v0, v1, ..." (see ARRAY_MACRO / ScriptEntries::mapArrayMacros)
// except the element values are computed at execution time rather than
// literal. This makes the usual array-macro access machinery
// ($strName.SIZE, $strName.$idx, $strName.N) work unmodified against the
// result.
//
// Deliberately uses the array-macro assignment operator "[=" rather than
// "?=" so the two forms are unambiguous by construction: "name ?= ..."
// (StreamValStatement) always has exactly one field and yields one scalar
// runtime variable macro; "name [= ..." (this struct) accepts any number of
// fields >= 1 and always yields an array macro, even when it only has one
// element — see parseStreamValArrayStatement() (uStreamStatementParser.hpp).
struct StreamValArrayStatement {
    std::string                 strName;      // destination array macro name (identifier)
    std::string                 strSourceTpl; // hexlified source buffer (may contain $macros)
    std::vector<StreamValField> vFields;      // one or more fields, extracted in order
    bool                        bByteMode = false; // false = BITSTREAMVAL, true = BYTESTREAMVAL
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
                                       MathStatement, BreakpointStatement, StreamStatement,
                                       StreamValStatement, StreamValArrayStatement,
                                       GeneratorStatement, GeneratorStopAllStatement>;

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
        case Token::BITSTREAM_STMT: { static const std::string name = "BITSTREAM";      return name; }
        case Token::BYTESTREAM_STMT:{ static const std::string name = "BYTESTREAM";     return name; }
        case Token::BITSTREAMVAL_STMT:       { static const std::string name = "BITSTREAMVAL";        return name; }
        case Token::BYTESTREAMVAL_STMT:      { static const std::string name = "BYTESTREAMVAL";       return name; }
        case Token::BITSTREAMVAL_ARRAY_STMT: { static const std::string name = "BITSTREAMVAL_ARRAY";  return name; }
        case Token::BYTESTREAMVAL_ARRAY_STMT:{ static const std::string name = "BYTESTREAMVAL_ARRAY"; return name; }
        case Token::GENERATOR_STMT:          { static const std::string name = "GENERATOR";          return name; }
        case Token::GENERATOR_STOP_ALL_STMT: { static const std::string name = "GENERATOR_STOP_ALL";  return name; }
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
        case HexOutputFormat::HEX_FLOAT_LE:  { static const std::string name = "HEX_FLOAT_LE";  return name; }
        case HexOutputFormat::HEX_FLOAT_BE:  { static const std::string name = "HEX_FLOAT_BE";  return name; }
        case HexOutputFormat::HEX_DOUBLE_LE: { static const std::string name = "HEX_DOUBLE_LE"; return name; }
        case HexOutputFormat::HEX_DOUBLE_BE: { static const std::string name = "HEX_DOUBLE_BE"; return name; }
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
        case HexOutputFormat::HEX_FLOAT_LE:  case HexOutputFormat::HEX_FLOAT_BE:  return 4;
        case HexOutputFormat::HEX_DOUBLE_LE: case HexOutputFormat::HEX_DOUBLE_BE: return 8;
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
        case HexOutputFormat::HEX_FLOAT_BE:
        case HexOutputFormat::HEX_DOUBLE_BE:
            return true;
        default:
            return false;
    }
}

// True if the given hex output format renders the result as a raw IEEE-754
// bit pattern (binary32 for HEX_FLOAT_*, binary64 for HEX_DOUBLE_*) rather
// than as a fixed-width two's-complement integer. Callers use this to pick
// between hexutils::floatToHexStringFixed()/doubleToHexStringFixed() and
// hexutils::intToHexStringFixed().
inline bool isHexFormatFloatingPoint(HexOutputFormat eFmt) noexcept
{
    switch(eFmt)
    {
        case HexOutputFormat::HEX_FLOAT_LE:
        case HexOutputFormat::HEX_FLOAT_BE:
        case HexOutputFormat::HEX_DOUBLE_LE:
        case HexOutputFormat::HEX_DOUBLE_BE:
            return true;
        default:
            return false;
    }
}

// True if the given hex output format renders binary32 (float) width, as
// opposed to binary64 (double) width. Only meaningful when
// isHexFormatFloatingPoint() is true.
inline bool isHexFormatSinglePrecision(HexOutputFormat eFmt) noexcept
{
    return eFmt == HexOutputFormat::HEX_FLOAT_LE || eFmt == HexOutputFormat::HEX_FLOAT_BE;
}

// ---------------------------------------------------------------------------
// GeneratorWaveform support: name lookup (logging) only — all the actual
// per-tick math lives in ScriptInterpreter (uScriptInterpreter.cpp), not
// here, since it needs per-thread mutable state (current/direction/phaseDeg/
// ticksAtLevel) this header has no business owning.
// ---------------------------------------------------------------------------
inline const std::string& getGeneratorWaveformName(GeneratorWaveform eWaveform)
{
    switch(eWaveform)
    {
        case GeneratorWaveform::SAWTOOTH: { static const std::string name = "SAWTOOTH"; return name; }
        case GeneratorWaveform::TRIANGLE: { static const std::string name = "TRIANGLE"; return name; }
        case GeneratorWaveform::SINE:     { static const std::string name = "SINE";     return name; }
        case GeneratorWaveform::SQUARE:   { static const std::string name = "SQUARE";   return name; }
        case GeneratorWaveform::EXP:      { static const std::string name = "EXP";      return name; }
        case GeneratorWaveform::LOG:      { static const std::string name = "LOG";      return name; }
        case GeneratorWaveform::RANDOM:   { static const std::string name = "RANDOM";   return name; }
        default:                          { static const std::string name = "UNKNOWN";  return name; }
    }
}

#endif // SCRIPTDATATYPES_HPP