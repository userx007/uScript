#ifndef U_MATH_OPS_EVALUATOR_HPP
#define U_MATH_OPS_EVALUATOR_HPP

#include "uLogger.hpp"
#include "uString.hpp"

#include <algorithm>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "EXPR_EVAL   |"
#define LOG_HDR LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                     IMPLEMENTATION                            //
///////////////////////////////////////////////////////////////////

namespace eval
{

    /** Parses TRUE/FALSE/!TRUE/!FALSE into @p bResult; returns false for any other token. */
    inline bool string2bool(std::string_view token, bool &bResult)
    {
        static const std::unordered_map<std::string_view, bool> token_map = {
            {"TRUE", true}, {"!FALSE", true}, {"FALSE", false}, {"!TRUE", false}};

        auto it = token_map.find(token);
        if (it != token_map.end()) {
            bResult = it->second;
            return true;
        }

        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid string for boolean:"); LOG_STRING(std::string(token)));
        return false;
    }

    /** True if @p strOp is a supported arithmetic or bitwise operator. */
    inline bool isMathOperator(const std::string &strOp)
    {
        static const std::unordered_set<std::string> validOperators = {
            "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};

        return validOperators.count(strOp) > 0;
    }

    /** True if @p strRule is a string comparison rule (EQ/NE). */
    inline bool isStringValidationRule(const std::string &strRule)
    {
        static const std::unordered_set<std::string> validRules{"EQ", "NE", "eq", "ne"};
        return validRules.count(strRule) > 0;
    }

    /** True if @p strRule is a numeric comparison rule (<, <=, ==, !=, >, >=). */
    inline bool isNumericValidationRule(const std::string &strRule)
    {
        static const std::unordered_set<std::string> validRules{"<", "<=", "==", "!=", ">", ">="};
        return validRules.count(strRule) > 0;
    }

    /** True if @p strRule is a math rule (+, -, *, /, %, &, |, ^, <<, >>). */
    inline bool isMathRule(const std::string &strRule)
    {
        static const std::unordered_set<std::string> validRules{"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"};
        return validRules.count(strRule) > 0;
    }

    /** True if @p strInput is a whitespace-separated list of decimal or 0x-hex numbers. */
    inline bool isValidVectorOfNumbers(const std::string &strInput)
    {
        static const std::regex rx(R"(^\s*(0[xX][0-9A-Fa-f]+|\d+)(\s+(0[xX][0-9A-Fa-f]+|\d+))*\s*$)", std::regex::ECMAScript | std::regex::optimize);
        return std::regex_match(strInput, rx);
    }

    /** True if @p strInput is a whitespace-separated list of words. */
    inline bool isValidVectorOfStrings(const std::string &strInput)
    {
        static const std::regex rx(R"(^\s*(\w+)(\s+\w+)*\s*$)", std::regex::ECMAScript | std::regex::optimize);
        return std::regex_match(strInput, rx);
    }

    /** True if @p strInput is a list of TRUE/FALSE tokens, each optionally prefixed by `!`. */
    inline bool isValidVectorOfBools(const std::string &strInput)
    {
        static const std::regex rx(R"(^(?:\s*(?:!?(?:TRUE|FALSE))\s*)+$)", std::regex::ECMAScript | std::regex::optimize);
        return std::regex_match(strInput, rx);
    }

    /** True if @p strInput is a dotted version with 2-4 numeric components. */
    inline bool isValidVersion(const std::string &strInput)
    {
        static const std::regex rgx(R"(^\d+(\.\d+){1,3}$)", std::regex::ECMAScript | std::regex::optimize);
        return std::regex_match(strInput, rgx);
    }

    /** Applies @p strRule to the boolean vector in @p strBoolString and stores the outcome in @p bOutResult. */
    inline bool validateVectorBooleans(const std::string &strBoolString, const std::string &strRule, bool &bOutResult)
    {
        enum class BoolRule
        {
            OR,
            AND
        };

        BoolRule evalRule;

        if (strRule == "OR") {
            evalRule = BoolRule::OR;
        } else if (strRule == "AND") {
            evalRule = BoolRule::AND;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid boolean rule:"); LOG_STRING(strRule); LOG_STRING("use AND OR"));
            return false;
        }

        std::vector<std::string> vstrBools;
        ustring::tokenize(strBoolString, vstrBools);
        std::vector<bool> values;

        for (const auto &token : vstrBools) {
            bool val;
            if (false == string2bool(token, val)) {
                return false;
            }
            values.push_back(val);
        }

        if (values.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty vector of booleans"));
            return false;
        }

        if (evalRule == BoolRule::OR) {
            bOutResult = std::any_of(values.begin(), values.end(), [](bool b) { return b; });
        } else {
            bOutResult = std::all_of(values.begin(), values.end(), [](bool b) { return b; });
        }

        return true;
    }

} // namespace eval

#endif // U_MATH_OPS_EVALUATOR_HPP