
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
     * as `true`, lowercase as `false`. Throws an exception if the same letter appears in both cases.
     *
     * @param flags A string containing flag characters.
     * @throws std::invalid_argument if the flag string contains both uppercase and lowercase versions of the same letter.
     */
    /*--------------------------------------------------------------------------------------------------------*/

    FlagParser(std::string_view flags)
    {
        if (!validate_flag_string(flags)) {
            throw std::invalid_argument("Flag string contains both cases of the same letter");
        }
        for (char c : flags) {
            m_umapFlags[std::tolower(c)] = std::isupper(c);
        }
    }


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
