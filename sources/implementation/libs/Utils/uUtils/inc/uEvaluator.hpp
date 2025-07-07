#ifndef UEVALUATOR_H
#define UEVALUATOR_H

#include <utility>

namespace eval {

constexpr std::pair<std::string_view, bool> mappings[] = {
    {"TRUE", true},
    {"!FALSE", true},
    {"FALSE", false},
    {"!TRUE", false}
};

constexpr bool string2bool(std::string_view input) {
    for (auto [key, value] : mappings) {
        if (key == input) return value;
    }
    return false; // or throw
}

}; // namespace eval {


#endif // UEVALUATOR_H