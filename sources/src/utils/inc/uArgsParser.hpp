#ifndef UARGS_PARSER_HPP
#define UARGS_PARSER_HPP

#include <string>
#include <unordered_map>
#include <optional>
#include <iostream>

class CommandLineParser {
public:
    CommandLineParser(std::string description = "")
        : description_(std::move(description)) {}

    void add_option(const std::string& key, const std::string& help_text = "") {
        help_texts_[key] = help_text;
    }

    void parse(int argc, const char *argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--", 0) == 0) {
                std::string key = arg.substr(2);
                std::string value;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    value = argv[++i];
                }
                parsed_options_[key] = value;
            }
        }
    }

    std::optional<std::string> get(const std::string& key) const {
        auto it = parsed_options_.find(key);
        if (it != parsed_options_.end() && !it->second.empty())
            return it->second;
        return std::nullopt;
    }

    bool has(const std::string& key) const {
        return parsed_options_.count(key) > 0;
    }

    void print_usage() const {
        std::cout << "Usage:\n";
        for (const auto& [key, text] : help_texts_) {
            std::cout << "  --" << key << "\t" << text << "\n";
        }
    }

private:
    std::string description_;
    std::unordered_map<std::string, std::string> help_texts_;
    std::unordered_map<std::string, std::string> parsed_options_;
};

#endif // UARGS_PARSER_HPP
