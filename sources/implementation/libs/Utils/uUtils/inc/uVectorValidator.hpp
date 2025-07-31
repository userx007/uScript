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

enum class eValidateType {
    STRING,
    NUMBER,
    VERSION,
    BOOLEAN
};

class VectorValidator {
public:
    bool validate(std::vector<std::string>& v1, std::vector<std::string>& v2, std::string& rule, eValidateType type) {
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
    bool compare(const std::string& a, const std::string& b, const std::string& rule, eValidateType type) {
        switch (type) {
            case eValidateType::STRING:
                return compareStrings(a, b, rule);
            case eValidateType::NUMBER:
                return compareUInt64(a, b, rule);
            case eValidateType::VERSION:
                return compareVersions(a, b);
            case eValidateType::BOOLEAN:
                return compareBooleans(a, b);
            default:
                std::cerr << "Error: Unknown validation type.\n";
                return false;
        }
    }

    bool compareStrings(const std::string& a, const std::string& b, const std::string& rule) {
        if (rule == "EQ") return a == b;
        if (rule == "NE") return a != b;
        if (rule == "eq") return toLower(a) == toLower(b);
        if (rule == "ne") return toLower(a) != toLower(b);
        std::cerr << "Error: Unsupported string rule \"" << rule << "\".\n";
        return false;
    }

    bool compareUInt64(const std::string& a, const std::string& b, const std::string& rule) {
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

    uint64_t parseUInt64(const std::string& s) {
        size_t idx = 0;
        uint64_t value = std::stoull(s, &idx, 10);
        if (idx != s.length()) throw std::invalid_argument("Non-numeric characters in number: \"" + s + "\"");
        return value;
    }

    bool compareVersions(const std::string& a, const std::string& b) {
        std::vector<int> va = parseVersion(a);
        std::vector<int> vb = parseVersion(b);
        return va == vb;
    }

    bool compareBooleans(const std::string& a, const std::string& b) {
        try {
            bool ba = parseBool(a);
            bool bb = parseBool(b);
            return ba == bb;
        } catch (const std::exception& ex) {
            std::cerr << "Error: " << ex.what() << "\n";
            return false;
        }
    }

    std::string toLower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), ::tolower);
        return out;
    }

    std::vector<int> parseVersion(const std::string& v) {
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

    bool parseBool(const std::string& val) {
        std::string v = toLower(val);
        if (v == "TRUE") return true;
        if (v == "FALSE") return false;
        if (v == "!TRUE") return false;
        if (v == "!FALSE") return true;
        throw std::invalid_argument("Invalid boolean format: \"" + val + "\"");
    }
};

#endif // UVECTORVALIDATOR_HPP