#ifndef UVECTORMATH_HPP
#define UVECTORMATH_HPP

#include "uLogger.hpp"

#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
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

#define LT_HDR  "VECTOR_MATH |"
#define LOG_HDR LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                     CLASS IMPLEMENTATION                      //
///////////////////////////////////////////////////////////////////

class VectorMath {
    public:
        enum class IntOp {
            Add,
            Sub,
            Mul,
            Div,
            Mod, // Arithmetic
            BitAnd,
            BitOr,
            BitXor, // Bitwise
            ShiftLeft,
            ShiftRight, // Shifts
            Invalid
        };

        enum class DoubleOp {
            Add,
            Sub,
            Mul,
            Div,
            Invalid
        };

        VectorMath()
        {
            initializeOperatorMaps();
        }

        // Public interface for uint64_t math
        bool mathInteger(const std::vector<std::string> &vV1,
                         const std::vector<std::string> &vV2,
                         const std::string &strRule,
                         std::vector<std::string> &vResult,
                         bool bHexResult = false) const
        {
            vResult.clear();

            // Early validation
            if (vV1.empty() || vV2.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty input vectors"));
                return false;
            }

            if (vV1.size() != vV2.size()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Vector size mismatch: ");
                          LOG_SIZET(vV1.size()); LOG_STRING(" vs "); LOG_SIZET(vV2.size()));
                return false;
            }

