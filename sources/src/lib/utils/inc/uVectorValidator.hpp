#ifndef UVECTORVALIDATOR_HPP
#define UVECTORVALIDATOR_HPP

#include "uLogger.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "VECTOR_VALID|"
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
    EQ,      // ==, EQ, eq  (case-sensitive)
    NE,      // !=, NE, ne  (case-sensitive)
    LT,      // <
    LE,      // <=
    GT,      // >
    GE,      // >=
    UNKNOWN
};

class VectorValidator
{
public:
    VectorValidator() = default;

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

    // ── O1: Rule maps are process-wide singletons ─────────────────────────
    // Built exactly once on first use via static local; never mutated after
    // that, so there is no thread-safety concern beyond the one-time init
    // which is guaranteed by the C++11 "magic statics" rule.
    static const std::unordered_map<std::string, ComparisonOp>& stringRules()
    {
        static const std::unordered_map<std::string, ComparisonOp> m = {
            {"EQ", ComparisonOp::EQ}, {"NE", ComparisonOp::NE},
            {"eq", ComparisonOp::EQ}, {"ne", ComparisonOp::NE},
            {"==", ComparisonOp::EQ}, {"!=", ComparisonOp::NE},
        };
        return m;
    }

    static const std::unordered_map<std::string, ComparisonOp>& numericRules()
    {
        static const std::unordered_map<std::string, ComparisonOp> m = {
            {"==", ComparisonOp::EQ}, {"!=", ComparisonOp::NE},
            {"<",  ComparisonOp::LT}, {"<=", ComparisonOp::LE},
            {">",  ComparisonOp::GT}, {">=", ComparisonOp::GE},
        };
        return m;
    }

    ComparisonOp parseRule(const std::string& rule, eValidateType type) const
    {
        std::string_view sv(rule);
        while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
            sv.remove_prefix(1);
        }

        while (!sv.empty() && (sv.back()  == ' ' || sv.back()  == '\t')) {
            sv.remove_suffix(1);
        }

        // Build a temporary std::string only when the map lookup actually needs it.
        const std::string trimmed(sv);

        if (type == eValidateType::STRING) {
            auto it = stringRules().find(trimmed);
            return (it != stringRules().end()) ? it->second : ComparisonOp::UNKNOWN;
        } else {
            auto it = numericRules().find(trimmed);
            return (it != numericRules().end()) ? it->second : ComparisonOp::UNKNOWN;
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
                    return compareDouble(a, b, op);
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
        // All string comparisons are case-sensitive: "Hello" != "hello".
        // EQ/eq/== are exact-match synonyms; NE/ne/!= are exact-mismatch synonyms.
        switch (op) {
            case ComparisonOp::EQ: return  (a == b);
            case ComparisonOp::NE: return  (a != b);
            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("compareStrings: unexpected op for string type"));
                return false;
        }
    }

    bool compareDouble(const std::string& a, const std::string& b, ComparisonOp op) const
    {
        double na = parseDouble(a);
        double nb = parseDouble(b);
        return applyComparison(na, nb, op);
    }

    double parseDouble(const std::string& s) const
    {
        if (s.empty()) {
            throw std::invalid_argument("Empty string cannot be parsed as number");
        }

        size_t idx = 0;
        double value = 0.0;
        try {
            value = std::stod(s, &idx);
        } catch (const std::exception&) {
            throw std::invalid_argument("Invalid number format: \"" + s + "\"");
        }

        // Skip trailing whitespace (stod stops cleanly, but be safe)
        while (idx < s.size() && std::isspace(static_cast<unsigned char>(s[idx]))) ++idx;

        if (idx != s.size()) {
            throw std::invalid_argument("Non-numeric characters in number: \"" + s + "\"");
        }

        return value;
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
            case ComparisonOp::LT: return a <  b;
            case ComparisonOp::LE: return a <= b;
            case ComparisonOp::GT: return a >  b;
            case ComparisonOp::GE: return a >= b;
            default:               return false;
        }
    }

    std::vector<int> parseVersion(const std::string& v) const
    {
        if (v.empty()) {
            return {0};
        }

        std::vector<int> result;
        result.reserve(4);

        const char* p   = v.data();
        const char* end = p + v.size();

        while (p <= end) {
            const char* dot = p;
            while (dot < end && *dot != '.') ++dot;

            // [p, dot) is one component
            if (dot == p) {
                result.push_back(0); // empty segment
            } else {
                std::string seg(p, dot);
                try {
                    if (!std::all_of(seg.begin(), seg.end(), ::isdigit))
                        throw std::invalid_argument("non-numeric");
                    result.push_back(std::stoi(seg));
                } catch (...) {
                    LOG_PRINT(LOG_WARNING, LOG_HDR;
                             LOG_STRING("Invalid version segment '");
                             LOG_STRING(seg);
                             LOG_STRING("', using 0"));
                    result.push_back(0);
                }
            }
            p = dot + 1;
        }

        return result.empty() ? std::vector<int>{0} : result;
    }

    static bool iequal(const std::string& s, const char* literal, size_t len)
    {
        if (s.size() != len) return false;
        for (size_t i = 0; i < len; ++i)
            if (std::tolower(static_cast<unsigned char>(s[i])) != literal[i]) return false;
        return true;
    }

    bool parseBool(const std::string& val) const
    {
        if (val.empty())
            throw std::invalid_argument("Empty string cannot be parsed as boolean");

        if (iequal(val,"true",4)  || iequal(val,"1",1) ||
            iequal(val,"yes",3)   || iequal(val,"on",2))  return true;
        if (iequal(val,"false",5) || iequal(val,"0",1) ||
            iequal(val,"no",2)    || iequal(val,"off",3)) return false;
        if (iequal(val,"!true",5))  return false;
        if (iequal(val,"!false",6)) return true;

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