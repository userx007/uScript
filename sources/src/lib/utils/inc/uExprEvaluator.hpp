#ifndef U_EXPR_EVALUATOR_HPP
#define U_EXPR_EVALUATOR_HPP

/*
 * uEvalExprEvaluator.hpp
 *
 * Unified EVAL expression evaluator.
 *
 * Provides evaluation of structured comparison expressions exposed through the
 * EVAL keyword in script statements.  Every comparison is delegated to either
 * VectorValidator (for typed scalar comparisons) or BoolExprEvaluator (for
 * pure boolean literals), so all existing validation rules remain in force.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * SYNTAX
 * ─────────────────────────────────────────────────────────────────────────────
 *
 *   EVAL <atom> [&& <atom> || <atom> ...]
 *
 * Where <atom> is ONE of:
 *
 *   (a)  <operand> <op> <operand>               — typed scalar comparison
 *   (b)  TRUE | FALSE | !TRUE | !FALSE          — boolean literal
 *
 * <operand>   ::= literal value (string / number / version)
 *                 or a $macro reference (already expanded before EVAL sees it)
 *
 * <op>        ::= ==  !=  <  <=  >  >=          — numeric / version / boolean
 *              |  EQ  NE  eq  ne  ==  !=         — string
 *
 * <type-hint> (optional suffix on <op>):
 *   :STR  :NUM  :VER  :BOOL                     — forces VectorValidator type
 *   Absent → type is inferred from the operand values (see m_inferType).
 *
 * Compound expression (logical AND / OR with C-operator precedence):
 *
 *   EVAL $a == $b && $c != $d || $e >= $f
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * INTEGRATION POINTS
 * ─────────────────────────────────────────────────────────────────────────────
 *
 *   1.  VarMacroInit  (name ?= EVAL …)
 *       Interpreter evaluates the EVAL expression at runtime and stores
 *       "TRUE" or "FALSE" in the variable macro.
 *
 *   2.  Condition     (IF EVAL … GOTO label)
 *       Interpreter evaluates the EVAL expression at runtime; if the result
 *       is true the GOTO skip is activated.
 *
 *   3.  RepeatUntil   (REPEAT label UNTIL EVAL …)
 *       Same as Condition — evaluated at each END_REPEAT.
 *
 * The caller (ScriptInterpreter) strips the leading "EVAL " prefix and passes
 * the remainder to EvalExprEvaluator::evaluate().
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * EXAMPLES (after $macro expansion)
 * ─────────────────────────────────────────────────────────────────────────────
 *
 *   done   ?= EVAL $flag == TRUE             → "TRUE" or "FALSE"
 *   done   ?= EVAL $count >= 10              → numeric comparison
 *   result ?= EVAL 1.2.3 < 1.3.0:VER        → version comparison
 *
 *   IF EVAL $a == $b GOTO label
 *   IF EVAL $x != $y && $z >= 5 GOTO label
 *
 *   REPEAT loop UNTIL EVAL $done == TRUE
 *   REPEAT loop UNTIL EVAL $i >= $max && $ok == TRUE
 */

#include "uLogger.hpp"
#include "uVectorValidator.hpp"
#include "uBoolEvaluator.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <cctype>
#include <stdexcept>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "EVAL_EXPR   |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                     IMPLEMENTATION                            //
///////////////////////////////////////////////////////////////////

class EvalExprEvaluator
{
public:

    // -----------------------------------------------------------------------
    // evaluate()
    //
    // Entry point.  Receives the expression string *without* the leading
    // "EVAL " prefix (the caller strips it).  All $macros must already be
    // expanded before this call.
    //
    // Returns true and sets result on success.
    // Returns false (and logs) on any parse / type / evaluation error.
    // -----------------------------------------------------------------------
    bool evaluate(const std::string& expr, bool& result) const
    {
        try {
            return m_parseCompound(expr, result);
        } catch (const std::exception& ex) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("EVAL exception:"); LOG_STRING(ex.what()));
            return false;
        } catch (...) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("EVAL unknown exception"));
            return false;
        }
    }