            // Parse operation once
            IntOp op = parseIntOp(strRule);
            if (op == IntOp::Invalid) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Invalid integer operation: "); LOG_STRING(strRule));
                return false;
            }

            // Reserve space
            vResult.reserve(vV1.size());

            // Process each element
            for (size_t i = 0; i < vV1.size(); ++i) {
                try {
                    uint64_t a = parseUint64(vV1[i]);
                    uint64_t b = parseUint64(vV2[i]);
                    uint64_t r = computeUInt64(a, b, op);

                    vResult.push_back(formatUint64(r, bHexResult));
                } catch (const std::exception &ex) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("Integer error at index "); LOG_SIZET(i);
                              LOG_STRING(": "); LOG_STRING(ex.what());
                              LOG_STRING(" (values: '"); LOG_STRING(vV1[i]);
                              LOG_STRING("', '"); LOG_STRING(vV2[i]); LOG_STRING("')"));
                    return false;
                }
            }

            return true;
        }

        // Public interface for double math
        bool mathDouble(const std::vector<std::string> &vV1,
                        const std::vector<std::string> &vV2,
                        const std::string &strRule,
                        std::vector<std::string> &vResult,
                        int iPrecision = 15) const
        {
            vResult.clear();

            // Early validation
            if (vV1.empty() || vV2.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty input vectors"));
                return false;
            }

            if (vV1.size() != vV2.size()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Vector size mismatch: ");
                          LOG_SIZET(vV1.size()); LOG_STRING(" vs "); LOG_SIZET(vV2.size()));
                return false;
            }

            // Parse operation once
            DoubleOp op = parseDoubleOp(strRule);
            if (op == DoubleOp::Invalid) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Invalid double operation: "); LOG_STRING(strRule));
                return false;
            }

            // Reserve space
            vResult.reserve(vV1.size());

            // Process each element
            for (size_t i = 0; i < vV1.size(); ++i) {
                try {
                    double a = parseDouble(vV1[i]);
                    double b = parseDouble(vV2[i]);
                    double r = computeDouble(a, b, op);

                    // Check for invalid results
                    if (!std::isfinite(r)) {
                        throw std::domain_error("Result is not finite (inf or nan)");
                    }

                    vResult.push_back(formatDouble(r, iPrecision));
                } catch (const std::exception &ex) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("Double error at index "); LOG_SIZET(i);
                              LOG_STRING(": "); LOG_STRING(ex.what());
                              LOG_STRING(" (values: '"); LOG_STRING(vV1[i]);
                              LOG_STRING("', '"); LOG_STRING(vV2[i]); LOG_STRING("')"));
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
            int_ops_["+"] = int_ops_["+="] = IntOp::Add;
            int_ops_["-"] = int_ops_["-="] = IntOp::Sub;
            int_ops_["*"] = int_ops_["*="] = IntOp::Mul;
            int_ops_["/"] = int_ops_["/="] = IntOp::Div;
            int_ops_["%"] = int_ops_["%="] = IntOp::Mod;
            int_ops_["&"] = int_ops_["&="] = IntOp::BitAnd;
            int_ops_["|"] = int_ops_["|="] = IntOp::BitOr;
            int_ops_["^"] = int_ops_["^="] = IntOp::BitXor;
            int_ops_["<<"] = int_ops_["<<="] = IntOp::ShiftLeft;
            int_ops_[">>"] = int_ops_[">>="] = IntOp::ShiftRight;

            // Double operations
            double_ops_["+"] = double_ops_["+="] = DoubleOp::Add;
            double_ops_["-"] = double_ops_["-="] = DoubleOp::Sub;
            double_ops_["*"] = double_ops_["*="] = DoubleOp::Mul;
            double_ops_["/"] = double_ops_["/="] = DoubleOp::Div;
        }

        IntOp parseIntOp(const std::string &strRule) const
        {
            auto it = int_ops_.find(strRule);
            return (it != int_ops_.end()) ? it->second : IntOp::Invalid;
        }

        DoubleOp parseDoubleOp(const std::string &strRule) const
        {
            auto it = double_ops_.find(strRule);
            return (it != double_ops_.end()) ? it->second : DoubleOp::Invalid;
        }

        // Parsing utilities
        uint64_t parseUint64(const std::string &strS) const
        {
            if (strS.empty()) {
                throw std::invalid_argument("Empty string");
            }

            // Check for invalid characters
            if (strS[0] == '-') {
                throw std::invalid_argument("Negative number not allowed: " + strS);
            }

            size_t idx   = 0;
            uint64_t val = std::stoull(strS, &idx, 10);

            if (idx != strS.length()) {
                throw std::invalid_argument("Invalid uint64 string: " + strS);
            }

            return val;
        }

        double parseDouble(const std::string &strS) const
        {
            if (strS.empty()) {
                throw std::invalid_argument("Empty string");
            }

            size_t idx = 0;
            double val = std::stod(strS, &idx);

            if (idx != strS.length()) {
                throw std::invalid_argument("Invalid double string: " + strS);
            }

            if (!std::isfinite(val)) {
                throw std::invalid_argument("Non-finite value: " + strS);
            }

            return val;
        }

        // Formatting utilities
        std::string formatUint64(uint64_t u64Val, bool bHexResult) const
        {
            if (bHexResult) {
                std::ostringstream ss;
                ss << std::hex << std::uppercase << u64Val;
                return ss.str();
            }
            return std::to_string(u64Val);
        }

        std::string formatDouble(double dVal, int iPrecision) const
        {
            std::ostringstream oss;
            oss << std::setprecision(iPrecision) << dVal;
            return oss.str();
        }

        // Computation logic for uint64_t with overflow detection
        uint64_t computeUInt64(uint64_t u64A, uint64_t u64B, IntOp eOp) const
        {
            switch (eOp) {
            case IntOp::Add:
                if (u64A > std::numeric_limits<uint64_t>::max() - u64B) {
                    throw std::overflow_error("Addition overflow");
                }
                return u64A + u64B;

            case IntOp::Sub:
                if (u64A < u64B) {
                    throw std::underflow_error("Subtraction underflow (result would be negative)");
                }
                return u64A - u64B;

            case IntOp::Mul:
                if (u64B != 0 && u64A > std::numeric_limits<uint64_t>::max() / u64B) {
                    throw std::overflow_error("Multiplication overflow");
                }
                return u64A * u64B;

            case IntOp::Div:
                if (u64B == 0) {
                    throw std::domain_error("Division by zero");
                }
                return u64A / u64B;

            case IntOp::Mod:
                if (u64B == 0) {
                    throw std::domain_error("Modulo by zero");
                }
                return u64A % u64B;

            case IntOp::BitAnd:
                return u64A & u64B;

            case IntOp::BitOr:
                return u64A | u64B;

            case IntOp::BitXor:
                return u64A ^ u64B;

            case IntOp::ShiftLeft:
                if (u64B >= 64) {
                    throw std::domain_error("Shift amount >= 64 (undefined behavior)");
                }
                return u64A << u64B;

            case IntOp::ShiftRight:
                if (u64B >= 64) {
                    throw std::domain_error("Shift amount >= 64 (undefined behavior)");
                }
                return u64A >> u64B;

            default:
                throw std::logic_error("Invalid operation (should never reach here)");
            }
        }

        // Computation logic for double
        double computeDouble(double dA, double dB, DoubleOp eOp) const
        {
            switch (eOp) {
            case DoubleOp::Add:
                return dA + dB;

            case DoubleOp::Sub:
                return dA - dB;

            case DoubleOp::Mul:
                return dA * dB;

            case DoubleOp::Div:
                if (dB == 0.0) {
                    throw std::domain_error("Division by zero");
                }
                return dA / dB;

            default:
                throw std::logic_error("Invalid operation (should never reach here)");
            }
        }
};

#endif // UVECTORMATH_HPP

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
