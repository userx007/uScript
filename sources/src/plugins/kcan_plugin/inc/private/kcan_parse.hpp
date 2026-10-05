#ifndef KCAN_PARSE_HPP
#define KCAN_PARSE_HPP

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>

/**
 * \brief Value parsers for the KCAN link-configuration keys (bitrate, sample point, on/off flags).
 *
 * Header-only and free of any framework dependency so they can be unit-tested standalone.
 */
namespace kcan_parse {

    /**
     * \brief Parse a CAN bitrate in bit/s.
     *
     * Accepts a plain integer ("500000"), or the usual shorthand with a k/K (x1000)
     * or m/M (x1000000) suffix: "500k", "1M", "83.333k", "0.5M". A trailing "bps" /
     * "bit" / "baud" is ignored. Range: 1 000 .. 16 000 000 bit/s.
     */
    inline bool bitrate(const std::string &strIn, uint32_t &u32Out)
    {
        std::string s = strIn;
        s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }), s.end());
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        for (const char *suffix : {"bps", "baud", "bit"}) {
            const std::string suf(suffix);
            if (s.size() > suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0) {
                s.erase(s.size() - suf.size());
                break;
            }
        }
        if (s.empty()) {
            return false;
        }

        double dMul = 1.0;
        if (s.back() == 'k') {
            dMul = 1e3;
            s.pop_back();
        } else if (s.back() == 'm') {
            dMul = 1e6;
            s.pop_back();
        }
        if (s.empty()) {
            return false;
        }

        // hex ("0x7A120") only makes sense without a suffix; strtod would also take it, but be explicit
        char *pEnd         = nullptr;
        errno              = 0;
        const double dVal  = std::strtod(s.c_str(), &pEnd);
        if (errno != 0 || pEnd == s.c_str() || *pEnd != '\0') {
            return false;
        }

        const double dBps = dVal * dMul + 0.5;
        if (dBps < 1000.0 || dBps > 16000000.0) {
            return false;
        }
        u32Out = static_cast<uint32_t>(dBps);
        return true;
    }

    /**
     * \brief Parse a sample point and return it in permille (the kernel's unit; 875 == 87.5 %).
     *
     * Accepted notations (the same value, three ways):
     *   0.875   fraction, as used by `ip link ... sample-point 0.875`
     *   87.5    percent
     *   875     permille
     * Plain "0" means "let the kernel choose" and yields 0. The valid range is 50.0 % .. 99.9 %.
     */
    inline bool samplePoint(const std::string &strIn, uint32_t &u32PermilleOut)
    {
        std::string s = strIn;
        s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }), s.end());
        if (!s.empty() && s.back() == '%') {
            s.pop_back();
            // an explicit '%' always means percent, even for values <= 1
            char *pEnd        = nullptr;
            errno             = 0;
            const double dPct = std::strtod(s.c_str(), &pEnd);
            if (s.empty() || errno != 0 || *pEnd != '\0' || dPct < 50.0 || dPct > 99.9) {
                return false;
            }
            u32PermilleOut = static_cast<uint32_t>(dPct * 10.0 + 0.5);
            return true;
        }
        if (s.empty()) {
            return false;
        }

        char *pEnd      = nullptr;
        errno           = 0;
        const double d  = std::strtod(s.c_str(), &pEnd);
        if (errno != 0 || pEnd == s.c_str() || *pEnd != '\0' || d < 0.0) {
            return false;
        }

        double dPermille = 0.0;
        if (d == 0.0) {
            u32PermilleOut = 0;
            return true;
        }
        if (d < 1.0) {
            dPermille = d * 1000.0; // 0.875
        } else if (d <= 100.0) {
            dPermille = d * 10.0; // 87.5
        } else {
            dPermille = d; // 875
        }

        if (dPermille < 500.0 || dPermille > 999.0) {
            return false;
        }
        u32PermilleOut = static_cast<uint32_t>(dPermille + 0.5);
        return true;
    }

    /** \brief Parse an on/off flag: on|off|true|false|yes|no|1|0 (case-insensitive). */
    inline bool onOff(const std::string &strIn, bool &bOut)
    {
        std::string s = strIn;
        s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }), s.end());
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (s == "on" || s == "true" || s == "yes" || s == "1") {
            bOut = true;
            return true;
        }
        if (s == "off" || s == "false" || s == "no" || s == "0") {
            bOut = false;
            return true;
        }
        return false;
    }

} // namespace kcan_parse

#endif // KCAN_PARSE_HPP
