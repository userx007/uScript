#ifndef UVECTORMATH_HPP
#define UVECTORMATH_HPP

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <iostream>

class VectorMath
{

public:

    // Public interface for uint64_t math
    bool mathInteger (const std::vector<std::string>& v1,
                      const std::vector<std::string>& v2,
                      const std::string& rule,
                      std::vector<std::string>& result,
                      bool bHexResult) const
    {
        result.clear();
        if (v1.size() != v2.size()) return false;

        for (size_t i = 0; i < v1.size(); ++i) {
            try {
                uint64_t a = parseUint64(v1[i]);
                uint64_t b = parseUint64(v2[i]);
                uint64_t r = computeUInt64(a, b, rule);

                if (bHexResult) {
                    std::stringstream ss;
                    ss << std::hex << std::uppercase << r; // std::uppercase is optional
                    result.push_back(ss.str());
                } else {
                    result.push_back(std::to_string(r));
                }
            } catch (const std::exception& ex) {
                std::cerr << "UInt error at index " << i << ": " << ex.what() << "\n";
                return false;
            }
        }

        return true;
    }

    // Public interface for double math
    bool mathDouble(const std::vector<std::string>& v1, const std::vector<std::string>& v2,
                    const std::string& rule, std::vector<std::string>& result) {
        result.clear();
        if (v1.size() != v2.size()) return false;

        for (size_t i = 0; i < v1.size(); ++i) {
            try {
                double a = parseDouble(v1[i]);
                double b = parseDouble(v2[i]);
                double r = computeDouble(a, b, rule);
                result.push_back(toString(r));
            } catch (const std::exception& ex) {
                std::cerr << "Double error at index " << i << ": " << ex.what() << "\n";
                return false;
            }
        }

        return true;
    }

private:

    // Parsing utilities
    uint64_t parseUint64(const std::string& s) const {
        size_t idx;
        uint64_t val = std::stoull(s, &idx, 10);
        if (idx != s.length()) throw std::invalid_argument("Invalid uint64 string: " + s);
        return val;
    }

    double parseDouble(const std::string& s) const {
        size_t idx;
        double val = std::stod(s, &idx);
        if (idx != s.length()) throw std::invalid_argument("Invalid double string: " + s);
        return val;
    }

    std::string toString(double val) const {
        std::ostringstream oss;
        oss.precision(15);
        oss << std::fixed << val;
        return oss.str();
    }

    // Computation logic for uint64_t
    uint64_t computeUInt64(uint64_t a, uint64_t b, const std::string& rule) const {
        if (rule == "+") return a + b;
        if (rule == "-") return a - b;
        if (rule == "*") return a * b;
        if (rule == "/") if (b != 0) return a / b; else throw std::domain_error("Divide by zero");
        if (rule == "%") if (b != 0) return a % b; else throw std::domain_error("Modulo by zero");
        if (rule == "&") return a & b;
        if (rule == "|") return a | b;
        if (rule == "^") return a ^ b;
        if (rule == "<<") return a << b;
        if (rule == ">>") return a >> b;
        if (rule == "+=") return a + b;
        if (rule == "-=") return a - b;
        if (rule == "*=") return a * b;
        if (rule == "/=") if (b != 0) return a / b; else throw std::domain_error("Divide by zero");
        if (rule == "%=") if (b != 0) return a % b; else throw std::domain_error("Modulo by zero");
        if (rule == "&=") return a & b;
        if (rule == "|=") return a | b;
        if (rule == "^=") return a ^ b;
        if (rule == "<<=") return a << b;
        if (rule == ">>=") return a >> b;

        throw std::invalid_argument("Invalid uint64 operation: " + rule);
    }

    // Computation logic for double
    double computeDouble(double a, double b, const std::string& rule) const {
        if (rule == "+") return a + b;
        if (rule == "-") return a - b;
        if (rule == "*") return a * b;
        if (rule == "/") if (b != 0.0) return a / b; else throw std::domain_error("Divide by zero");
        if (rule == "+=") return a + b;
        if (rule == "-=") return a - b;
        if (rule == "*=") return a * b;
        if (rule == "/=") if (b != 0.0) return a / b; else throw std::domain_error("Divide by zero");

        throw std::invalid_argument("Invalid double operation: " + rule);
    }
};

#endif //UVECTORMATH_HPP
