#ifndef UVECTORVALIDATOR_HPP
#define UVECTORVALIDATOR_HPP

#include "uLogger.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <functional>

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "VECTOR_VAL :"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                     CLASS IMPLEMENTATION                      //
///////////////////////////////////////////////////////////////////

enum class eValidateType
{
    STRING,
    NUMBER,
    VERSION,
    BOOLEAN
};

enum class ComparisonOp
{
    EQ,      // ==, EQ, eq
    NE,      // !=, NE, ne
    LT,      // 
    LE,      // <=
    GT,      // >
    GE,      // >=
    UNKNOWN
};

class VectorValidator
{
public:
    VectorValidator()
    {
        initializeRuleMaps();
    }

    // Main validation method - now takes const references
    bool validate(const std::vector<std::string>& v1,
                  const std::vector<std::string>& v2,
                  const std::string& rule,
                  eValidateType type) const
    {
        // Handle empty vectors
        if (v1.empty() && v2.empty()) {
            return evaluateEmptyVectors(rule);
        }

        // Check size mismatch
        if (v1.size() != v2.size()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Vector sizes do not match: "); 
                     LOG_SIZET(v1.size()); LOG_STRING(" vs "); LOG_SIZET(v2.size()));
            return false;
        }

        // Validate rule upfront
        ComparisonOp op = parseRule(rule, type);
        if (op == ComparisonOp::UNKNOWN) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid rule: "); LOG_STRING(rule));
            return false;
        }

        // Compare each element
        for (size_t i = 0; i < v1.size(); ++i) {
            if (!compare(v1[i], v2[i], op, type)) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; 
                         LOG_STRING("Validation failed at index "); LOG_SIZET(i); 
                         LOG_STRING(": '"); LOG_STRING(v1[i]); 
                         LOG_STRING("' vs '"); LOG_STRING(v2[i]); LOG_STRING("'"));
                return false;
            }
        }
        return true;
    }

