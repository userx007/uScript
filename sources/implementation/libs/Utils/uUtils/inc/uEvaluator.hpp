#ifndef UEVALUATOR_H
#define UEVALUATOR_H

#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>

namespace eval {

bool string2bool(std::string_view token, bool& result)
{
    static const std::unordered_map<std::string_view, bool> token_map = {
        {"TRUE",   true}, {"!FALSE", true}, {"1", true}, {"!0", true},
        {"FALSE",  false}, {"!TRUE", false}, {"0", false}, {"!1", false}
    };

    auto it = token_map.find(token);
    if (it != token_map.end()) {
        result = it->second;
        return true;
    }

    return false;
}

std::vector<std::string_view> split_tokens(const std::string& input)
{
    std::vector<std::string_view> tokens;
    std::istringstream stream(input);
    std::string token;
    while (stream >> token) {
        tokens.emplace_back(token);
    }
    return tokens;
}

bool validateVectorBooleans(const std::string& boolString, const std::string& rule, bool& outResult)
{
    enum class BoolRule { OR, AND };

    BoolRule evalRule;
    if (rule == "OR")      evalRule = BoolRule::OR;
    else if (rule == "AND") evalRule = BoolRule::AND;
    else                   return false;

    auto rawTokens = split_tokens(boolString);
    std::vector<bool> values;

    for (const auto& token : rawTokens) {
        bool val;
        if (!convert_to_bool(token, val)) {
            return false;
        }
        values.push_back(val);
    }

    if (values.empty()) {
        return false;
    }

    if (evalRule == BoolRule::OR) {
        outResult = std::any_of(values.begin(), values.end(), [](bool b) { return b; });
    } else {
        outResult = std::all_of(values.begin(), values.end(), [](bool b) { return b; });
    }

    return true;
}

}; // namespace eval


#endif // UEVALUATOR_H