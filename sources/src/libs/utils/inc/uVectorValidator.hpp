#ifndef UVECTORVALIDATOR_HPP
#define UVECTORVALIDATOR_HPP

#include "uLogger.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

#define LT_HDR  "VECTOR_VALID|"
#define LOG_HDR LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                     CLASS IMPLEMENTATION                      //
///////////////////////////////////////////////////////////////////

enum class eValidateType {
    STRING,
    NUMBER,
    VERSION,
    BOOLEAN
};

enum class ComparisonOp {
    EQ, // ==, EQ, eq  (case-sensitive)
    NE, // !=, NE, ne  (case-sensitive)
    LT, // <
    LE, // <=
    GT, // >
    GE, // >=
    UNKNOWN
};

class VectorValidator {
    public:
        VectorValidator() = default;

        // Main validation method - now takes const references
        bool validate(const std::vector<std::string> &vV1,
                      const std::vector<std::string> &vV2,
                      const std::string &strRule,
                      eValidateType eType) const
        {
            // Handle empty vectors
            if (vV1.empty() && vV2.empty()) {
                return evaluateEmptyVectors(strRule);
            }

            // Check size mismatch
            if (vV1.size() != vV2.size()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Vector sizes do not match: ");
                          LOG_SIZET(vV1.size()); LOG_STRING(" vs "); LOG_SIZET(vV2.size()));
                return false;
            }

