
#ifndef UFLAG_PARSER_H
#define UFLAG_PARSER_H

#include <string>
#include <string_view>
#include <unordered_set>
#include <cctype>
#include <stdexcept>
#include <unordered_map>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @class FlagParser
 * @brief Parses a string of flags and provides access to their boolean values.
 *
 * This class interprets a string of characters as flags, where the case of each character
 * determines its boolean value: uppercase means `true`, lowercase means `false`.
 * It ensures that no flag appears in both cases (e.g., 'a' and 'A') to avoid ambiguity.
 */
/*--------------------------------------------------------------------------------------------------------*/

class FlagParser
{
public:

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Constructs a FlagParser from a flag string.
     *
     * Each character in the string represents a flag. Uppercase characters are interpreted
     * as `true`, lowercase as `false`. If the same letter appears in both cases the parser
     * is left in an invalid state; call isValid() before use.
     *
     * @param flags A string containing flag characters.
     */
    /*--------------------------------------------------------------------------------------------------------*/

    FlagParser(std::string_view flags) : m_bValid(false)
    {
        if (!validate_flag_string(flags)) {
            // Conflict detected — parser remains invalid; caller should check isValid()
            return;
        }
        for (char c : flags) {
            m_umapFlags[std::tolower(c)] = std::isupper(c);
        }
        m_bValid = true;
    }

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Returns whether the flag string was valid at construction time.
     *
     * @return `true` if the flag string contained no conflicting cases, `false` otherwise.
     */
    /*--------------------------------------------------------------------------------------------------------*/

    [[nodiscard]] bool isValid() const noexcept { return m_bValid; }


    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Retrieves the boolean value of a given flag.
     *
     * @param flag The character representing the flag to query.
     * @return `true` if the flag was set as uppercase, `false` if lowercase or not present.
     */
    /*--------------------------------------------------------------------------------------------------------*/

    bool get_flag(char flag) const
    {
        auto it = m_umapFlags.find(std::tolower(flag));
        if (it == m_umapFlags.end()) {
            return false;
        }
        return it->second;
    } /* get_flag() */


private:

    std::unordered_map<char, bool> m_umapFlags; ///< Stores flags with their boolean values.
    bool m_bValid = false;                       ///< True iff the flag string passed validation.


    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * @brief Validates that the flag string does not contain both cases of the same letter.
     *
     * @param flags The flag string to validate.
     * @return `true` if valid, `false` if both cases of any letter are present.
     */
    /*--------------------------------------------------------------------------------------------------------*/

    bool validate_flag_string(std::string_view flags)
    {
        std::unordered_set<char> seen;
        for (char c : flags) {
            char lower = std::tolower(c);
            if (seen.count(lower)) {
                return false;
            }
            seen.insert(lower);
        }
        return true;
    } /* validate_flag_string() */
};


#endif // UFLAG_PARSER_H
