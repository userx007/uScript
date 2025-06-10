#ifndef BOOLEAN_EXPRESSION_PARSER_HPP
#define BOOLEAN_EXPRESSION_PARSER_HPP

#define BOOL_EXPR_PARSER_LOGGING_ENABLED 1U

#ifdef BOOL_EXPR_PARSER_LOGGING_ENABLED
    #include "uLogger.hpp"
    #define BEP_LOG(SEVERITY, ...) LOG_PRINT(SEVERITY, __VA_ARGS__)
    #undef  LOG_HDR
    #define LOG_HDR     LOG_STRING("BOOLEXPRP:");
#else
    /* disable logging */
    #define BEP_LOG(SEVERITY, ...)
#endif

#include <string_view>
#include <cctype>


class BoolExprParser
{
public:
    constexpr bool evaluate(std::string_view input, bool& result) const
    {
        try {
            result = false;
            bool success = parseExpression(input, result);
            if (!(success && input.empty())) { // Ensure full input was consumed
                BEP_LOG(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to evaluate the boolean expression, remained:"); LOG_STRING(input));
                return false;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    constexpr bool parseExpression(std::string_view& expr, bool& result) const
    {
        bool lhs;
        if (!parseTerm(expr, lhs)) return false;

        while (true) {
            skipWhitespace(expr);
            if (expr.starts_with("||")) {
                expr.remove_prefix(2);
                bool rhs;
                if (!parseTerm(expr, rhs)) return false;
                lhs = lhs || rhs;
            } else {
                break;
            }
        }
        result = lhs;
        return true;
    }

    constexpr bool parseTerm(std::string_view& expr, bool& result) const
    {
        bool lhs;
        if (!parseFactor(expr, lhs)) return false;

        while (true) {
            skipWhitespace(expr);
            if (expr.starts_with("&&")) {
                expr.remove_prefix(2);
                bool rhs;
                if (!parseFactor(expr, rhs)) return false;
                lhs = lhs && rhs;
            } else {
                break;
            }
        }
        result = lhs;
        return true;
    }

    constexpr bool parseFactor(std::string_view& expr, bool& result) const
    {
        skipWhitespace(expr);

        if (expr.starts_with("!")) {
            expr.remove_prefix(1);
            bool inner;
            if (!parseFactor(expr, inner)) return false;
            result = !inner;
            return true;
        }

        if (expr.starts_with("(")) {
            expr.remove_prefix(1);
            if (!parseExpression(expr, result)) return false;
            skipWhitespace(expr);
            if (!expr.starts_with(")")) return false;
            expr.remove_prefix(1);
            return true;
        }

        if (expr.starts_with("TRUE")) {
            expr.remove_prefix(4);
            result = true;
            return true;
        }

        if (expr.starts_with("FALSE")) {
            expr.remove_prefix(5);
            result = false;
            return true;
        }

        return false; // Unexpected token
    }

    constexpr void skipWhitespace(std::string_view& expr) const
    {
        while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.front()))) {
            expr.remove_prefix(1);
        }
    }
};

#endif // BOOLEAN_EXPRESSION_PARSER_HPP



