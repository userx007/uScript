#ifndef UVECTORMATH_HPP
#define UVECTORMATH_HPP

#include "uLogger.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#include <functional>
#include <limits>
#include <cmath>

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "VECTOR_MATH:"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                     CLASS IMPLEMENTATION                      //
///////////////////////////////////////////////////////////////////

class VectorMath
{
public:
    enum class IntOp {
        Add, Sub, Mul, Div, Mod,           // Arithmetic
        BitAnd, BitOr, BitXor,             // Bitwise
        ShiftLeft, ShiftRight,             // Shifts
        Invalid
    };

    enum class DoubleOp {
        Add, Sub, Mul, Div,
        Invalid
    };

    VectorMath()
    {
        initializeOperatorMaps();
    }

    // Public interface for uint64_t math
    bool mathInteger(const std::vector<std::string>& v1,
                     const std::vector<std::string>& v2,
                     const std::string& rule,
                     std::vector<std::string>& result,
                     bool bHexResult = false) const
    {
        result.clear();

        // Early validation
        if (v1.empty() || v2.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty input vectors"));
            return false;
        }

        if (v1.size() != v2.size()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                     LOG_STRING("Vector size mismatch: ");
                     LOG_SIZET(v1.size()); LOG_STRING(" vs "); LOG_SIZET(v2.size()));
            return false;
        }

        // Parse operation once
        IntOp op = parseIntOp(rule);
        if (op == IntOp::Invalid) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                     LOG_STRING("Invalid integer operation: "); LOG_STRING(rule));
            return false;
        }

        // Reserve space
        result.reserve(v1.size());

        // Process each element
        for (size_t i = 0; i < v1.size(); ++i) {
            try {
                uint64_t a = parseUint64(v1[i]);
                uint64_t b = parseUint64(v2[i]);
                uint64_t r = computeUInt64(a, b, op);

                result.push_back(formatUint64(r, bHexResult));
            } catch (const std::exception& ex) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                         LOG_STRING("Integer error at index "); LOG_SIZET(i);
                         LOG_STRING(": "); LOG_STRING(ex.what());
                         LOG_STRING(" (values: '"); LOG_STRING(v1[i]);
                         LOG_STRING("', '"); LOG_STRING(v2[i]); LOG_STRING("')"));
                return false;
            }
        }

        return true;
    }

    // Public interface for double math
    bool mathDouble(const std::vector<std::string>& v1,
                    const std::vector<std::string>& v2,
                    const std::string& rule,
                    std::vector<std::string>& result,
                    int precision = 15) const
    {
        result.clear();

        // Early validation
        if (v1.empty() || v2.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty input vectors"));
            return false;
        }

        if (v1.size() != v2.size()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                     LOG_STRING("Vector size mismatch: ");
                     LOG_SIZET(v1.size()); LOG_STRING(" vs "); LOG_SIZET(v2.size()));
            return false;
        }

        // Parse operation once
        DoubleOp op = parseDoubleOp(rule);
        if (op == DoubleOp::Invalid) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                     LOG_STRING("Invalid double operation: "); LOG_STRING(rule));
            return false;
        }

        // Reserve space
        result.reserve(v1.size());

        // Process each element
        for (size_t i = 0; i < v1.size(); ++i) {
            try {
                double a = parseDouble(v1[i]);
                double b = parseDouble(v2[i]);
                double r = computeDouble(a, b, op);

                // Check for invalid results
                if (!std::isfinite(r)) {
                    throw std::domain_error("Result is not finite (inf or nan)");
                }

                result.push_back(formatDouble(r, precision));
            } catch (const std::exception& ex) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                         LOG_STRING("Double error at index "); LOG_SIZET(i);
                         LOG_STRING(": "); LOG_STRING(ex.what());
                         LOG_STRING(" (values: '"); LOG_STRING(v1[i]);
                         LOG_STRING("', '"); LOG_STRING(v2[i]); LOG_STRING("')"));
                return false;
            }
        }

        return true;
    }

