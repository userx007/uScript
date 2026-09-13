#ifndef UCALCULATOR_HPP
#define UCALCULATOR_HPP

/*
 * uCalculator.hpp  —  Extended recursive-descent expression evaluator
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * FIXES vs. original
 * ─────────────────────────────────────────────────────────────────────────────
 *  F1. Unary minus / unary plus  — "-5 + 3" and "+5" now work correctly.
 *  F2. Assignment detection     — uses first '=' that is NOT part of '==' or
 *                                  '!=' / '<=' / '>=' so comparisons in
 *                                  standalone expressions no longer cause a
 *                                  spurious assignment.
 *  F3. Scientific notation      — "1.5e10", "2E-3" parsed as numbers, not
 *                                  confused with the built-in constant 'e'.
 *  F4. Variable names           — allow digits after the first alpha/underscore
 *                                  character (x1, val_2, …).
 *  F5. Unconsumed input         — evaluate() throws if trailing non-whitespace
 *                                  remains after a full expression parse.
 *  F6. Division by zero         — throws a descriptive runtime_error.
 *  F7. Domain errors            — sqrt, log, asin, acos throw on invalid input.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * NEW FEATURES
 * ─────────────────────────────────────────────────────────────────────────────
 *  N1.  Operators
 *         %   — modulo (fmod for doubles)
 *         //  — integer (floor) division
 *
 *  N2.  Comparison operators  (return 1.0 = true, 0.0 = false)
 *         ==  !=  <  <=  >  >=
 *
 *  N3.  Logical operators  (short-circuit; 0.0 = false, anything else = true)
 *         &&  ||  !  (unary prefix)
 *
 *  N4.  Ternary operator
 *         condition ? expr_true : expr_false
 *
 *  N5.  Bitwise operators (operate on truncated 64-bit integers)
 *         &   |   ~   ^(xor)   <<   >>
 *       Note: '^' is ambiguous with power — power is expressed as pow(b,e)
 *             or via the ** operator; bare '^' means XOR in this version.
 *
 *  N6.  Power operator
 *         **  (right-associative, replaces original '^')
 *
 *  N7.  Additional math functions (single-argument)
 *         abs   ceil   floor   round   trunc
 *         exp   exp2
 *         log2
 *         asin  acos  atan
 *         sinh  cosh  tanh
 *         cbrt  (cube root)
 *         sign  (−1 / 0 / +1)
 *
 *  N8.  Two-argument functions
 *         pow(base, exp)   atan2(y, x)   min(a, b)   max(a, b)
 *         hypot(a, b)      fmod(a, b)    log_b(val, base)
 *
 *  N9.  Constants
 *         pi  e  tau  phi  (golden ratio)  inf  nan
 *
 *  N10. Implicit multiplication
 *         2pi   3(x+1)   (a+b)(a-b)
 *         Triggered when a number or closing ')' is immediately followed by
 *         an identifier or opening '(' without an operator between them.
 *
 *  N11. Non-decimal integer literals
 *         0b101010   0B101010   — binary   (prefix 0b / 0B)
 *         0x1F       0XDEAD     — hexadecimal (prefix 0x / 0X)
 *         0o755      0O755      — octal (explicit prefix 0o / 0O)
 *         0755                  — octal (legacy C-style leading zero)
 *         All forms are parsed as exact integers and converted to double.
 *         Hex digits a-f / A-F are accepted for 0x literals.
 *
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * GRAMMAR  (precedence, lowest → highest)
 * ─────────────────────────────────────────────────────────────────────────────
 *   expr        := assignment
 *   assignment  := ternary  [ '=' assignment ]       right-assoc
 *   ternary     := logical_or [ '?' expr ':' expr ]
 *   logical_or  := logical_and  ( '||' logical_and )*
 *   logical_and := bitwise_or   ( '&&' bitwise_or  )*
 *   bitwise_or  := bitwise_xor  ( '|'  bitwise_xor )*
 *   bitwise_xor := bitwise_and  ( '^'  bitwise_and )*
 *   bitwise_and := equality     ( '&'  equality    )*
 *   equality    := relational   ( ('=='|'!=') relational )*
 *   relational  := shift        ( ('<'|'<='|'>'|'>=') shift )*
 *   shift       := additive     ( ('<<'|'>>') additive )*
 *   additive    := term         ( ('+'|'-') term )*
 *   term        := unary        ( ('*'|'/'|'//'|'%') unary )*
 *   unary       := ('+'|'-'|'!'|'~') unary  |  power
 *   power       := postfix  ('**' unary)*              right-assoc
 *   postfix     := primary  (implicit_mul)*
 *   primary     := number | identifier_or_func | '(' expr ')'
 */

