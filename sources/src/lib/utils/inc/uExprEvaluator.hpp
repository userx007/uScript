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

    static std::string m_trim(const std::string& s)
    {
        return std::string(m_trimSV(s));
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

    static bool m_isBoolLiteral(const std::string& v)
    {
        std::string lv = v;
        std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
        return lv == "true" || lv == "false" || lv == "!true" || lv == "!false";
    }

    static bool m_isNumericLiteral(const std::string& v)
    {
        return !v.empty() && std::all_of(v.begin(), v.end(), ::isdigit);
    }

    static bool m_isVersionLiteral(const std::string& v)
    {
        if (v.empty()) return false;
        bool hasDot = false;
        for (char c : v) {
            if (c == '.') { hasDot = true; continue; }
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        return hasDot;
    }

    static eValidateType m_inferType(const std::string& lhs, const std::string& rhs)
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

    struct Atom {
        std::string lhs;
        std::string op;         // pure operator symbol / word (no :suffix)
        std::string rhs;
        eValidateType type = eValidateType::STRING;
        bool isBoolLiteralOnly = false; // TRUE / FALSE standing alone
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

        atom.lhs = std::string(word1);

        // Save position so we can test if this is a lone boolean literal
        std::string_view svAfterWord1 = sv;

        // word2 — could be an operator or we might be at && / || / end
        std::string_view word2 = m_nextWord(sv);
        const std::string sWord2(word2);

        // If word2 is a logical connector or empty → word1 is a lone boolean
        if (word2.empty() || sWord2 == "&&" || sWord2 == "||") {
            // Restore sv so the caller can re-read the connector
            sv = svAfterWord1;
            atom.isBoolLiteralOnly = true;
            // Validate it IS actually a bool literal
            if (!m_isBoolLiteral(atom.lhs)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("EVAL: expected operator after"); LOG_STRING(atom.lhs));
                return false;
            }
            return true;
        }

        // word2 is the operator — parse optional :TYPE suffix
        {
            std::string opRaw = sWord2;
            std::string typeSuffix;
            auto colon = opRaw.find(':');
            if (colon != std::string::npos) {
                typeSuffix = opRaw.substr(colon + 1);
                opRaw      = opRaw.substr(0, colon);
            }
            atom.op = opRaw;

            // word3 — right-hand side
            std::string_view word3 = m_nextWord(sv);
            if (word3.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("EVAL: missing RHS after operator"); LOG_STRING(atom.op));
                return false;
            }
            atom.rhs = std::string(word3);

            // Resolve type
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
            // Lone boolean literal — delegate to BoolExprEvaluator
            BoolExprEvaluator beEval;
            std::string expr = atom.lhs;
            return beEval.evaluate(expr, result);
        }

        // Typed comparison — VectorValidator works on single-element vectors
        VectorValidator vv;
        std::vector<std::string> v1{ atom.lhs };
        std::vector<std::string> v2{ atom.rhs };

        result = vv.validate(v1, v2, atom.op, atom.type);
        return true; // VectorValidator logs and returns false on error; treat false as result
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
            // peek at next word
            std::string_view saved = sv;
            std::string_view token = m_nextWord(sv);
            if (std::string(token) == "||") {
                bool rhs;
                if (!m_parseAnd(sv, rhs)) return false;
                lhs = lhs || rhs;
            } else {
                sv = saved; // not ||, push back
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
            if (std::string(token) == "&&") {
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