            // Validate strRule upfront
            ComparisonOp op = parseRule(strRule, eType);
            if (op == ComparisonOp::UNKNOWN) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid strRule: "); LOG_STRING(strRule));
                return false;
            }

            // Compare each element
            for (size_t i = 0; i < vV1.size(); ++i) {
                if (!compare(vV1[i], vV2[i], op, eType)) {
                    LOG_PRINT(LOG_WERBOSE, LOG_HDR;
                              LOG_STRING("Validation failed at index "); LOG_SIZET(i);
                              LOG_STRING(": '"); LOG_STRING(vV1[i]);
                              LOG_STRING("' vs '"); LOG_STRING(vV2[i]); LOG_STRING("'"));
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
        static const std::unordered_map<std::string, ComparisonOp> &stringRules()
        {
            static const std::unordered_map<std::string, ComparisonOp> m = {
                {"EQ", ComparisonOp::EQ},
                {"NE", ComparisonOp::NE},
                {"eq", ComparisonOp::EQ},
                {"ne", ComparisonOp::NE},
                {"==", ComparisonOp::EQ},
                {"!=", ComparisonOp::NE},
            };
            return m;
        }

        static const std::unordered_map<std::string, ComparisonOp> &numericRules()
        {
            static const std::unordered_map<std::string, ComparisonOp> m = {
                {"==", ComparisonOp::EQ},
                {"!=", ComparisonOp::NE},
                {"<", ComparisonOp::LT},
                {"<=", ComparisonOp::LE},
                {">", ComparisonOp::GT},
                {">=", ComparisonOp::GE},
            };
            return m;
        }

        ComparisonOp parseRule(const std::string &strRule, eValidateType eType) const
        {
            std::string_view sv(strRule);
            while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
                sv.remove_prefix(1);
            }

            while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) {
                sv.remove_suffix(1);
            }

            // Build a temporary std::string only when the map lookup actually needs it.
            const std::string trimmed(sv);

            if (eType == eValidateType::STRING) {
                auto it = stringRules().find(trimmed);
                return (it != stringRules().end()) ? it->second : ComparisonOp::UNKNOWN;
            } else {
                auto it = numericRules().find(trimmed);
                return (it != numericRules().end()) ? it->second : ComparisonOp::UNKNOWN;
            }
        }

        bool compare(const std::string &strA, const std::string &strB,
                     ComparisonOp eOp, eValidateType eType) const
        {
            try {
                switch (eType) {
                case eValidateType::STRING:
                    return compareStrings(strA, strB, eOp);
                case eValidateType::NUMBER:
                    return compareDouble(strA, strB, eOp);
                case eValidateType::VERSION:
                    return compareVersions(strA, strB, eOp);
                case eValidateType::BOOLEAN:
                    return compareBooleans(strA, strB, eOp);
                default:
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown validation eType"));
                    return false;
                }
            } catch (const std::exception &ex) {
                LOG_PRINT(LOG_WERBOSE, LOG_HDR;
                          LOG_STRING("Comparison failed: "); LOG_STRING(ex.what());
                          LOG_STRING(" (values: '"); LOG_STRING(strA);
                          LOG_STRING("', '"); LOG_STRING(strB); LOG_STRING("')"));
                return false;
            }
        }

        bool compareStrings(const std::string &strA, const std::string &strB, ComparisonOp eOp) const
        {
            // All string comparisons are case-sensitive: "Hello" != "hello".
            // EQ/eq/== are exact-match synonyms; NE/ne/!= are exact-mismatch synonyms.
            switch (eOp) {
            case ComparisonOp::EQ:
                return (strA == strB);
            case ComparisonOp::NE:
                return (strA != strB);
            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("compareStrings: unexpected eOp for string type"));
                return false;
            }
        }

        bool compareDouble(const std::string &strA, const std::string &strB, ComparisonOp eOp) const
        {
            double na = parseDouble(strA);
            double nb = parseDouble(strB);
            return applyComparison(na, nb, eOp);
        }

        double parseDouble(const std::string &strS) const
        {
            if (strS.empty()) {
                throw std::invalid_argument("Empty string cannot be parsed as number");
            }

            size_t idx   = 0;
            double value = 0.0;
            try {
                value = std::stod(strS, &idx);
            } catch (const std::exception &) {
                throw std::invalid_argument("Invalid number format: \"" + strS + "\"");
            }

            // Skip trailing whitespace (stod stops cleanly, but be safe)
            while (idx < strS.size() && std::isspace(static_cast<unsigned char>(strS[idx]))) {
                ++idx;
            }

            if (idx != strS.size()) {
                throw std::invalid_argument("Non-numeric characters in number: \"" + strS + "\"");
            }

            return value;
        }

        bool compareVersions(const std::string &strA, const std::string &strB, ComparisonOp eOp) const
        {
            std::vector<int> va = parseVersion(strA);
            std::vector<int> vb = parseVersion(strB);

            // Normalize to same length for comparison
            size_t maxSize      = std::max(va.size(), vb.size());
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

            return applyComparison(cmp, 0, eOp);
        }

        bool compareBooleans(const std::string &strA, const std::string &strB, ComparisonOp eOp) const
        {
            if (eOp != ComparisonOp::EQ && eOp != ComparisonOp::NE) {
                throw std::invalid_argument("Booleans only support == and != operators");
            }

            bool ba = parseBool(strA);
            bool bb = parseBool(strB);
            return (eOp == ComparisonOp::EQ) ? (ba == bb) : (ba != bb);
        }

        // Generic comparison application
        template <typename T>
        bool applyComparison(T a, T b, ComparisonOp eOp) const
        {
            switch (eOp) {
            case ComparisonOp::EQ:
                return a == b;
            case ComparisonOp::NE:
                return a != b;
            case ComparisonOp::LT:
                return a < b;
            case ComparisonOp::LE:
                return a <= b;
            case ComparisonOp::GT:
                return a > b;
            case ComparisonOp::GE:
                return a >= b;
            default:
                return false;
            }
        }

        std::vector<int> parseVersion(const std::string &strV) const
        {
            if (strV.empty()) {
                return {0};
            }

            std::vector<int> result;
            result.reserve(4);

            const char *p   = strV.data();
            const char *end = p + strV.size();

            while (p <= end) {
                const char *dot = p;
                while (dot < end && *dot != '.') {
                    ++dot;
                }

                // [p, dot) is one component
                if (dot == p) {
                    result.push_back(0); // empty segment
                } else {
                    std::string seg(p, dot);
                    try {
                        if (!std::all_of(seg.begin(), seg.end(), ::isdigit)) {
                            throw std::invalid_argument("non-numeric");
                        }
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

        static bool iequal(const std::string &strS, const char *pstrLiteral, size_t len)
        {
            if (strS.size() != len) {
                return false;
            }
            for (size_t i = 0; i < len; ++i) {
                if (std::tolower(static_cast<unsigned char>(strS[i])) != pstrLiteral[i]) {
                    return false;
                }
            }
            return true;
        }

        bool parseBool(const std::string &strVal) const
        {
            if (strVal.empty()) {
                throw std::invalid_argument("Empty string cannot be parsed as boolean");
            }

            if (iequal(strVal, "true", 4) || iequal(strVal, "1", 1) ||
                iequal(strVal, "yes", 3) || iequal(strVal, "on", 2)) {
                return true;
            }
            if (iequal(strVal, "false", 5) || iequal(strVal, "0", 1) ||
                iequal(strVal, "no", 2) || iequal(strVal, "off", 3)) {
                return false;
            }
            if (iequal(strVal, "!true", 5)) {
                return false;
            }
            if (iequal(strVal, "!false", 6)) {
                return true;
            }

            throw std::invalid_argument("Invalid boolean format: \"" + strVal + "\"");
        }

        bool evaluateEmptyVectors(const std::string &strRule) const
        {
            // Empty vectors are equal
            if (strRule == "==" || strRule == "EQ" || strRule == "eq" ||
                strRule == "<=" || strRule == ">=") {
                return true;
            }

            if (strRule == "!=" || strRule == "NE" || strRule == "ne" ||
                strRule == "<" || strRule == ">") {
                return false;
            }

            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Unsupported strRule on empty vectors: ");
                      LOG_STRING(strRule));
            return false;
        }
};

#endif // UVECTORVALIDATOR_HPP