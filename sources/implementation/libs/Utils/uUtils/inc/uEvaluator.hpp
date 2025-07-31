#ifndef UEVALUATOR_H
#define UEVALUATOR_H

#include "uString.hpp"

#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>


namespace eval {


using CMPFCTVUI = std::function<bool(const VUI&, const VUI&)>;
static const std::map<std::string, CMPFCTVUI> g_uiRuleMap{
    { "<",  [](VUI v1, VUI v2) { return v1 <  v2; } },
    { "<=", [](VUI v1, VUI v2) { return v1 <= v2; } },
    { "==", [](VUI v1, VUI v2) { return v1 == v2; } },
    { "!=", [](VUI v1, VUI v2) { return v1 != v2; } },
    { ">=", [](VUI v1, VUI v2) { return v1 >= v2; } },
    { ">",  [](VUI v1, VUI v2) { return v1 >  v2; } }
};



using CMPFCTVSTR = std::function<bool(const VSTR&, const VSTR&)>;
static const std::map<std::string, CMPFCTVSTR> g_strRuleMap{
    { "EQ", [](const VSTR& v1, const VSTR& v2) { return  vstring_ssame(v1, v2); } },
    { "NE", [](const VSTR& v1, const VSTR& v2) { return !vstring_ssame(v1, v2); } },
    { "eq", [](const VSTR& v1, const VSTR& v2) { return  vstring_isame(v1, v2); } },
    { "ne", [](const VSTR& v1, const VSTR& v2) { return !vstring_isame(v1, v2); } }
};



using MATHFCTUI32 = std::function<uint64_t(uint32_t, uint32_t)>;
static const std::map<std::string, MATHFCTUI32> g_uiMathRuleMap{
    { "+",    [](auto v1, auto v2) { return v1 + v2; } },
    { "-",    [](auto v1, auto v2) { return v1 - v2; } },
    { "*",    [](auto v1, auto v2) { return v1 * v2; } },
    { "/",    [](auto v1, auto v2) { if (!v2) throw std::runtime_error("divide by 0"); return v1 / v2; } },
    { "%",    [](auto v1, auto v2) { if (!v2) throw std::runtime_error("divide by 0"); return v1 % v2; } },
    // Bitwise ops...
    { "&",    [](auto v1, auto v2) { return v1 & v2; } },
    { "|",    [](auto v1, auto v2) { return v1 | v2; } },
    { "^",    [](auto v1, auto v2) { return v1 ^ v2; } },
    { "<<",   [](auto v1, auto v2) { return v1 << v2; } },
    { ">>",   [](auto v1, auto v2) { return v1 >> v2; } }
};



bool string2bool(std::string_view token, bool& result)
{
    static const std::unordered_map<std::string_view, bool> token_map = {
        {"TRUE",   true}, {"!FALSE", true}, {"1", true}, {"!0", true},
        {"FALSE",  false}, {"!TRUE", false}, {"0", false}, {"!1", false}
    };

    auto it = token_map.find(token);
    if (it != token_map.end()) {
        result = it->second;
        return true;
    }

    return false;
}


bool isMathOperator(const std::string& op) {
    static const std::unordered_set<std::string> validOperators = {
        "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
    };

    return validOperators.count(op) > 0;
}




bool isStringValidationRule (const std::string &strRule)
{
    std::vector<std::string> vstrStringRules {"EQ", "NE", "eq", "ne"};
    return (find(vstrStringRules.begin(), vstrStringRules.end(), strRule) != vstrStringRules.end());
}



bool isNumericValidationRule (const std::string &strRule)
{
    std::vector<std::string> vstrStringRules {"<", "<=", "==", "!=", ">", ">="};
    return (find(vstrStringRules.begin(), vstrStringRules.end(), strRule) != vstrStringRules.end());
}



bool isMathRule (const std::string &strRule)
{
    std::vector<std::string> vstrStringRules {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"};
    return (find(vstrStringRules.begin(), vstrStringRules.end(), strRule) != vstrStringRules.end());
}



bool isValidVectorOfNumbers (const std::string& input)
{
    static const std::regex rx(R"(^\s*(0[xX][0-9A-Fa-f]+|\d+)(\s+(0[xX][0-9A-Fa-f]+|\d+))*\s*$)",std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input, rx);
}



bool isValidVectorOfStrings (const std::string& input)
{
    static const std::regex rx(R"(^\s*(\w+)(\s+\w+)*\s*$)",std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input, rx);
}



bool isValidVersion(const std::string& input)
{
    static const std::regex rgx(R"(^\d+(\.\d+){1,3}$)", std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input, rgx);
}



bool validateVectorBooleans(const std::string& boolString, const std::string& rule, bool& outResult)
{
    enum class BoolRule { OR, AND };

    BoolRule evalRule;
    if (rule == "OR")       evalRule = BoolRule::OR;
    else if (rule == "AND") evalRule = BoolRule::AND;
    else                    return false;

    auto rawTokens = ustring::splitTokens(boolString);
    std::vector<bool> values;

    for (const auto& token : rawTokens) {
        bool val;
        if (false == string2bool(token, val)) {
            return false;
        }
        values.push_back(val);
    }

    if (values.empty()) {
        return false;
    }

    if (evalRule == BoolRule::OR) {
        outResult = std::any_of(values.begin(), values.end(), [](bool b) { return b; });
    } else {
        outResult = std::all_of(values.begin(), values.end(), [](bool b) { return b; });
    }

    return true;
}



template <typename T>
bool validateSizes(const T& v1, const T& v2)
{
    if (v1.size() != v2.size())
    {
        DLT_LOG(ValidHdlCtx, DLT_LOG_ERROR,DLT_HDR; DLT_STRING("Items have different sizes:"); DLT_UINT32(static_cast<uint32_t>(v1.size())); DLT_UINT32(static_cast<uint32_t>(v2.size())));
        return false;
    }

    return true;
}



bool validateVui(const VUI& v1, const VUI& v2, const std::string& strRule, bool& bResult)
{
    if (!validateSizes(v1, v2))
        return false;

    auto it = g_uiRuleMap.find(strRule);
    if (it == g_uiRuleMap.end())
    {
        DLT_LOG(ValidHdlCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Invalid rule:"); DLT_STRING(strRule));
        return false;
    }

    bResult = it->second(v1, v2);
    return true;
}



bool validateVstr(const VSTR& v1, const VSTR& v2, const std::string& strRule, bool& bResult)
{
    if (!validateSizes(v1, v2))
        return false;

    auto it = g_strRuleMap.find(strRule);
    if (it == g_strRuleMap.end())
    {
        DLT_LOG(ValidHdlCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Invalid rule:"); DLT_STRING(strRule));
        return false;
    }

    bResult = it->second(v1, v2);
    return true;
}



bool validateVersion(const std::string& strVers1, const std::string& strVers2, const std::string& strRule, bool& bResult )
{
    bool bResult = false;
    std::vector<uint32_t> vu32Vers1;
    std::vector<uint32_t> vu32Vers2;

    ustring::tokenize(strVers1, CHAR_SEPARATOR_DOT, vu32Vers1);
    ustring::tokenize(strVers2, CHAR_SEPARATOR_DOT, vu32Vers2);

    return validate_vui( vu32Vers1, vu32Vers2, strRule, bResult) && bResult;
}



bool math_vectors_numbers(const std::string& strVect1, const std::string& strVect2, const std::string& strRule, std::vector<uint64_t>& vu64Result)
{
    auto ruleIt = g_uiMathRuleMap.find(strRule);

    if (ruleIt == g_uiMathRuleMap.end()) {
        DLT_LOG(ValidHdlCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Invalid rule:"); DLT_STRING(strRule.c_str()));
        return false;
    }

    // Tokenize input strings into vectors
    std::vector<uint32_t> vu32Vect1, vu32Vect2;
    if (!string_tokenize(strVect1, STRING_SEPARATOR_SPACE, vu32Vect1)) {
        DLT_LOG(ValidHdlCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("V1: invalid number ["); DLT_STRING(strVect1.c_str()); DLT_STRING("]"));
        return false;
    }

    if (!string_tokenize(strVect2, STRING_SEPARATOR_SPACE, vu32Vect2)) {
        DLT_LOG(ValidHdlCtx, DLT_LOG_ERROR,DLT_HDR; DLT_STRING("V2: invalid number ["); DLT_STRING(strVect2.c_str()); DLT_STRING("]"));
        return false;
    }

    // Validate sizes
    if (!validateSizes<VUI>(vu32Vect1, vu32Vect2)) {
        return false;
    }

    // Perform math operations with exception handling
    const auto& mathOp = ruleIt->second;
    try {
        vu64Result.reserve(vu32Vect1.size()); // Optional optimization
        for (size_t i = 0; i < vu32Vect1.size(); ++i) {
            vu64Result.push_back(mathOp(vu32Vect1[i], vu32Vect2[i]));
        }
    } catch (const char* pstrErrorMessage) {
        DLT_LOG(ValidHdlCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Math exception detected:"); DLT_STRING(pstrErrorMessage));
        return false;
    }

    return true;
}


}; // namespace eval


#endif // UEVALUATOR_H