private:
    // Rule parsing maps
    std::unordered_map<std::string, ComparisonOp> string_rules_;
    std::unordered_map<std::string, ComparisonOp> numeric_rules_;

    void initializeRuleMaps()
    {
        // String rules (case-sensitive and insensitive)
        string_rules_["EQ"] = ComparisonOp::EQ;
        string_rules_["NE"] = ComparisonOp::NE;
        string_rules_["eq"] = ComparisonOp::EQ;
        string_rules_["ne"] = ComparisonOp::NE;
        string_rules_["=="] = ComparisonOp::EQ;
        string_rules_["!="] = ComparisonOp::NE;

        // Numeric/Version/Boolean rules
        numeric_rules_["=="] = ComparisonOp::EQ;
        numeric_rules_["!="] = ComparisonOp::NE;
        numeric_rules_["<"]  = ComparisonOp::LT;
        numeric_rules_["<="] = ComparisonOp::LE;
        numeric_rules_[">"]  = ComparisonOp::GT;
        numeric_rules_[">="] = ComparisonOp::GE;
    }

    ComparisonOp parseRule(const std::string& rule, eValidateType type) const
    {
        if (type == eValidateType::STRING) {
            auto it = string_rules_.find(rule);
            return (it != string_rules_.end()) ? it->second : ComparisonOp::UNKNOWN;
        } else {
            auto it = numeric_rules_.find(rule);
            return (it != numeric_rules_.end()) ? it->second : ComparisonOp::UNKNOWN;
        }
    }

    bool compare(const std::string& a, const std::string& b, 
                 ComparisonOp op, eValidateType type) const
    {
        try {
            switch (type) {
                case eValidateType::STRING:
                    return compareStrings(a, b, op);
                case eValidateType::NUMBER:
                    return compareUInt64(a, b, op);
                case eValidateType::VERSION:
                    return compareVersions(a, b, op);
                case eValidateType::BOOLEAN:
                    return compareBooleans(a, b, op);
                default:
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown validation type"));
                    return false;
            }
        } catch (const std::exception& ex) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                     LOG_STRING("Comparison failed: "); LOG_STRING(ex.what());
                     LOG_STRING(" (values: '"); LOG_STRING(a); 
                     LOG_STRING("', '"); LOG_STRING(b); LOG_STRING("')"));
            return false;
        }
    }

    bool compareStrings(const std::string& a, const std::string& b, ComparisonOp op) const
    {
        // Check if case-insensitive comparison is needed
        bool case_insensitive = (op == ComparisonOp::EQ || op == ComparisonOp::NE);
        
        if (case_insensitive && a.size() == b.size()) {
            // Optimized case-insensitive comparison without allocation
            bool equal = std::equal(a.begin(), a.end(), b.begin(),
                [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
            
            return (op == ComparisonOp::EQ) ? equal : !equal;
        }
        
        // Case-sensitive or different sizes
        bool equal = (a == b);
        return (op == ComparisonOp::EQ) ? equal : !equal;
    }

    bool compareUInt64(const std::string& a, const std::string& b, ComparisonOp op) const
    {
        uint64_t na = parseUInt64(a);
        uint64_t nb = parseUInt64(b);
        return applyComparison(na, nb, op);
    }

    bool compareVersions(const std::string& a, const std::string& b, ComparisonOp op) const
    {
        std::vector<int> va = parseVersion(a);
        std::vector<int> vb = parseVersion(b);
        
        // Normalize to same length for comparison
        size_t maxSize = std::max(va.size(), vb.size());
        va.resize(maxSize, 0);
        vb.resize(maxSize, 0);
        
        // Lexicographic comparison
        int cmp = 0;
        for (size_t i = 0; i < maxSize; ++i) {
            if (va[i] < vb[i]) {
                cmp = -1;
                break;
            } else if (va[i] > vb[i]) {
                cmp = 1;
                break;
            }
        }
        
        return applyComparison(cmp, 0, op);
    }

    bool compareBooleans(const std::string& a, const std::string& b, ComparisonOp op) const
    {
        if (op != ComparisonOp::EQ && op != ComparisonOp::NE) {
            throw std::invalid_argument("Booleans only support == and != operators");
        }
        
        bool ba = parseBool(a);
        bool bb = parseBool(b);
        return (op == ComparisonOp::EQ) ? (ba == bb) : (ba != bb);
    }

    // Generic comparison application
    template<typename T>
    bool applyComparison(T a, T b, ComparisonOp op) const
    {
        switch (op) {
            case ComparisonOp::EQ: return a == b;
            case ComparisonOp::NE: return a != b;
            case ComparisonOp::LT: return a < b;
            case ComparisonOp::LE: return a <= b;
            case ComparisonOp::GT: return a > b;
            case ComparisonOp::GE: return a >= b;
            default: return false;
        }
    }

    uint64_t parseUInt64(const std::string& s) const
    {
        if (s.empty()) {
            throw std::invalid_argument("Empty string cannot be parsed as number");
        }
        
        // Check for invalid characters before parsing
        if (!std::all_of(s.begin(), s.end(), ::isdigit)) {
            throw std::invalid_argument("Non-numeric characters in number: \"" + s + "\"");
        }
        
        size_t idx = 0;
        uint64_t value = std::stoull(s, &idx, 10);
        
        if (idx != s.length()) {
            throw std::invalid_argument("Invalid number format: \"" + s + "\"");
        }
        
        return value;
    }

    std::vector<int> parseVersion(const std::string& v) const
    {
        if (v.empty()) {
            return {0};
        }
        
        std::vector<int> result;
        result.reserve(4); // Most versions have <= 4 components
        
        std::stringstream ss(v);
        std::string token;
        
        while (std::getline(ss, token, '.')) {
            if (token.empty()) {
                result.push_back(0);
                continue;
            }
            
            try {
                // Validate token contains only digits
                if (!std::all_of(token.begin(), token.end(), ::isdigit)) {
                    throw std::invalid_argument("Non-numeric version segment");
                }
                result.push_back(std::stoi(token));
            } catch (const std::exception& ex) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; 
                         LOG_STRING("Invalid version segment '"); 
                         LOG_STRING(token); 
                         LOG_STRING("', using 0"));
                result.push_back(0);
            }
        }
        
        return result.empty() ? std::vector<int>{0} : result;
    }

    bool parseBool(const std::string& val) const
    {
        if (val.empty()) {
            throw std::invalid_argument("Empty string cannot be parsed as boolean");
        }
        
        // Case-insensitive comparison
        std::string v = val;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        
        if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
        if (v == "false" || v == "0" || v == "no" || v == "off") return false;
        if (v == "!true") return false;
        if (v == "!false") return true;
        
        throw std::invalid_argument("Invalid boolean format: \"" + val + "\"");
    }

    bool evaluateEmptyVectors(const std::string& rule) const
    {
        // Empty vectors are equal
        if (rule == "==" || rule == "EQ" || rule == "eq" || 
            rule == "<=" || rule == ">=") {
            return true;
        }
        
        if (rule == "!=" || rule == "NE" || rule == "ne" || 
            rule == "<" || rule == ">") {
            return false;
        }
        
        LOG_PRINT(LOG_ERROR, LOG_HDR; 
                 LOG_STRING("Unsupported rule on empty vectors: "); 
                 LOG_STRING(rule));
        return false;
    }
};

#endif // UVECTORVALIDATOR_HPP