private:

    // ─────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────

    static std::string_view m_trimSV(std::string_view sv)
    {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))  sv.remove_suffix(1);
        return sv;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Type inference
    //
    // Heuristic applied when no :TYPE suffix is present:
    //   BOOL  — value is TRUE, FALSE, !TRUE, !FALSE (case-insensitive)
    //   NUM   — all characters are digits (uint64 range)
    //   VER   — digits separated by dots (at least one dot)
    //   STR   — fallback
    // ─────────────────────────────────────────────────────────────────────

    static bool m_isBoolLiteral(std::string_view v)
    {
        // Case-insensitive check for the four boolean keywords.
        auto ci = [](std::string_view s, const char* lit, size_t n) {
            if (s.size() != n) return false;
            for (size_t i = 0; i < n; ++i)
                if (std::tolower(static_cast<unsigned char>(s[i])) != lit[i]) return false;
            return true;
        };
        return ci(v,"true",4) || ci(v,"false",5) || ci(v,"!true",5) || ci(v,"!false",6);
    }

    static bool m_isNumericLiteral(std::string_view v)
    {
        return !v.empty() && std::all_of(v.begin(), v.end(), ::isdigit);
    }

    static bool m_isVersionLiteral(std::string_view v)
    {
        // A canonical version string has the form N.N[.N[.N]] where every
        // component is a non-empty run of digits and there are 1–3 dots
        // (i.e. 2–4 components).  A plain decimal number like "3.14" or
        // "3.14159" is deliberately NOT matched so that floats are never
        // misclassified as versions when no :TYPE hint is present.
        //
        // Rules:
        //  - Must contain at least one dot.
        //  - Every dot-separated component must be non-empty and all-digit.
        //  - Maximum 3 dots (4 components), minimum 1 dot (2 components).
        //  - Each component must be a SHORT integer-looking token (no more
        //    than 9 digits) — this rejects long decimal fractions like
        //    "3.14159265" which have a single multi-digit fractional part.
        if (v.empty()) return false;

        int    dotCount   = 0;
        int    segLen     = 0;   // length of current component
        bool   startOfSeg = true;

        for (char c : v) {
            if (c == '.') {
                if (startOfSeg) return false;  // leading dot or consecutive dots
                if (segLen > 9) return false;  // component too long → looks like a float fraction
                ++dotCount;
                if (dotCount > 3) return false; // more than 4 components
                segLen     = 0;
                startOfSeg = true;
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                ++segLen;
                startOfSeg = false;
            } else {
                return false; // non-digit, non-dot character
            }
        }

        // Must not end with a dot, must have had at least one dot,
        // and the last component must not be too long.
        if (startOfSeg)  return false;  // trailing dot
        if (dotCount < 1) return false; // no dots at all
        if (segLen > 9)  return false;  // last component too long

        return true;
    }

    static eValidateType m_inferType(std::string_view lhs, std::string_view rhs)
    {
        // Both operands must agree for a reliable inference.
        // Priority: BOOL > NUM > VER > STR
        if (m_isBoolLiteral(lhs) || m_isBoolLiteral(rhs))  return eValidateType::BOOLEAN;
        if (m_isNumericLiteral(lhs) && m_isNumericLiteral(rhs)) return eValidateType::NUMBER;
        if (m_isVersionLiteral(lhs) || m_isVersionLiteral(rhs)) return eValidateType::VERSION;
        return eValidateType::STRING;
    }

    static eValidateType m_typeFromSuffix(const std::string& suffix)
    {
        if (suffix == "STR")  return eValidateType::STRING;
        if (suffix == "NUM")  return eValidateType::NUMBER;
        if (suffix == "VER")  return eValidateType::VERSION;
        if (suffix == "BOOL") return eValidateType::BOOLEAN;
        throw std::invalid_argument("Unknown type suffix: " + suffix);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Token extraction
    //
    // An atom is one of:
    //   <lhs-word> <op[:TYPE]> <rhs-word>
    //   TRUE | FALSE | !TRUE | !FALSE
    //
    // Words are delimited by whitespace.  The operator may carry an optional
    // colon-separated type suffix: ==:NUM, !=:STR, >=:VER, etc.
    //
    // Returns a view of the remainder (everything after the atom, trimmed).
    // ─────────────────────────────────────────────────────────────────────

    // O7: lhs and rhs are string_views into the original expression string.
    // The expression string is owned by the caller and outlives all Atom instances,
    // so string_view is safe.  op is kept as std::string because it may be a
    // trimmed substring (trim can produce a view of itself, but the opRaw copy
    // is a local — materialising as string is the safest and clearest choice
    // for a small fixed-size token like "==" or "EQ").
    struct Atom {
        std::string_view  lhs;
        std::string       op;
        std::string_view  rhs;
        eValidateType     type = eValidateType::STRING;
        bool              isBoolLiteralOnly = false;
    };

    // Split a string_view at the first whitespace boundary.
    static std::string_view m_nextWord(std::string_view& sv)
    {
        // skip leading ws
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
        if (sv.empty()) return {};
        // consume non-ws
        size_t len = 0;
        while (len < sv.size() && !std::isspace(static_cast<unsigned char>(sv[len]))) ++len;
        std::string_view word = sv.substr(0, len);
        sv.remove_prefix(len);
        return word;
    }

    // Parse a single comparison atom from sv, advancing sv past the atom.
    // Returns false on parse error.
    bool m_parseAtom(std::string_view& sv, Atom& atom) const
    {
        // word1
        std::string_view word1 = m_nextWord(sv);
        if (word1.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("EVAL: empty atom"));
            return false;
        }

        atom.lhs = word1;   // O7: string_view — no copy

        // Save position so we can test if this is a lone boolean literal
        std::string_view svAfterWord1 = sv;

        // word2 — could be an operator or we might be at && / || / end
        std::string_view word2 = m_nextWord(sv);

        // If word2 is a logical connector or empty → word1 is a lone boolean
        if (word2.empty() || word2 == "&&" || word2 == "||") {
            sv = svAfterWord1;
            atom.isBoolLiteralOnly = true;
            if (!m_isBoolLiteral(atom.lhs)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("EVAL: expected operator after"); LOG_STRING(atom.lhs));
                return false;
            }
            return true;
        }

        // word2 is the operator — parse optional :TYPE suffix
        {
            std::string opRaw(word2);   // materialise only for the operator token
            std::string typeSuffix;
            auto colon = opRaw.find(':');
            if (colon != std::string::npos) {
                typeSuffix = opRaw.substr(colon + 1);
                opRaw      = opRaw.substr(0, colon);
            }

            // Trim stray whitespace from the operator — defensive against
            // code paths that may include surrounding spaces in the token.
            {
                const size_t fs = opRaw.find_first_not_of(" \t");
                const size_t ls = opRaw.find_last_not_of(" \t");
                opRaw = (fs == std::string::npos) ? "" : opRaw.substr(fs, ls - fs + 1);
            }

            atom.op = opRaw;

            // word3 — right-hand side
            std::string_view word3 = m_nextWord(sv);
            if (word3.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("EVAL: missing RHS after operator"); LOG_STRING(atom.op));
                return false;
            }
            atom.rhs = word3;   // O7: string_view — no copy

            // ── Postfix type hint ─────────────────────────────────────────
            // After the RHS, an optional type token may follow in one of two
            // spaced forms:
            //   … rhs :TYPE          e.g.  hello EQ hello :STR
            //   … rhs : TYPE         e.g.  hello EQ hello : STR
            // The inline op:TYPE form (no spaces) is already handled above.
            // The token must start with ':' and the name must be a known
            // keyword (STR / NUM / VER / BOOL); otherwise it is NOT consumed.
            if (typeSuffix.empty()) {
                std::string_view svSaved = sv;
                std::string_view word4   = m_nextWord(sv);

                if (!word4.empty() && word4[0] == ':') {
                    std::string_view candidate = word4.substr(1); // strip ':'

                    if (candidate.empty()) {
                        // Bare ':' — type keyword is the next word
                        std::string_view svSaved2 = sv;
                        std::string_view word5    = m_nextWord(sv);
                        if (word5 == "STR" || word5 == "NUM" ||
                            word5 == "VER" || word5 == "BOOL") {
                            typeSuffix = std::string(word5);
                        } else {
                            sv = svSaved; // not a keyword — push both back
                        }
                    } else {
                        if (candidate == "STR" || candidate == "NUM" ||
                            candidate == "VER" || candidate == "BOOL") {
                            typeSuffix = std::string(candidate);
                        } else {
                            sv = svSaved; // not a keyword — push word4 back
                        }
                    }
                } else {
                    sv = svSaved; // word4 is not a type token — restore
                }
            }

            // Resolve type: explicit suffix wins over inference
            if (!typeSuffix.empty()) {
                atom.type = m_typeFromSuffix(typeSuffix);
            } else {
                atom.type = m_inferType(atom.lhs, atom.rhs);
            }
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Atom evaluation
    //
    // Delegates to VectorValidator for the typed comparison, or to
    // BoolExprEvaluator for lone boolean literals.
    // ─────────────────────────────────────────────────────────────────────

    bool m_evaluateAtom(const Atom& atom, bool& result) const
    {
        if (atom.isBoolLiteralOnly) {
            // Lone boolean literal — BoolExprEvaluator takes a string_view directly.
            return BoolExprEvaluator{}.evaluate(atom.lhs, result);
        }

        // Typed comparison via VectorValidator.
        // Materialise lhs/rhs to std::string only here — the one point where
        // the public API requires std::string (via vector<string>).
        // VectorValidator is now default-constructible with no per-instance state.
        const std::string slhs(atom.lhs);
        const std::string srhs(atom.rhs);
        result = VectorValidator{}.validate({slhs}, {srhs}, atom.op, atom.type);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Compound expression parser  (|| / && with standard C precedence)
    //
    //   expr  := term  ( '||' term  )*
    //   term  := atom  ( '&&' atom  )*
    //
    // Implements short-circuit evaluation to match C semantics.
    // ─────────────────────────────────────────────────────────────────────

    bool m_parseCompound(const std::string& exprStr, bool& result) const
    {
        std::string_view sv(exprStr);

        // Skip leading "EVAL" keyword if caller did not strip it
        {
            std::string_view peek = sv;
            std::string_view first = m_nextWord(peek);
            if (first == "EVAL") {
                sv = peek; // advance past the keyword
            }
        }

        return m_parseOr(sv, result);
    }

    bool m_parseOr(std::string_view& sv, bool& result) const
    {
        bool lhs;
        if (!m_parseAnd(sv, lhs)) return false;

        while (true) {
            std::string_view saved = sv;
            std::string_view token = m_nextWord(sv);
            if (token == "||") {  // O2: compare string_view directly — no heap alloc
                bool rhs;
                if (!m_parseAnd(sv, rhs)) return false;
                lhs = lhs || rhs;
            } else {
                sv = saved;
                break;
            }
        }
        result = lhs;
        return true;
    }

    bool m_parseAnd(std::string_view& sv, bool& result) const
    {
        Atom atom;
        if (!m_parseAtom(sv, atom)) return false;
        bool lhs;
        if (!m_evaluateAtom(atom, lhs)) return false;

        while (true) {
            std::string_view saved = sv;
            std::string_view token = m_nextWord(sv);
            if (token == "&&") {  // O2: compare string_view directly — no heap alloc
                Atom rAtom;
                if (!m_parseAtom(sv, rAtom)) return false;
                bool rhs;
                if (!m_evaluateAtom(rAtom, rhs)) return false;
                lhs = lhs && rhs;
            } else {
                sv = saved;
                break;
            }
        }
        result = lhs;
        return true;
    }
};

#endif // U_EXPR_EVALUATOR_HPP
