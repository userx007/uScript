#pragma once

#include <cctype>
#include <cstring>
#include <string>

/**
 * Portable glob matcher with the semantics of POSIX fnmatch(pattern, string, 0).
 *
 * Exists because fnmatch() is not available on Windows (MSVC / MinGW). On POSIX the callers keep
 * using the real fnmatch(); this is only the Windows fallback (it is compiled everywhere so it can be
 * unit-tested against fnmatch() on Linux).
 *
 * Supported (flags == 0, i.e. no FNM_PATHNAME / FNM_PERIOD / FNM_NOESCAPE / FNM_CASEFOLD):
 *   *            any sequence of characters, including '/' and a leading '.'
 *   ?            any single character
 *   [abc] [a-z]  bracket expression; '!' or '^' right after '[' negates; ']' first is literal;
 *                POSIX classes [:alpha:] [:digit:] [:alnum:] [:upper:] [:lower:] [:space:]
 *                [:punct:] [:xdigit:] [:blank:] [:cntrl:] [:graph:] [:print:]
 *   \c           backslash escapes the next character (also inside brackets)
 *   an unterminated '[' is an ordinary character
 * Not supported: collating symbols / equivalence classes inside brackets ([.x.], [=x=]) - locale
 * dependent in glibc and irrelevant for topic/path patterns. (Fuzzed against glibc fnmatch(): identical
 * results except for such malformed collating-symbol brackets.)
 * Matching is byte-wise and case sensitive (the "C" locale behaviour of fnmatch).
 */
namespace uglob
{

    namespace detail
    {

        inline bool classMatch(const std::string &strName, unsigned char c)
        {
            if (strName == "alpha") {
                return std::isalpha(c) != 0;
            }
            if (strName == "digit") {
                return std::isdigit(c) != 0;
            }
            if (strName == "alnum") {
                return std::isalnum(c) != 0;
            }
            if (strName == "upper") {
                return std::isupper(c) != 0;
            }
            if (strName == "lower") {
                return std::islower(c) != 0;
            }
            if (strName == "space") {
                return std::isspace(c) != 0;
            }
            if (strName == "punct") {
                return std::ispunct(c) != 0;
            }
            if (strName == "xdigit") {
                return std::isxdigit(c) != 0;
            }
            if (strName == "blank") {
                return c == ' ' || c == '\t';
            }
            if (strName == "cntrl") {
                return std::iscntrl(c) != 0;
            }
            if (strName == "graph") {
                return std::isgraph(c) != 0;
            }
            if (strName == "print") {
                return std::isprint(c) != 0;
            }
            return false;
        }

        /** Match exactly one (non-'*') pattern element at `p` against `c`; advances `p` past the element. */
        inline bool matchElement(const char *&p, unsigned char c)
        {
            switch (*p) {
            case '?':
                ++p;
                return true;

            case '[': {
                const char *q = p + 1;
                bool bNegate  = false;
                if (*q == '!' || *q == '^') {
                    bNegate = true;
                    ++q;
                }
                bool bMatched = false;
                bool bFirst   = true;
                for (;;) {
                    if (*q == '\0') { // no closing ']': '[' is an ordinary character
                        ++p;
                        return c == '[';
                    }
                    if (*q == ']' && !bFirst) {
                        ++q;
                        break;
                    }
                    bFirst = false;

                    if (*q == '[' && q[1] == ':') {
                        const char *pEnd = std::strstr(q + 2, ":]");
                        if (pEnd != nullptr) {
                            if (classMatch(std::string(q + 2, pEnd), c)) {
                                bMatched = true;
                            }
                            q = pEnd + 2;
                            continue;
                        }
                    }

                    if (*q == '\\' && q[1] != '\0') {
                        ++q;
                    }
                    const unsigned char lo = static_cast<unsigned char>(*q++);
                    unsigned char hi       = lo;
                    if (*q == '-' && q[1] != '\0' && q[1] != ']') {
                        ++q;
                        if (*q == '\\' && q[1] != '\0') {
                            ++q;
                        }
                        hi = static_cast<unsigned char>(*q++);
                    }
                    if (lo <= c && c <= hi) {
                        bMatched = true;
                    }
                }
                p = q;
                return bMatched != bNegate;
            }

            case '\\':
                if (p[1] != '\0') {
                    ++p; // match the escaped character literally
                }
                [[fallthrough]];
            default: {
                const bool bEq = static_cast<unsigned char>(*p) == c;
                ++p;
                return bEq;
            }
            }
        }

    } // namespace detail

    /** @return true if `pszStr` matches the glob `pszPattern` (fnmatch(..., 0) semantics). */
    inline bool match(const char *pszPattern, const char *pszStr)
    {
        const char *p     = pszPattern;
        const char *s     = pszStr;
        const char *starP = nullptr; // pattern position just after the last '*'
        const char *starS = nullptr; // string position that '*' currently covers up to

        for (;;) {
            if (*p == '*') {
                while (*p == '*') {
                    ++p;
                }
                if (*p == '\0') {
                    return true; // trailing '*' matches the rest
                }
                starP = p;
                starS = s;
                continue;
            }

            if (*s != '\0') {
                const char *q = p;
                if (*p != '\0' && detail::matchElement(q, static_cast<unsigned char>(*s))) {
                    p = q;
                    ++s;
                    continue;
                }
            } else if (*p == '\0') {
                return true; // both exhausted
            }

            // mismatch: let the last '*' swallow one more character and retry
            if (starP != nullptr && *starS != '\0') {
                p = starP;
                s = ++starS;
                continue;
            }
            return false;
        }
    }

    inline bool match(const std::string &strPattern, const std::string &strStr)
    {
        return match(strPattern.c_str(), strStr.c_str());
    }

} // namespace uglob