#include <cmath>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

#ifndef M_PI
#define M_PI   3.14159265358979323846
#endif
#ifndef M_E
#define M_E    2.71828182845904523536
#endif

class Calculator
{
public:
    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    // vars is a persistent map shared across multiple Calculator invocations
    // so that assigned variables survive between calls.
    Calculator(const std::string& expr,
               std::unordered_map<std::string, double>& vars)
        : m_expr(expr), m_pos(0), m_vars(vars)
    {
        // Built-in constants (only set if not already defined by the user)
        m_vars.try_emplace("pi",  M_PI);
        m_vars.try_emplace("e",   M_E);
        m_vars.try_emplace("tau", 2.0 * M_PI);
        m_vars.try_emplace("phi", 1.6180339887498948482);   // golden ratio
        m_vars.try_emplace("inf", std::numeric_limits<double>::infinity());
        m_vars.try_emplace("nan", std::numeric_limits<double>::quiet_NaN());
    }

    // Evaluate the expression and return the result.
    // Throws std::runtime_error on any parse or domain error.
    double evaluate()
    {
        m_pos = 0;
        double result = parseAssignment();
        skipWhitespace();
        if (m_pos < m_expr.size()) {
            throw std::runtime_error(
                std::string("Unexpected token at position ") +
                std::to_string(m_pos) + ": '" + m_expr[m_pos] + "'");
        }
        return result;
    }

private:
    std::string m_expr;
    size_t      m_pos;
    std::unordered_map<std::string, double>& m_vars;

    // ─────────────────────────────────────────────────────────────────────────
    // Utilities
    // ─────────────────────────────────────────────────────────────────────────

    void skipWhitespace()
    {
        while (m_pos < m_expr.size() &&
               std::isspace(static_cast<unsigned char>(m_expr[m_pos])))
            ++m_pos;
    }

    char peek(size_t offset = 0) const
    {
        size_t i = m_pos + offset;
        return (i < m_expr.size()) ? m_expr[i] : '\0';
    }

    bool match(const char* s)
    {
        size_t len = 0;
        while (s[len]) ++len;
        if (m_pos + len > m_expr.size()) return false;
        if (m_expr.compare(m_pos, len, s) != 0) return false;
        m_pos += len;
        return true;
    }

    // Consume a specific character; throw if not present.
    void expect(char c, const char* ctx = "")
    {
        skipWhitespace();
        if (m_pos >= m_expr.size() || m_expr[m_pos] != c) {
            std::string msg = "Expected '";
            msg += c;
            msg += "'";
            if (ctx && ctx[0]) { msg += " "; msg += ctx; }
            throw std::runtime_error(msg);
        }
        ++m_pos;
    }

    // Cast double to int64 for bitwise ops, with range check
    static int64_t toInt(double v)
    {
        if (!std::isfinite(v))
            throw std::runtime_error("Bitwise operation on non-finite value");
        if (v < static_cast<double>(INT64_MIN) || v > static_cast<double>(INT64_MAX))
            throw std::runtime_error("Value out of range for bitwise operation");
        return static_cast<int64_t>(v);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Grammar rules  (see header comment for full precedence table)
    // ─────────────────────────────────────────────────────────────────────────

    // assignment := ternary [ '=' assignment ]   (right-associative)
    // F2: Only treat '=' as assignment when NOT preceded/followed by '='/'!'/'<'/'>'
    double parseAssignment()
    {
        // Snapshot position to rewind if needed
        size_t savedPos = m_pos;

        // Try to read an identifier — if it is followed by '=' (but not ==)
        // treat this as an assignment.
        skipWhitespace();
        if (m_pos < m_expr.size() && (std::isalpha(static_cast<unsigned char>(m_expr[m_pos]))
                                       || m_expr[m_pos] == '_'))
        {
            size_t nameStart = m_pos;
            std::string name;
            while (m_pos < m_expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) ||
                    m_expr[m_pos] == '_'))
            {
                name += m_expr[m_pos++];
            }
            skipWhitespace();

            // Assignment: single '=' not followed by another '='
            if (m_pos < m_expr.size() && m_expr[m_pos] == '=' &&
                (m_pos + 1 >= m_expr.size() || m_expr[m_pos + 1] != '='))
            {
                ++m_pos; // consume '='
                double value = parseAssignment(); // right-associative
                m_vars[name] = value;
                return value;
            }

            // Not an assignment — rewind and parse as a normal expression
            m_pos = savedPos;
        }

