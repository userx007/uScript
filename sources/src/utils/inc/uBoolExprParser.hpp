#ifndef BOOLEAN_EXPRESSION_PARSER_HPP
#define BOOLEAN_EXPRESSION_PARSER_HPP

#include <string_view>
#include <cctype>

class BoolExprParser
{
    public:

        constexpr bool evaluate(std::string_view input, bool& result) const {
            try {
                result = false;
                bool success = parseExpression(input, result);
                return success && input.empty(); // Ensure full input was consumed
            } catch (...) {
                return false;
            }
        }

    private:

        constexpr bool parseExpression(std::string_view& expr, bool& result) const {
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

        constexpr bool parseTerm(std::string_view& expr, bool& result) const {
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

        constexpr bool parseFactor(std::string_view& expr, bool& result) const {
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

        constexpr void skipWhitespace(std::string_view& expr) const {
            while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.front()))) {
                expr.remove_prefix(1);
            }
        }
};

#endif // BOOLEAN_EXPRESSION_PARSER_HPP



