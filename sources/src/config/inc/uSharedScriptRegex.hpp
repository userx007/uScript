#ifndef USHARED_SCRIPT_REGEX_HPP
#define USHARED_SCRIPT_REGEX_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Shared script-grammar REGEX FRAGMENTS.
//
// uScriptSyntax.hpp (src/script/shared/syntax) is the authoritative definition
// of the uscript statement grammar: it uses std::regex to VALIDATE whole
// script lines. The GUI frontend (src/scriptgui) needs to recognise the exact
// same sub-shapes — identifiers, the plugin-instance suffix, numeric/macro
// tokens, etc. — with QRegularExpression, in order to SYNTAX-HIGHLIGHT and
// (in a couple of places) auto-detect clickable lines.
//
// Historically each side hand-wrote its own copy of these fragments, which
// drifted apart (see the "PLUGIN_TYPE_NAME" and "LOAD_PLUGIN_VERSION" notes
// below for two drifts this header fixes). This header holds each fragment
// exactly ONCE, as a plain #define string — deliberately NOT a compiled
// std::regex or QRegularExpression object, because the two engines are used
// completely differently on each side (uScriptSyntax.hpp mostly concatenates
// fragments into a handful of large ^...$ whole-line patterns at static-init
// time; the frontend drops fragments into many small \b...\b partial-line
// rules, each wrapped in its own capture groups for colouring). A plain
// string macro lets each side compose it however it needs, via ordinary
// adjacent string-literal concatenation:
//
//   std::regex   pattern("^LABEL\\s+" SCRIPT_RX_IDENT "$");
//   QRegularExpression re(QStringLiteral("\\b(LABEL)\\s+(") + SCRIPT_RX_IDENT + ")");
//
// Every fragment is written as a plain (non-raw) C string literal — using
// "\\s" rather than R"(\s)" — specifically so it concatenates cleanly with
// either raw or non-raw string literals at any call site, on either side.
//
// Every fragment is a non-capturing shape (uses (?:...) internally, never a
// bare capturing "(...)"), so wrapping a fragment in the caller's own "(" ")"
// never shifts anyone else's capture-group numbering.
//
// NOT shared here: fragments that only resemble each other by coincidence but
// serve unrelated purposes (e.g. eval::isValidVersion() in uMathOpsValidator.hpp
// validates a runtime plugin-ABI version string and has no connection to the
// script grammar; IniHighlighter's TRUE/FALSE and hex-literal rules colour
// uscript.ini files, not script text). Only fragments that recognise the same
// piece of *script* syntax on both sides live here.
// ─────────────────────────────────────────────────────────────────────────────


// ── Identifier ─────────────────────────────────────────────────────────────
// A macro/array/variable name, a LABEL name, or a GOTO/REPEAT/END_REPEAT/
// BREAK/CONTINUE loop-label reference. Leading underscore allowed.
//   uScriptSyntax.hpp : m_isConstantMacro, m_isArrayMacro, m_isVariableMacro,
//                        m_isVarMacroInit, m_isFormatStmt, m_isMathStmt,
//                        m_isIfGoToCondition, m_isLabel, m_isRepeat,
//                        m_isEndRepeat, m_isBreak, m_isContinue
//   frontend          : NAME ?=/[=/:=  ·  LABEL name  ·  GOTO target  ·
//                        REPEAT's own loop-label  ·  END_REPEAT's label
#define SCRIPT_RX_IDENT                     "[A-Za-z_][A-Za-z0-9_]*"

// ── LOAD_PLUGIN's plugin-type name ───────────────────────────────────────────
// Deliberately narrower than SCRIPT_RX_IDENT: the first character must be a
// letter — a plugin *type* name (UART, KVCAN, ...) never starts with '_',
// unlike a macro/label name.
//   uScriptSyntax.hpp : m_isLoadPlugin
//   frontend          : LOAD_PLUGIN argument highlighter — this fragment
//                        FIXES a drift where the highlighter used to reuse
//                        SCRIPT_RX_IDENT (allowing a leading '_') and so could
//                        highlight a LOAD_PLUGIN argument the interpreter
//                        would actually reject.
#define SCRIPT_RX_PLUGIN_TYPE_NAME          "[A-Za-z][A-Za-z0-9_]*"