        return parseTernary();
    }

    // ternary := logical_or [ '?' expr ':' expr ]
    double parseTernary()
    {
        double cond = parseLogicalOr();
        skipWhitespace();
        if (m_pos < m_expr.size() && m_expr[m_pos] == '?') {
            ++m_pos;
            double vtrue  = parseAssignment();
            expect(':', "in ternary operator");
            double vfalse = parseAssignment();
            return (cond != 0.0) ? vtrue : vfalse;
        }
        return cond;
    }

    // logical_or := logical_and ( '||' logical_and )*
    double parseLogicalOr()
    {
        double lhs = parseLogicalAnd();
        while (true) {
            skipWhitespace();
            if (m_pos + 1 < m_expr.size() &&
                m_expr[m_pos] == '|' && m_expr[m_pos + 1] == '|')
            {
                m_pos += 2;
                double rhs = parseLogicalAnd();
                lhs = ((lhs != 0.0) || (rhs != 0.0)) ? 1.0 : 0.0;
            } else {
                break;
            }
        }
        return lhs;
    }

    // logical_and := bitwise_or ( '&&' bitwise_or )*
    double parseLogicalAnd()
    {
        double lhs = parseBitwiseOr();
        while (true) {
            skipWhitespace();
            if (m_pos + 1 < m_expr.size() &&
                m_expr[m_pos] == '&' && m_expr[m_pos + 1] == '&')
            {
                m_pos += 2;
                double rhs = parseBitwiseOr();
                lhs = ((lhs != 0.0) && (rhs != 0.0)) ? 1.0 : 0.0;
            } else {
                break;
            }
        }
        return lhs;
    }

    // bitwise_or := bitwise_xor ( '|' bitwise_xor )*
    double parseBitwiseOr()
    {
        double lhs = parseBitwiseXor();
        while (true) {
            skipWhitespace();
            // Single '|' not followed by '|'
            if (m_pos < m_expr.size() && m_expr[m_pos] == '|' &&
                (m_pos + 1 >= m_expr.size() || m_expr[m_pos + 1] != '|'))
            {
                ++m_pos;
                double rhs = parseBitwiseXor();
                lhs = static_cast<double>(toInt(lhs) | toInt(rhs));
            } else {
                break;
            }
        }
        return lhs;
    }

    // bitwise_xor := bitwise_and ( '^' bitwise_and )*
    // N5: '^' means XOR; power is '**'
    double parseBitwiseXor()
    {
        double lhs = parseBitwiseAnd();
        while (true) {
            skipWhitespace();
            if (m_pos < m_expr.size() && m_expr[m_pos] == '^')
            {
                ++m_pos;
                double rhs = parseBitwiseAnd();
                lhs = static_cast<double>(toInt(lhs) ^ toInt(rhs));
            } else {
                break;
            }
        }
        return lhs;
    }

    // bitwise_and := equality ( '&' equality )*
    double parseBitwiseAnd()
    {
        double lhs = parseEquality();
        while (true) {
            skipWhitespace();
            // Single '&' not followed by '&'
            if (m_pos < m_expr.size() && m_expr[m_pos] == '&' &&
                (m_pos + 1 >= m_expr.size() || m_expr[m_pos + 1] != '&'))
            {
                ++m_pos;
                double rhs = parseEquality();
                lhs = static_cast<double>(toInt(lhs) & toInt(rhs));
            } else {
                break;
            }
        }
        return lhs;
    }

    // equality := relational ( ('=='|'!=') relational )*
    double parseEquality()
    {
        double lhs = parseRelational();
        while (true) {
            skipWhitespace();
            if (m_pos + 1 < m_expr.size() &&
                m_expr[m_pos] == '=' && m_expr[m_pos + 1] == '=')
            {
                m_pos += 2;
                double rhs = parseRelational();
                lhs = (lhs == rhs) ? 1.0 : 0.0;
            }
            else if (m_pos + 1 < m_expr.size() &&
                     m_expr[m_pos] == '!' && m_expr[m_pos + 1] == '=')
            {
                m_pos += 2;
                double rhs = parseRelational();
                lhs = (lhs != rhs) ? 1.0 : 0.0;
            }
            else {
                break;
            }
        }
        return lhs;
    }

    // relational := shift ( ('<'|'<='|'>'|'>=') shift )*
    double parseRelational()
    {
        double lhs = parseShift();
        while (true) {
            skipWhitespace();
            if (m_pos < m_expr.size()) {
                char c = m_expr[m_pos];
                char c2 = (m_pos + 1 < m_expr.size()) ? m_expr[m_pos + 1] : '\0';

                if (c == '<' && c2 == '=') { m_pos += 2; double r = parseShift(); lhs = (lhs <= r) ? 1.0 : 0.0; }
                else if (c == '>' && c2 == '=') { m_pos += 2; double r = parseShift(); lhs = (lhs >= r) ? 1.0 : 0.0; }
                else if (c == '<' && c2 != '<') { ++m_pos;    double r = parseShift(); lhs = (lhs <  r) ? 1.0 : 0.0; }
                else if (c == '>' && c2 != '>') { ++m_pos;    double r = parseShift(); lhs = (lhs >  r) ? 1.0 : 0.0; }
                else break;
            } else {
                break;
            }
        }
        return lhs;
    }

    // shift := additive ( ('<<'|'>>') additive )*
    double parseShift()
    {
        double lhs = parseAdditive();
        while (true) {
            skipWhitespace();
            if (m_pos + 1 < m_expr.size() &&
                m_expr[m_pos] == '<' && m_expr[m_pos + 1] == '<')
            {
                m_pos += 2;
                double rhs = parseAdditive();
                lhs = static_cast<double>(toInt(lhs) << toInt(rhs));
            }
            else if (m_pos + 1 < m_expr.size() &&
                     m_expr[m_pos] == '>' && m_expr[m_pos + 1] == '>')
            {
                m_pos += 2;
                double rhs = parseAdditive();
                lhs = static_cast<double>(toInt(lhs) >> toInt(rhs));
            }
            else {
                break;
            }
        }
        return lhs;
    }

    // additive := term ( ('+'|'-') term )*
    double parseAdditive()
    {
        double lhs = parseTerm();
        while (true) {
            skipWhitespace();
            if (m_pos < m_expr.size() &&
                (m_expr[m_pos] == '+' || m_expr[m_pos] == '-'))
            {
                char op = m_expr[m_pos++];
                double rhs = parseTerm();
                lhs = (op == '+') ? lhs + rhs : lhs - rhs;
            } else {
                break;
            }
        }
        return lhs;
    }

    // term := unary ( ('*'|'/'|'//'|'%') unary )*
    double parseTerm()
    {
        double lhs = parseUnary();
        while (true) {
            skipWhitespace();
            if (m_pos < m_expr.size()) {
                // '//' — floor division
                if (m_pos + 1 < m_expr.size() &&
                    m_expr[m_pos] == '/' && m_expr[m_pos + 1] == '/')
                {
                    m_pos += 2;
                    double rhs = parseUnary();
                    if (rhs == 0.0)
                        throw std::runtime_error("Floor division by zero");
                    lhs = std::floor(lhs / rhs);
                }
                else if (m_expr[m_pos] == '*') { ++m_pos; lhs *= parseUnary(); }
                else if (m_expr[m_pos] == '/') {
                    ++m_pos;
                    double rhs = parseUnary();
                    if (rhs == 0.0)
                        throw std::runtime_error("Division by zero");
                    lhs /= rhs;
                }
                else if (m_expr[m_pos] == '%') {
                    ++m_pos;
                    double rhs = parseUnary();
                    if (rhs == 0.0)
                        throw std::runtime_error("Modulo by zero");
                    lhs = std::fmod(lhs, rhs);
                }
                else { break; }
            } else {
                break;
            }
        }
        return lhs;
    }

    // unary := ('+' | '-' | '!' | '~') unary  |  power
    double parseUnary()
    {
        skipWhitespace();
        if (m_pos < m_expr.size()) {
            if (m_expr[m_pos] == '+') { ++m_pos; return  parseUnary(); }
            if (m_expr[m_pos] == '-') { ++m_pos; return -parseUnary(); }
            if (m_expr[m_pos] == '!') { ++m_pos; return (parseUnary() == 0.0) ? 1.0 : 0.0; }
            if (m_expr[m_pos] == '~') { ++m_pos; return static_cast<double>(~toInt(parseUnary())); }
        }
        return parsePower();
    }

    // power := primary ('**' unary)*  right-associative
    double parsePower()
    {
        double base = parseImplicitMul();
        skipWhitespace();
        if (m_pos + 1 < m_expr.size() &&
            m_expr[m_pos] == '*' && m_expr[m_pos + 1] == '*')
        {
            m_pos += 2;
            double exp = parseUnary(); // right-assoc → recurse into unary
            return std::pow(base, exp);
        }
        return base;
    }

    // implicit_mul := primary (primary)*
    // Handles: 2pi   3(x+1)   (a+b)(a-b)
    double parseImplicitMul()
    {
        double result = parsePrimary();
        while (true) {
            skipWhitespace();
            if (m_pos >= m_expr.size()) break;
            char c = m_expr[m_pos];
            // Implicit multiply when next token starts with alpha/digit/'('
            // but only when the current character is NOT an operator
            if (c == '(' ||
                std::isalpha(static_cast<unsigned char>(c)) ||
                c == '_')
            {
                result *= parsePrimary();
            } else {
                break;
            }
        }
        return result;
    }

    // primary := number | identifier_or_func | '(' expr ')'
    double parsePrimary()
    {
        skipWhitespace();
        if (m_pos >= m_expr.size())
            throw std::runtime_error("Unexpected end of expression");

        if (m_expr[m_pos] == '(') {
            ++m_pos;
            double result = parseAssignment();
            expect(')', "closing sub-expression");
            return result;
        }

        if (std::isalpha(static_cast<unsigned char>(m_expr[m_pos])) ||
            m_expr[m_pos] == '_')
        {
            return parseFunctionOrVariable();
        }

        return parseNumber();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Number parser — F3: supports scientific notation, unary sign handled
    // by parseUnary so parseNumber only handles the unsigned numeric literal.
    // N11: supports 0b/0B (binary), 0x/0X (hex), 0o/0O (octal explicit),
    //      and legacy C-style 0NNN... (octal) prefixes.
    // ─────────────────────────────────────────────────────────────────────────
    double parseNumber()
    {
        skipWhitespace();
        size_t start = m_pos;

        if (m_pos >= m_expr.size())
            throw std::runtime_error(
                std::string("Expected number at position ") + std::to_string(m_pos));

        // ── Non-decimal prefix literals (0b / 0o / 0x / legacy octal) ────────
        if (m_expr[m_pos] == '0' && m_pos + 1 < m_expr.size())
        {
            char next = m_expr[m_pos + 1];

            // Binary: 0b / 0B
            if (next == 'b' || next == 'B')
            {
                m_pos += 2;
                size_t digitStart = m_pos;
                while (m_pos < m_expr.size() &&
                       (m_expr[m_pos] == '0' || m_expr[m_pos] == '1'))
                    ++m_pos;
                if (m_pos == digitStart)
                    throw std::runtime_error(
                        "Binary literal (0b) has no digits at position " +
                        std::to_string(start));
                std::string digits = m_expr.substr(digitStart, m_pos - digitStart);
                return static_cast<double>(std::stoull(digits, nullptr, 2));
            }

            // Hexadecimal: 0x / 0X
            if (next == 'x' || next == 'X')
            {
                m_pos += 2;
                size_t digitStart = m_pos;
                while (m_pos < m_expr.size() &&
                       std::isxdigit(static_cast<unsigned char>(m_expr[m_pos])))
                    ++m_pos;
                if (m_pos == digitStart)
                    throw std::runtime_error(
                        "Hexadecimal literal (0x) has no digits at position " +
                        std::to_string(start));
                std::string digits = m_expr.substr(digitStart, m_pos - digitStart);
                return static_cast<double>(std::stoull(digits, nullptr, 16));
            }

            // Explicit octal: 0o / 0O
            if (next == 'o' || next == 'O')
            {
                m_pos += 2;
                size_t digitStart = m_pos;
                while (m_pos < m_expr.size() &&
                       m_expr[m_pos] >= '0' && m_expr[m_pos] <= '7')
                    ++m_pos;
                if (m_pos == digitStart)
                    throw std::runtime_error(
                        "Octal literal (0o) has no digits at position " +
                        std::to_string(start));
                std::string digits = m_expr.substr(digitStart, m_pos - digitStart);
                return static_cast<double>(std::stoull(digits, nullptr, 8));
            }

            // Legacy C-style octal: leading '0' followed by more octal digits,
            // but NOT followed by '.', 'e'/'E' (those are decimal floats).
            if (next >= '0' && next <= '7')
            {
                size_t probe = m_pos + 1;
                while (probe < m_expr.size() &&
                       (m_expr[probe] >= '0' && m_expr[probe] <= '7'))
                    ++probe;
                // If the next non-octal character is '8','9','.','e','E'
                // fall through to the normal decimal path.
                bool isLegacyOctal = true;
                if (probe < m_expr.size() &&
                    (m_expr[probe] == '8' || m_expr[probe] == '9' ||
                     m_expr[probe] == '.' ||
                     m_expr[probe] == 'e' || m_expr[probe] == 'E'))
                    isLegacyOctal = false;

                if (isLegacyOctal)
                {
                    m_pos = probe;
                    std::string digits = m_expr.substr(start, m_pos - start);
                    return static_cast<double>(std::stoull(digits, nullptr, 8));
                }
                // else fall through to decimal float handling below
            }
        }

        // ── Decimal integer / float (including scientific notation) ──────────

        // integer or decimal part
        while (m_pos < m_expr.size() &&
               (std::isdigit(static_cast<unsigned char>(m_expr[m_pos])) ||
                m_expr[m_pos] == '.'))
            ++m_pos;

        // optional scientific notation: e/E followed by optional sign and digits
        if (m_pos < m_expr.size() &&
            (m_expr[m_pos] == 'e' || m_expr[m_pos] == 'E'))
        {
            size_t savedPos = m_pos;
            ++m_pos;
            if (m_pos < m_expr.size() &&
                (m_expr[m_pos] == '+' || m_expr[m_pos] == '-'))
                ++m_pos;
            if (m_pos < m_expr.size() &&
                std::isdigit(static_cast<unsigned char>(m_expr[m_pos])))
            {
                while (m_pos < m_expr.size() &&
                       std::isdigit(static_cast<unsigned char>(m_expr[m_pos])))
                    ++m_pos;
            } else {
                m_pos = savedPos; // not a valid exponent — rewind
            }
        }

        if (start == m_pos)
            throw std::runtime_error(
                std::string("Expected number at position ") + std::to_string(m_pos));

        return std::stod(m_expr.substr(start, m_pos - start));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Identifier, variable, and function dispatch
    // F4: variable names allow alphanumeric + '_' (digits allowed after first char)
    // ─────────────────────────────────────────────────────────────────────────
    double parseFunctionOrVariable()
    {
        std::string name;
        while (m_pos < m_expr.size() &&
               (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) ||
                m_expr[m_pos] == '_'))
        {
            name += m_expr[m_pos++];
        }

        skipWhitespace();

        // Function call
        if (m_pos < m_expr.size() && m_expr[m_pos] == '(') {
            ++m_pos;
            return dispatchFunction(name);
        }

        // Variable / constant lookup
        auto it = m_vars.find(name);
        if (it != m_vars.end())
            return it->second;

        throw std::runtime_error("Undefined variable: " + name);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Function dispatch — single-arg and two-arg
    // ─────────────────────────────────────────────────────────────────────────
    double dispatchFunction(const std::string& name)
    {
        // Helper: read first argument (already past the opening '(')
        auto readArg = [&]() -> double {
            double v = parseAssignment();
            skipWhitespace();
            return v;
        };

        // Helper: expect comma
        auto comma = [&]() {
            expect(',', ("in function '" + name + "'").c_str());
            skipWhitespace();
        };

        // Helper: close paren
        auto close = [&]() {
            expect(')', ("closing '" + name + "()'").c_str());
        };

        // ── Single-argument functions ──────────────────────────────────────

        // Trigonometric
        if (name == "sin")   { double a = readArg(); close(); return std::sin(a); }
        if (name == "cos")   { double a = readArg(); close(); return std::cos(a); }
        if (name == "tan")   { double a = readArg(); close(); return std::tan(a); }
        if (name == "asin")  {
            double a = readArg(); close();
            if (a < -1.0 || a > 1.0) throw std::runtime_error("asin: domain error (|x| > 1)");
            return std::asin(a);
        }
        if (name == "acos")  {
            double a = readArg(); close();
            if (a < -1.0 || a > 1.0) throw std::runtime_error("acos: domain error (|x| > 1)");
            return std::acos(a);
        }
        if (name == "atan")  { double a = readArg(); close(); return std::atan(a); }
        if (name == "sinh")  { double a = readArg(); close(); return std::sinh(a); }
        if (name == "cosh")  { double a = readArg(); close(); return std::cosh(a); }
        if (name == "tanh")  { double a = readArg(); close(); return std::tanh(a); }

        // Exponential / logarithmic
        if (name == "sqrt")  {
            double a = readArg(); close();
            if (a < 0.0) throw std::runtime_error("sqrt: domain error (negative argument)");
            return std::sqrt(a);
        }
        if (name == "cbrt")  { double a = readArg(); close(); return std::cbrt(a); }
        if (name == "exp")   { double a = readArg(); close(); return std::exp(a); }
        if (name == "exp2")  { double a = readArg(); close(); return std::exp2(a); }
        if (name == "log")   {
            double a = readArg(); close();
            if (a <= 0.0) throw std::runtime_error("log: domain error (argument <= 0)");
            return std::log(a);
        }
        if (name == "log2")  {
            double a = readArg(); close();
            if (a <= 0.0) throw std::runtime_error("log2: domain error (argument <= 0)");
            return std::log2(a);
        }
        if (name == "log10") {
            double a = readArg(); close();
            if (a <= 0.0) throw std::runtime_error("log10: domain error (argument <= 0)");
            return std::log10(a);
        }

        // Rounding
        if (name == "abs")   { double a = readArg(); close(); return std::abs(a); }
        if (name == "ceil")  { double a = readArg(); close(); return std::ceil(a); }
        if (name == "floor") { double a = readArg(); close(); return std::floor(a); }
        if (name == "round") { double a = readArg(); close(); return std::round(a); }
        if (name == "trunc") { double a = readArg(); close(); return std::trunc(a); }

        // Sign
        if (name == "sign")  {
            double a = readArg(); close();
            return (a > 0.0) ? 1.0 : (a < 0.0) ? -1.0 : 0.0;
        }

        // ── Two-argument functions ─────────────────────────────────────────

        if (name == "pow") {
            double base = readArg(); comma();
            double exp  = readArg(); close();
            return std::pow(base, exp);
        }
        if (name == "atan2") {
            double y = readArg(); comma();
            double x = readArg(); close();
            return std::atan2(y, x);
        }
        if (name == "min") {
            double a = readArg(); comma();
            double b = readArg(); close();
            return std::min(a, b);
        }
        if (name == "max") {
            double a = readArg(); comma();
            double b = readArg(); close();
            return std::max(a, b);
        }
        if (name == "hypot") {
            double a = readArg(); comma();
            double b = readArg(); close();
            return std::hypot(a, b);
        }
        if (name == "fmod") {
            double a = readArg(); comma();
            double b = readArg(); close();
            if (b == 0.0) throw std::runtime_error("fmod: second argument is zero");
            return std::fmod(a, b);
        }
        if (name == "log_b") {
            // log_b(value, base) = log(value) / log(base)
            double v = readArg(); comma();
            double b = readArg(); close();
            if (v <= 0.0) throw std::runtime_error("log_b: value must be > 0");
            if (b <= 0.0 || b == 1.0) throw std::runtime_error("log_b: base must be > 0 and != 1");
            return std::log(v) / std::log(b);
        }

        throw std::runtime_error("Unknown function: " + name);
    }
};

#endif // UCALCULATOR_HPP
