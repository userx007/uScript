#ifndef ULINEPARSER_HPP
#define ULINEPARSER_HPP

#include <string>
#include <string_view>
#include <utility>
#include <cctype>
#include <algorithm>

enum class Direction {
    Input,
    Output,
    Invalid
};

struct ParsedLine {
    Direction direction = Direction::Invalid;
    std::pair<std::string, std::string> values = {"", ""};
};

class LineParser
{
    public:

        bool parse(std::string_view line, ParsedLine& result) const
        {
            result = ParsedLine{};

            if (line.empty()) return false;

            // Determine direction
            char firstChar = line.front();
            switch (firstChar) {
                case '>': result.direction = Direction::Output; break;
                case '<': result.direction = Direction::Input;  break;
                default: return false;
            }

            line.remove_prefix(1);
            skipWhitespace(line);

            std::string field1, field2;
            bool insideQuote = false;
            bool separatorFound = false;

            for (char ch : line) {
                if (ch == '"') {
                    insideQuote = !insideQuote;
                } else if (ch == '|' && !insideQuote) {
                    if (separatorFound) return false;  // Multiple separators
                    separatorFound = true;
                } else {
                    (separatorFound ? field2 : field1) += ch;
                }
            }

            trim(field1);
            trim(field2);

            // Invalid if separator exists but one side is empty
            if ((separatorFound && field1.empty()) || (separatorFound && field2.empty())) {
                return false;
            }

            result.values = std::make_pair(field1, field2);
            return true;
        }

    private:

        static void skipWhitespace(std::string_view& sv)
        {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                sv.remove_prefix(1);
        }

        static void trim(std::string& s)
        {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
        }
};

#endif // ULINEPARSER_HPP