// ── Optional plugin-instance suffix:  :N ─────────────────────────────────────
// N is a positive integer with no leading zero (UART:1, UART:2, ... — not
// UART:0 or UART:01). The single most duplicated fragment in the codebase.
//   uScriptSyntax.hpp : m_isLoadPlugin, m_isVariableMacro, m_isCommand
//   frontend          : PLUGIN.COMMAND highlighter, LOAD_PLUGIN argument
//                        highlighter, and both comm-script filename rules
//                        (PLUGIN.SCRIPT <file> / PLUGIN.COMMAND script <file>)
#define SCRIPT_RX_INSTANCE_SUFFIX           "(?::[1-9][0-9]*)?"

// ── Uppercase identifier:  PLUGIN name / COMMAND name ────────────────────────
// The "PLUGIN[:N].COMMAND" grammar requires both sides of the '.' to be
// upper-case identifiers (lower-case is never a valid plugin or command name).
//   uScriptSyntax.hpp : m_isVariableMacro (command target), m_isCommand
//   frontend          : PLUGIN.COMMAND highlighter, both comm-script filename
//                        rules
#define SCRIPT_RX_UPPER_IDENT               "[A-Z][A-Z0-9_]*"

// ── Macro reference:  $name ───────────────────────────────────────────────────
//   uScriptSyntax.hpp : m_isRepeat's <begin>/<end>/<step> macro alternative
//   frontend          : addMacroVariableRule()'s bare $VAR rule, and the same
//                        REPEAT range macro alternative
#define SCRIPT_RX_MACRO_REF                 "\\$[A-Za-z_][A-Za-z0-9_]*"

// ── REPEAT range value token:  signed hex/bin/oct/dec/float literal ─────────
// One <begin>/<end>/<step> token in REPEAT's counted/ranged form (the macro-
// reference alternative is SCRIPT_RX_MACRO_REF, kept separate — the validator
// ORs the two together in one alternation, while the highlighter keeps them
// in separate capture groups so it can colour a literal differently from a
// macro reference).
//   uScriptSyntax.hpp : m_isRepeat's strNumTok (literal alternative)
//   frontend          : the REPEAT-range highlighter's numTok (literal
//                        alternative)
#define SCRIPT_RX_NUMERIC_TOKEN             \
    "(?:[+-]?(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|0[oO][0-7]+|" \
    "(?:[0-9]+\\.[0-9]*|\\.[0-9]+|[0-9]+)(?:[eE][+-]?[0-9]+)?))"

// ── REPEAT's counted/ranged-form prefix ───────────────────────────────────────
// "[varname ?=] REPEAT <label> " — the exact literal text both sides match
// before the range-value list begins.
//   uScriptSyntax.hpp : m_isRepeat's "counted" pattern prefix
//   frontend          : the REPEAT-range highlighter's `prefix`
#define SCRIPT_RX_REPEAT_PREFIX             \
    "^(?:" SCRIPT_RX_IDENT "\\s*\\?=\\s*)?REPEAT\\s+" SCRIPT_RX_IDENT "\\s+"

// ── DELAY time units ──────────────────────────────────────────────────────────
//   uScriptSyntax.hpp : m_isDelay
//   frontend          : CommScriptHighlighter's delay-unit rule (the set is
//                        identical; only the alternative order differs, which
//                        doesn't affect matching)
#define SCRIPT_RX_TIME_UNITS                "(us|ms|sec)"

// ── LOAD_PLUGIN's version-comparator literal:  v1.2.3.4 ─────────────────────
// Exactly four dot-separated groups — this is the ABI version compared with
// <=/</>=/>/== in "LOAD_PLUGIN name >= v1.2.3.4".
//   uScriptSyntax.hpp : m_isLoadPlugin's comparator clause
//   frontend          : the "Version literals" highlighter — this fragment
//                        FIXES a drift where the highlighter used to accept
//                        any 2-or-more-part "v#.#..." shape (so it would
//                        colour "v1.2" or "v1.2.3.4.5" too, neither of which
//                        the interpreter actually accepts here).
#define SCRIPT_RX_LOAD_PLUGIN_VERSION        "v\\d+\\.\\d+\\.\\d+\\.\\d+"

#endif /* USHARED_SCRIPT_REGEX_HPP */
