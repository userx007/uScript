#ifndef UEVALUATOR_H
#define UEVALUATOR_H

#include "uString.hpp"
#include "uLogger.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <regex>

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "EVALUATOR  :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                     IMPLEMENTATION                            //
///////////////////////////////////////////////////////////////////

namespace eval {


inline bool string2bool(std::string_view token, bool& result)
{
    static const std::unordered_map<std::string_view, bool> token_map = {
        {"TRUE",   true},  {"!FALSE", true},
        {"FALSE",  false}, {"!TRUE", false}
    };

    auto it = token_map.find(token);
    if (it != token_map.end()) {
        result = it->second;
        return true;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid string for boolean:"); LOG_STRING(std::string(token)));
    return false;
}



inline bool isMathOperator(const std::string& op)
{
    static const std::unordered_set<std::string> validOperators = {
        "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
    };

    return validOperators.count(op) > 0;
}



inline bool isStringValidationRule (const std::string &strRule)
{
    static const std::unordered_set<std::string> validRules {"EQ", "NE", "eq", "ne"};
    return validRules.count(strRule) > 0;
}



inline bool isNumericValidationRule (const std::string &strRule)
{
    static const std::unordered_set<std::string> validRules {"<", "<=", "==", "!=", ">", ">="};
    return validRules.count(strRule) > 0;
}



inline bool isMathRule (const std::string &strRule)
{
    static const std::unordered_set<std::string> validRules {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"};
    return validRules.count(strRule) > 0;
}



inline bool isValidVectorOfNumbers (const std::string& input)
{
    static const std::regex rx(R"(^\s*(0[xX][0-9A-Fa-f]+|\d+)(\s+(0[xX][0-9A-Fa-f]+|\d+))*\s*$)",std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input, rx);
}



inline bool isValidVectorOfStrings (const std::string& input)
{
    static const std::regex rx(R"(^\s*(\w+)(\s+\w+)*\s*$)",std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input, rx);
}



inline bool isValidVectorOfBools (const std::string& input)
{
    static const std::regex rx(R"(^(?:\s*(?:!?(?:TRUE|FALSE))\s*)+$)",std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input, rx);
}



inline bool isValidVersion(const std::string& input)
{
    static const std::regex rgx(R"(^\d+(\.\d+){1,3}$)", std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input, rgx);
}



inline bool validateVectorBooleans(const std::string& boolString, const std::string& rule, bool& outResult)
{
    enum class BoolRule { OR, AND };

    BoolRule evalRule;

    if      (rule == "OR")  evalRule = BoolRule::OR;
    else if (rule == "AND") evalRule = BoolRule::AND;
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid boolean rule:"); LOG_STRING(rule); LOG_STRING("use AND OR"));
        return false;
    }

    std::vector<std::string> vstrBools;
    ustring::tokenize(boolString, vstrBools);
    std::vector<bool> values;

    for (const auto& token : vstrBools) {
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
        outResult = std::any_of(values.begin(), values.end(), [](bool b) { return b; });
    } else {
        outResult = std::all_of(values.begin(), values.end(), [](bool b) { return b; });
    }

    return true;
}

} // namespace eval


#endif // UEVALUATOR_H