private:
    // Operator lookup tables
    std::unordered_map<std::string, IntOp> int_ops_;
    std::unordered_map<std::string, DoubleOp> double_ops_;

    void initializeOperatorMaps()
    {
        // Integer operations (both forms map to same operation)
        int_ops_["+"]   = int_ops_["+="]  = IntOp::Add;
        int_ops_["-"]   = int_ops_["-="]  = IntOp::Sub;
        int_ops_["*"]   = int_ops_["*="]  = IntOp::Mul;
        int_ops_["/"]   = int_ops_["/="]  = IntOp::Div;
        int_ops_["%"]   = int_ops_["%="]  = IntOp::Mod;
        int_ops_["&"]   = int_ops_["&="]  = IntOp::BitAnd;
        int_ops_["|"]   = int_ops_["|="]  = IntOp::BitOr;
        int_ops_["^"]   = int_ops_["^="]  = IntOp::BitXor;
        int_ops_["<<"]  = int_ops_["<<="] = IntOp::ShiftLeft;
        int_ops_[">>"]  = int_ops_[">>="] = IntOp::ShiftRight;

        // Double operations
        double_ops_["+"]  = double_ops_["+="] = DoubleOp::Add;
        double_ops_["-"]  = double_ops_["-="] = DoubleOp::Sub;
        double_ops_["*"]  = double_ops_["*="] = DoubleOp::Mul;
        double_ops_["/"]  = double_ops_["/="] = DoubleOp::Div;
    }

    IntOp parseIntOp(const std::string& rule) const
    {
        auto it = int_ops_.find(rule);
        return (it != int_ops_.end()) ? it->second : IntOp::Invalid;
    }

    DoubleOp parseDoubleOp(const std::string& rule) const
    {
        auto it = double_ops_.find(rule);
        return (it != double_ops_.end()) ? it->second : DoubleOp::Invalid;
    }

    // Parsing utilities
    uint64_t parseUint64(const std::string& s) const
    {
        if (s.empty()) {
            throw std::invalid_argument("Empty string");
        }

        // Check for invalid characters
        if (s[0] == '-') {
            throw std::invalid_argument("Negative number not allowed: " + s);
        }

        size_t idx = 0;
        uint64_t val = std::stoull(s, &idx, 10);
        
        if (idx != s.length()) {
            throw std::invalid_argument("Invalid uint64 string: " + s);
        }
        
        return val;
    }

    double parseDouble(const std::string& s) const
    {
        if (s.empty()) {
            throw std::invalid_argument("Empty string");
        }

        size_t idx = 0;
        double val = std::stod(s, &idx);
        
        if (idx != s.length()) {
            throw std::invalid_argument("Invalid double string: " + s);
        }

        if (!std::isfinite(val)) {
            throw std::invalid_argument("Non-finite value: " + s);
        }
        
        return val;
    }

    // Formatting utilities
    std::string formatUint64(uint64_t val, bool bHexResult) const
    {
        if (bHexResult) {
            std::ostringstream ss;
            ss << std::hex << std::uppercase << val;
            return ss.str();
        }
        return std::to_string(val);
    }

    std::string formatDouble(double val, int precision) const
    {
        std::ostringstream oss;
        oss << std::setprecision(precision) << val;
        return oss.str();
    }

    // Computation logic for uint64_t with overflow detection
    uint64_t computeUInt64(uint64_t a, uint64_t b, IntOp op) const
    {
        switch (op) {
            case IntOp::Add:
                if (a > std::numeric_limits<uint64_t>::max() - b) {
                    throw std::overflow_error("Addition overflow");
                }
                return a + b;

            case IntOp::Sub:
                if (a < b) {
                    throw std::underflow_error("Subtraction underflow (result would be negative)");
                }
                return a - b;

            case IntOp::Mul:
                if (b != 0 && a > std::numeric_limits<uint64_t>::max() / b) {
                    throw std::overflow_error("Multiplication overflow");
                }
                return a * b;

            case IntOp::Div:
                if (b == 0) {
                    throw std::domain_error("Division by zero");
                }
                return a / b;

            case IntOp::Mod:
                if (b == 0) {
                    throw std::domain_error("Modulo by zero");
                }
                return a % b;

            case IntOp::BitAnd:
                return a & b;

            case IntOp::BitOr:
                return a | b;

            case IntOp::BitXor:
                return a ^ b;

            case IntOp::ShiftLeft:
                if (b >= 64) {
                    throw std::domain_error("Shift amount >= 64 (undefined behavior)");
                }
                return a << b;

            case IntOp::ShiftRight:
                if (b >= 64) {
                    throw std::domain_error("Shift amount >= 64 (undefined behavior)");
                }
                return a >> b;

            default:
                throw std::logic_error("Invalid operation (should never reach here)");
        }
    }

    // Computation logic for double
    double computeDouble(double a, double b, DoubleOp op) const
    {
        switch (op) {
            case DoubleOp::Add:
                return a + b;

            case DoubleOp::Sub:
                return a - b;

            case DoubleOp::Mul:
                return a * b;

            case DoubleOp::Div:
                if (b == 0.0) {
                    throw std::domain_error("Division by zero");
                }
                return a / b;

            default:
                throw std::logic_error("Invalid operation (should never reach here)");
        }
    }
};

#endif //UVECTORMATH_HPP



///////////////////////////////////////////////////////////////////////
// USAGE:
///////////////////////////////////////////////////////////////////////

/*
VectorMath math;
std::vector<std::string> result;

// Integer addition with overflow detection
std::vector<std::string> v1 = {"100", "200", "18446744073709551615"}; // max uint64
std::vector<std::string> v2 = {"50", "300", "1"};
if (math.mathInteger(v1, v2, "+", result)) {
    // result[2] will fail with overflow error
}

// Hexadecimal output
if (math.mathInteger(v1, v2, "&", result, true)) {
    // result contains hex strings
}

// Double operations with custom precision
std::vector<std::string> d1 = {"3.14159", "2.71828"};
std::vector<std::string> d2 = {"1.41421", "1.61803"};
if (math.mathDouble(d1, d2, "*", result, 10)) {
    // Result with 10 decimal places
}

// Shift operations (now safe)
std::vector<std::string> s1 = {"1", "255"};
std::vector<std::string> s2 = {"8", "64"}; // This will error (shift >= 64)
math.mathInteger(s1, s2, "<<", result); // Returns false, logs error
*/
