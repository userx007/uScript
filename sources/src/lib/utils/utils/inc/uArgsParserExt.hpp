#ifndef UARGS_PARSER_EXT_HPP
#define UARGS_PARSER_EXT_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>
#include <iostream>
#include <sstream>
#include <algorithm>

class CommandLineParser
{
public:
    struct ParseResult {
        bool success = true;
        std::vector<std::string> errors;
        operator bool() const { return success; }
    };

    enum class OptionType { String, Flag, Int, Float };

    CommandLineParser(std::string description = "")
        : description_(std::move(description)) {}

    // Add an option with comprehensive configuration
    void add_option(std::string long_flag, std::string short_flag = "",
                    std::string help = "", bool required = false,
                    std::string default_value = "",
                    OptionType type = OptionType::String)
    {
        OptionConfig config{std::move(long_flag), std::move(short_flag),
                           std::move(help), std::move(default_value),
                           required, type};
        options_[config.long_flag] = config;
        if (!config.short_flag.empty()) {
            short_to_long_[config.short_flag] = config.long_flag;
        }
    }

    // Convenience method for boolean flags
    void add_flag(std::string long_flag, std::string short_flag = "",
                  std::string help = "")
    {
        add_option(std::move(long_flag), std::move(short_flag),
                   std::move(help), false, "false", OptionType::Flag);
    }

    // Parse command line arguments with error handling
    ParseResult parse(int argc, const char *argv[])
    {
        ParseResult result;
        
        // Clear previous parse state (CRITICAL BUG FIX)
        parsed_options_.clear();
        positional_args_.clear();
        positional_args_.reserve(argc);
        
        // Apply defaults
        for (const auto& [flag, config] : options_) {
            if (!config.default_value.empty()) {
                parsed_options_[flag] = config.default_value;
            }
        }

        std::string current_flag;
        OptionType current_type = OptionType::String;

        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];

            // Handle long flags (--flag)
            if (arg.starts_with("--")) {
                // Save previous flag if it was a boolean
                if (!current_flag.empty() && current_type == OptionType::Flag) {
                    parsed_options_[current_flag] = "true";
                }

                std::string flag = std::string(arg.substr(2));
                auto it = options_.find(flag);
                if (it == options_.end()) {
                    result.errors.push_back("Unknown option: --" + flag);
                    result.success = false;
                    current_flag.clear();
                    continue;
                }
                current_flag = std::move(flag);
                current_type = it->second.type;
                parsed_options_[current_flag] = (current_type == OptionType::Flag) ? "true" : "";
            }
            // Handle short flags (-f)
            else if (arg.starts_with("-") && arg.length() > 1 && !is_number(arg)) {
                if (!current_flag.empty() && current_type == OptionType::Flag) {
                    parsed_options_[current_flag] = "true";
                }

                std::string short_flag = std::string(arg.substr(1));
                auto it = short_to_long_.find(short_flag);
                if (it == short_to_long_.end()) {
                    result.errors.push_back("Unknown option: -" + short_flag);
                    result.success = false;
                    current_flag.clear();
                    continue;
                }
                current_flag = it->second;
                current_type = options_[current_flag].type;
                parsed_options_[current_flag] = (current_type == OptionType::Flag) ? "true" : "";
            }
            // Handle values
            else {
                if (!current_flag.empty() && current_type != OptionType::Flag) {
                    parsed_options_[current_flag] = std::string(arg);
                    current_flag.clear();
                } else {
                    positional_args_.emplace_back(arg);
                }
            }
        }

        // Check if last flag was expecting a value
        if (!current_flag.empty() && current_type != OptionType::Flag &&
            parsed_options_[current_flag].empty()) {
            result.errors.push_back("Option --" + current_flag + " requires a value");
            result.success = false;
        }

        // Validate required options
        for (const auto& [flag, config] : options_) {
            if (config.required && parsed_options_.count(flag) == 0) {
                result.errors.push_back("Required option missing: --" + flag);
                result.success = false;
            }
        }

        // Type validation
        for (const auto& [flag, value] : parsed_options_) {
            auto it = options_.find(flag);
            if (it != options_.end() && !validate_type(value, it->second.type)) {
                result.errors.push_back("Invalid value for --" + flag +
                                      ": expected " + type_name(it->second.type));
                result.success = false;
            }
        }

        return result;
    }

    // Check if option was provided
    bool has(const std::string& key) const 
    { 
        return parsed_options_.count(key) > 0; 
    }

    // Get string value
    std::optional<std::string> get(const std::string& key) const
    {
        auto it = parsed_options_.find(key);
        return (it != parsed_options_.end()) ? std::optional(it->second) : std::nullopt;
    }

    // Get string value with default
    std::string get_or(const std::string& key, const std::string& default_value) const
    {
        auto it = parsed_options_.find(key);
        return (it != parsed_options_.end()) ? it->second : default_value;
    }

    // Get boolean value (TYPE-SAFE)
    bool get_flag(const std::string& key) const
    {
        auto it = parsed_options_.find(key);
        if (it != parsed_options_.end()) {
            const std::string& val = it->second;
            return val == "true" || val == "1" || val == "yes";
        }
        return false;
    }

    // Get integer value (TYPE-SAFE)
    std::optional<int> get_int(const std::string& key) const
    {
        auto it = parsed_options_.find(key);
        if (it != parsed_options_.end()) {
            try { return std::stoi(it->second); }
            catch (...) { return std::nullopt; }
        }
        return std::nullopt;
    }

    // Get float value (TYPE-SAFE)
    std::optional<float> get_float(const std::string& key) const
    {
        auto it = parsed_options_.find(key);
        if (it != parsed_options_.end()) {
            try { return std::stof(it->second); }
            catch (...) { return std::nullopt; }
        }
        return std::nullopt;
    }

    // Get positional arguments
    const std::vector<std::string>& get_positional() const 
    { 
        return positional_args_; 
    }

    // Print formatted usage information (IMPROVED FORMATTING)
    void print_usage(const std::string& program_name = "") const
    {
        if (!description_.empty()) {
            std::cout << description_ << "\n\n";
        }

        if (!program_name.empty()) {
            std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
        }

        if (options_.empty()) return;

        std::cout << "Options:\n";
        
        // Find max width for alignment
        size_t max_width = 0;
        for (const auto& [flag, config] : options_) {
            size_t width = flag.length() + 4;
            if (!config.short_flag.empty()) {
                width += config.short_flag.length() + 4;
            }
            max_width = std::max(max_width, width);
        }

        // Print each option
        for (const auto& [flag, config] : options_) {
            std::ostringstream oss;
            oss << "  --" << flag;
            if (!config.short_flag.empty()) {
                oss << ", -" << config.short_flag;
            }
            
            std::string flags_str = oss.str();
            std::cout << flags_str;
            
            if (flags_str.length() < max_width) {
                std::cout << std::string(max_width - flags_str.length(), ' ');
            }
            
            std::cout << "  " << config.help;
            
            if (config.type != OptionType::Flag && config.type != OptionType::String) {
                std::cout << " [" << type_name(config.type) << "]";
            }
            
            if (!config.default_value.empty()) {
                std::cout << " (default: " << config.default_value << ")";
            }
            
            if (config.required) {
                std::cout << " [REQUIRED]";
            }
            
            std::cout << "\n";
        }
    }

    // Print errors from parse result
    static void print_errors(const ParseResult& result, std::ostream& out = std::cerr)
    {
        if (!result.success && !result.errors.empty()) {
            out << "Parsing errors:\n";
            for (const auto& error : result.errors) {
                out << "  - " << error << "\n";
            }
        }
    }

