#ifndef UVECTORVALIDATOR_HPP
#define UVECTORVALIDATOR_HPP

#include "uLogger.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iostream>
#include <limits>

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "VECTORVALID:"
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

class VectorValidator
{
public:

    bool validate(std::vector<std::string> v1,
                  std::vector<std::string> v2,
                  std::string rule,
                  eValidateType type) const
    {

        if (v1.empty() && v2.empty()) {
            return evaluateEmptyVectors(rule);
        }

        if (v1.size() != v2.size()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Vector sizes do not match"));
            return false;
        }

        for (size_t i = 0; i < v1.size(); ++i) {
            if (!compare(v1[i], v2[i], rule, type)) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Validation failed at index"); LOG_SIZET(i); LOG_STRING("with values:"); LOG_STRING(v1[i]); LOG_STRING(v2[i]));
                return false;
            }
        }
        return true;
    }

private:

    bool compare(const std::string& a, const std::string& b, const std::string& rule, eValidateType type) const
    {
        switch (type) {
            case eValidateType::STRING:
                return compareStrings(a, b, rule);
            case eValidateType::NUMBER:
                return compareUInt64(a, b, rule);
            case eValidateType::VERSION:
                return compareVersions(a, b, rule);
            case eValidateType::BOOLEAN:
                return compareBooleans(a, b, rule);
            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown validation type"));
                return false;
        }
    }

    bool compareStrings(const std::string& a, const std::string& b, const std::string& rule) const
    {
        if (rule == "EQ") return a == b;
        if (rule == "NE") return a != b;
        if (rule == "eq") return toLower(a) == toLower(b);
        if (rule == "ne") return toLower(a) != toLower(b);
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unsupported string rule:"); LOG_STRING(rule));
        return false;
    }

    bool compareUInt64(const std::string& a, const std::string& b, const std::string& rule) const
    {
        try {
            uint64_t na = parseUInt64(a);
            uint64_t nb = parseUInt64(b);

            if (rule == "==") return na == nb;
            if (rule == "!=") return na != nb;
            if (rule == "<")  return na <  nb;
            if (rule == "<=") return na <= nb;
            if (rule == ">")  return na >  nb;
            if (rule == ">=") return na >= nb;

            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unsupported numeric rule:"); LOG_STRING(rule));
        } catch (const std::exception& ex) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("failed:"); LOG_STRING(ex.what()));
        }

        return false;
    }

    uint64_t parseUInt64(const std::string& s) const
    {
        size_t idx = 0;
        uint64_t value = std::stoull(s, &idx, 10);
        if (idx != s.length()) throw std::invalid_argument("Non-numeric characters in number: \"" + s + "\"");
        return value;
    }

    bool compareVersions(const std::string& a, const std::string& b, const std::string& rule) const
    {
        std::vector<int> va = parseVersion(a);
        std::vector<int> vb = parseVersion(b);
        return compareVersionVectors(va, vb, rule);
    }

    bool compareVersionVectors(const std::vector<int>& va, const std::vector<int>& vb, const std::string& rule) const
    {
        size_t maxSize = (std::max)(va.size(), vb.size());

        for (size_t i = 0; i < maxSize; ++i) {
            int a = (i < va.size()) ? va[i] : 0;
            int b = (i < vb.size()) ? vb[i] : 0;

            if (a != b) {
                if (rule == "==") return false;
                if (rule == "!=") return true;
                if (rule == "<")  return a < b;
                if (rule == "<=") return a < b || (i + 1 == maxSize && a == b);
                if (rule == ">")  return a > b;
                if (rule == ">=") return a > b || (i + 1 == maxSize && a == b);
            }
        }

        return rule == "==" || rule == ">=" || rule == "<=";
    }

    bool compareBooleans(const std::string& a, const std::string& b, const std::string& rule) const
    {
        try {
            bool ba = parseBool(a);
            bool bb = parseBool(b);

            if (rule == "==") return ba == bb;
            if (rule == "!=") return ba != bb;

            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unsupported boolean rule:"); LOG_STRING(rule));
        } catch (const std::exception& ex) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("failed:"); LOG_STRING(ex.what()));
        }
        return false;
    }

    std::string toLower(const std::string& s) const
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), ::tolower);
        return out;
    }

    std::vector<int> parseVersion(const std::string& v) const
    {
        std::vector<int> result;
        std::stringstream ss(v);
        std::string token;
        while (std::getline(ss, token, '.')) {
            try {
                result.push_back(std::stoi(token));
            } catch (...) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("Invalid version segment:"); LOG_STRING(token));
                result.push_back(0);
            }
        }
        return result;
    }

    bool parseBool(const std::string& val) const
    {
        std::string v = toLower(val);
        if (v == "true") return true;
        if (v == "false") return false;
        if (v == "!true") return false;
        if (v == "!false") return true;
        throw std::invalid_argument("Invalid boolean format: \"" + val + "\"");
    }

    bool evaluateEmptyVectors(const std::string& rule) const
    {
        if (rule == "==" || rule == "EQ" || rule == "eq") return true;
        if (rule == "!=" || rule == "NE" || rule == "ne") return false;
        if (rule == "<=" || rule == ">=") return true;
        if (rule == "<"  || rule == ">")  return false;

        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unsupported rule on empty vectors:"); LOG_STRING(rule));
        return false;
    }
};


#endif // UVECTORVALIDATOR_HPP