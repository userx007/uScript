#ifndef UVECTORVALIDATOR_HPP
#define UVECTORVALIDATOR_HPP

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iostream>
#include <limits>

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

    bool validate (const std::vector<std::string>& v1,
                   const std::vector<std::string>& v2,
                   const std::string& rule,
                   eValidateType type) const
    {
        if (v1.size() != v2.size()) {
            std::cerr << "Error: Vector sizes do not match.\n";
            return false;
        }

        for (size_t i = 0; i < v1.size(); ++i) {
            if (!compare(v1[i], v2[i], rule, type)) {
                std::cerr << "Error: Validation failed at index " << i << " with values \""
                          << v1[i] << "\" and \"" << v2[i] << "\".\n";
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
                std::cerr << "Error: Unknown validation type.\n";
                return false;
        }
    }

    bool compareStrings(const std::string& a, const std::string& b, const std::string& rule) const
    {
        if (rule == "EQ") return a == b;
        if (rule == "NE") return a != b;
        if (rule == "eq") return toLower(a) == toLower(b);
        if (rule == "ne") return toLower(a) != toLower(b);
        std::cerr << "Error: Unsupported string rule \"" << rule << "\".\n";
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

            std::cerr << "Error: Unsupported numeric rule \"" << rule << "\".\n";
        } catch (const std::exception& ex) {
            std::cerr << "Error: " << ex.what() << "\n";
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

        // All parts are equal
        return rule == "==" || rule == ">=" || rule == "<=";
    }

    bool compareBooleans(const std::string& a, const std::string& b, const std::string& rule) const
    {
        try {
            bool ba = parseBool(a);
            bool bb = parseBool(b);
            if (rule == "==") return ba == bb;
            if (rule == "!=") return ba != bb;
            std::cerr << "Error: Unsupported boolean rule \"" << rule << "\".\n";
            return false;
        } catch (const std::exception& ex) {
            std::cerr << "Error: " << ex.what() << "\n";
            return false;
        }
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
                std::cerr << "Error: Invalid version segment \"" << token << "\".\n";
                result.push_back(0);
            }
        }
        return result;
    }

    bool parseBool(const std::string& val) const
    {
        std::string v = toLower(val);
        if (v == "TRUE") return true;
        if (v == "FALSE") return false;
        if (v == "!TRUE") return false;
        if (v == "!FALSE") return true;
        throw std::invalid_argument("Invalid boolean format: \"" + val + "\"");
    }
};

#endif // UVECTORVALIDATOR_HPP