private:
    struct OptionConfig {
        std::string long_flag;
        std::string short_flag;
        std::string help;
        std::string default_value;
        bool required = false;
        OptionType type = OptionType::String;
    };

    // Check if string looks like a negative number
    static bool is_number(std::string_view str)
    {
        return str.length() >= 2 && str[0] == '-' && std::isdigit(str[1]);
    }

    // Validate value matches expected type
    static bool validate_type(const std::string& value, OptionType type)
    {
        if (value.empty()) return true;
        
        try {
            switch (type) {
                case OptionType::String:
                case OptionType::Flag:
                    return true;
                case OptionType::Int:
                    std::stoi(value);
                    return true;
                case OptionType::Float:
                    std::stof(value);
                    return true;
            }
        } catch (...) {
            return false;
        }
        return false;
    }

    // Get human-readable type name
    static std::string type_name(OptionType type)
    {
        switch (type) {
            case OptionType::String: return "string";
            case OptionType::Flag: return "flag";
            case OptionType::Int: return "int";
            case OptionType::Float: return "float";
        }
        return "unknown";
    }

    std::string description_;
    std::unordered_map<std::string, OptionConfig> options_;
    std::unordered_map<std::string, std::string> parsed_options_;
    std::unordered_map<std::string, std::string> short_to_long_;
    std::vector<std::string> positional_args_;
};

#endif // UARGS_PARSER_EXT_HPP



///////////////////////////////////////////////////////////////////////
// USAGE:
///////////////////////////////////////////////////////////////////////

/*
int main(int argc, const char* argv[]) {
    CommandLineParser parser("My awesome tool");
    
    parser.add_option("input", "i", "Input file", true);
    parser.add_option("output", "o", "Output file", false, "out.txt");
    parser.add_option("threads", "t", "Thread count", false, "4", 
                     CommandLineParser::OptionType::Int);
    parser.add_flag("verbose", "v", "Enable verbose output");
    
    auto result = parser.parse(argc, argv);
    
    if (!result) {
        CommandLineParser::print_errors(result);
        parser.print_usage(argv[0]);
        return 1;
    }
    
    // Type-safe access
    auto input = parser.get("input").value();
    int threads = parser.get_int("threads").value_or(1);
    bool verbose = parser.get_flag("verbose");
    
    return 0;
}

*/