#ifndef UARGS_PARSER_EXT_HPP
#define UARGS_PARSER_EXT_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <iostream>

class CommandLineParser
{
    public:

        CommandLineParser(std::string description = "")
            : description_(std::move(description)) {}

        void add_option(const std::string& long_flag,
                        const std::string& short_flag,
                        const std::string& help = "",
                        bool required = false,
                        std::string default_value = "")
        {
            help_texts_[long_flag] = help;
            short_to_long_[short_flag] = long_flag;

            if (!default_value.empty()) {
                parsed_options_[long_flag] = default_value;
            }

            if (required) {
                required_flags_.insert(long_flag);
            }
        }

        void parse(int argc, const char *argv[])
        {
            positional_args_.clear();
            std::string current_flag;

            for (int i = 1; i < argc; ++i) {
                std::string arg = argv[i];

                if (arg.rfind("--", 0) == 0) {
                    current_flag = arg.substr(2);
                    parsed_options_[current_flag] = "";
                } else if (arg.rfind("-", 0) == 0) {
                    auto it = short_to_long_.find(arg.substr(1));
                    if (it != short_to_long_.end()) {
                        current_flag = it->second;
                        parsed_options_[current_flag] = "";
                    }
                } else {
                    if (!current_flag.empty()) {
                        parsed_options_[current_flag] = arg;
                        current_flag.clear();
                    } else {
                        positional_args_.push_back(arg);
                    }
                }
            }
        }

        bool has(const std::string& key) const
        {
            return parsed_options_.count(key) > 0;
        }

        std::optional<std::string> get(const std::string& key) const
        {
            auto it = parsed_options_.find(key);
            if (it != parsed_options_.end())
                return it->second;
            return std::nullopt;
        }

        std::vector<std::string> get_positional() const
        {
            return positional_args_;
        }

        bool check_required() const
        {
            for (const auto& req : required_flags_) {
                if (parsed_options_.count(req) == 0)
                    return false;
            }
            return true;
        }

        void print_usage() const
        {
            std::cout << "Usage:\n";
            for (const auto& [long_flag, help] : help_texts_) {
                std::string short_flag;
                for (const auto& [s, l] : short_to_long_) {
                    if (l == long_flag)
                        short_flag = s;
                }
                std::cout << "  --" << long_flag;
                if (!short_flag.empty()) {
                    std::cout << " (-" << short_flag << ")";
                }
                std::cout << "\t" << help << "\n";
            }
        }

    private:

        std::string description_;
        std::unordered_map<std::string, std::string> help_texts_;
        std::unordered_map<std::string, std::string> parsed_options_;
        std::unordered_map<std::string, std::string> short_to_long_;
        std::unordered_set<std::string> required_flags_;
        std::vector<std::string> positional_args_;
};

#endif // UARGS_PARSER_EXT_HPP

