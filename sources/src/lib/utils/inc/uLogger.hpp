#ifndef ULOGGER_H
#define ULOGGER_H

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>
#include <memory>
#include <concepts>
#include <array>
#include <filesystem>
#include <optional>

#include "uGuiNotify.hpp"


/**
 * @brief Enumeration for log levels.
 */
enum class LogLevel : uint8_t {
    EC_VERBOSE,        /**< Verbose log level. */
    EC_DEBUG,          /**< Debug log level. */
    EC_INFO,           /**< Info log level. */
    EC_WARNING,        /**< Warning log level. */
    EC_ERROR,          /**< Error log level. */
    EC_FATAL,          /**< Fatal log level. */
    EC_FIXED,          /**< Fixed log level. */
    EC_EMPTY           /**< Empty log level: prints content with no timestamp/severity prefix; empty string yields a blank line. */
};

inline constexpr auto LOG_VERBOSE = LogLevel::EC_VERBOSE;      /**< Verbose log level constant. */
inline constexpr auto LOG_DEBUG   = LogLevel::EC_DEBUG;        /**< Debug log level constant. */
inline constexpr auto LOG_INFO    = LogLevel::EC_INFO;         /**< Info log level constant. */
inline constexpr auto LOG_WARNING = LogLevel::EC_WARNING;      /**< Warning log level constant. */
inline constexpr auto LOG_ERROR   = LogLevel::EC_ERROR;        /**< Error log level constant. */
inline constexpr auto LOG_FATAL   = LogLevel::EC_FATAL;        /**< Fatal log level constant. */
inline constexpr auto LOG_FIXED   = LogLevel::EC_FIXED;        /**< Fixed log level constant. */
inline constexpr auto LOG_EMPTY   = LogLevel::EC_EMPTY;        /**< Empty log level constant. */


/**
 * @brief Default logger settings 
 */
#define LOGGER_DEFAULT_CONSOLE_SEVERITY  LOG_VERBOSE
#define LOGGER_DEFAULT_LOGFILE_SEVERITY  LOG_VERBOSE
#define LOGGER_DEFAULT_ENABLE_FILELOG    false
#define LOGGER_DEFAULT_INCLUDE_DATE      false
#define LOGGER_DEFAULT_USE_COLORS        true

using ConsoleLogLevel = LogLevel;                           /**< Console log level threshold. */
using FileLogLevel    = LogLevel;                           /**< File log level threshold. */

/**
 * @brief Shared separator
 */
inline static const char* g_pstrLogSeparator = "------------------------------------------------";

/**
 * @brief Conversion from size_t to LogLevel 
 */
inline std::optional<LogLevel> sizet2loglevel(size_t v) {
    switch (v) {
        case 0: return LOG_VERBOSE;
        case 1: return LOG_DEBUG;
        case 2: return LOG_INFO;
        case 3: return LOG_WARNING;
        case 4: return LOG_ERROR;
        case 5: return LOG_FATAL;
        case 6: return LOG_FIXED;
        case 7: return LOG_EMPTY;
        default: return std::nullopt;
    }
}


/**
 * @brief Type concepts for logger
 */
namespace log_concepts {
    template<typename T>
    concept Integral = std::is_integral_v<T> && !std::is_same_v<T, bool>;

    template<typename T>
    concept FloatingPoint = std::is_floating_point_v<T>;

    template<typename T>
    concept Pointer = std::is_pointer_v<T> &&
                      !std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, char>;

    template<typename T>
    concept StringLike = std::is_same_v<T, const char*> || 
                         std::is_same_v<T, std::string> || 
                         std::is_same_v<T, std::string_view>;
}


/**
 * @brief Converts a log level to a string.
 * @param level The log level to convert.
 * @return The string representation of the log level.
 */
[[nodiscard]] constexpr const char* toString(LogLevel level) noexcept
{
    switch (level) {
        case LOG_VERBOSE: return "VERBOSE";
        case LOG_DEBUG:   return "  DEBUG";
        case LOG_INFO:    return "   INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR:   return "  ERROR";
        case LOG_FATAL:   return "  FATAL";
        case LOG_FIXED:   return "  FIXED";
        case LOG_EMPTY:   return "       ";  // blank — no label printed for empty lines
        default:          return "UNKNOWN";
    }
}


/**
 * @brief Gets the color code for a log level.
 * @param level The log level to get the color code for.
 * @return The color code for the log level.
 */
[[nodiscard]] constexpr const char* getColor(LogLevel level) noexcept
{
    switch (level) {
        case LOG_VERBOSE: return "\033[90m"; // Bright Black (Gray)
        case LOG_DEBUG:   return "\033[36m"; // Cyan
        case LOG_INFO:    return "\033[32m"; // Green
        case LOG_WARNING: return "\033[33m"; // Yellow
        case LOG_ERROR:   return "\033[31m"; // Red
        case LOG_FATAL:   return "\033[91m"; // Bright Red
        case LOG_FIXED:   return "\033[97m"; // Bright White
        case LOG_EMPTY:   return "\033[0m";  // Reset — no special colour
        default:          return "\033[0m";  // Reset
    }
}

/**
 * @brief Structure for log buffer with optimized performance and safety.
 */
struct LogBuffer
{
    static constexpr size_t BUFFER_SIZE = 1024;                     /**< Buffer size constant. */
    static constexpr const char* RESET_COLOR = "\033[0m";

    char buffer[BUFFER_SIZE] {};                                    /**< Buffer for storing log messages. */
    size_t size = 0;                                                /**< Size of the log message in the buffer. */
    LogLevel currentLevel = LOG_INFO;                               /**< Current log level. */

    LogLevel consoleThreshold = LOGGER_DEFAULT_CONSOLE_SEVERITY;    /**< Console log level threshold. */
    LogLevel fileThreshold = LOGGER_DEFAULT_LOGFILE_SEVERITY;       /**< File log level threshold. */
    bool fileLoggingEnabled = LOGGER_DEFAULT_ENABLE_FILELOG;        /**< Flag indicating if file logging is enabled. */
    bool useColors = LOGGER_DEFAULT_USE_COLORS;                     /**< Flag indicating if colors are used in console logging. */
    bool includeDate = LOGGER_DEFAULT_INCLUDE_DATE;                 /**< Flag indicating if date is included in log messages. */

    std::ofstream logFile;                                          /**< File stream for logging to a file. */
    std::mutex logMutex;                                            /**< Mutex for synchronizing log access. */

    /**
     * @brief Resets the log buffer.
     */
    void reset() noexcept
    {
        size = 0;
        buffer[0] = '\0';
        currentLevel = LOG_INFO;
    }


    /**
     * @brief Checks if there's enough space in the buffer
     * @param needed Amount of space needed
     * @return true if space available, false otherwise
     */
    [[nodiscard]] constexpr bool hasSpace(size_t needed) const noexcept
    {
        return (size + needed) < BUFFER_SIZE;
    }


    /**
     * @brief Safely appends formatted data to buffer with overflow protection
     * @return Number of characters actually written
     */
    template<typename... Args>
    size_t appendSafe(const char* format, Args... args) noexcept
    {
        if (size >= BUFFER_SIZE) return 0;
        
        int written = std::snprintf(buffer + size, BUFFER_SIZE - size, format, args...);
        if (written < 0) return 0;
        
        size_t actual = static_cast<size_t>(written);
        if (size + actual >= BUFFER_SIZE) {
            // Truncation occurred
            actual = BUFFER_SIZE - size - 1;
            buffer[BUFFER_SIZE - 1] = '\0';
        }
        
        size += actual;
        return actual;
    }


    /**
     * @brief Appends a single character to the log buffer.
     * @param c The character to append.
     */
    void append(char c) noexcept
    {
        appendSafe("%c ", c);
    }


    /**
     * @brief Appends a text message to the log buffer.
     * @param text The text message to append. If 'text' is 'nullptr', no action is taken.
     */
    void append(const char* text) noexcept
    {
        if (text != nullptr) {
            appendSafe("%s ", text);
        }
    }


    /**
     * @brief Appends a string message to the log buffer.
     * @param text The string message to append. If 'text' is empty, no action is taken.
     */
    void append(const std::string& text) noexcept
    {
        if (!text.empty()) {
            append(text.c_str());
        }
    }


    /**
     * @brief Appends a string_view message to the log buffer (optimized, no allocation).
     * @param text_view The string view to append. If 'text_view' is empty, no action is taken.
     */
    void append(std::string_view text_view) noexcept
    {
        if (text_view.empty() || size >= BUFFER_SIZE) return;

        // Direct copy for string_view to avoid allocation
        size_t available = BUFFER_SIZE - size - 2; // -2 for space and null terminator
        size_t toCopy = std::min(text_view.size(), available);
        
        if (toCopy > 0) {
            std::memcpy(buffer + size, text_view.data(), toCopy);
            size += toCopy;
            buffer[size++] = ' ';
            buffer[size] = '\0';
        }
    }

    /**
     * @brief Appends a char array (e.g. char buf[N]) to the log buffer as a string.
     *        Prevents decay to pointer — prints content, not the address.
     * @tparam N The array size (deduced automatically).
     * @param text The char array to append.
     */
    template<size_t N>
    void append(const char (&text)[N]) noexcept
    {
        appendSafe("%.*s ", static_cast<int>(strnlen(text, N)), text);
    }

    /**
     * @brief Appends a std::array<char, N> to the log buffer as a string.
     *        Reads up to the first null terminator or N characters, whichever comes first.
     * @tparam N The array size (deduced automatically).
     * @param text The char array to append.
     */
    template<size_t N>
    void append(const std::array<char, N>& text) noexcept
    {
        // string_view stops at the null terminator thanks to strnlen
        append(std::string_view{ text.data(), strnlen(text.data(), N) });
    }

    /**
     * @brief Appends a boolean value to the internal buffer as a string.
     *
     * It appends the string "true" or "false" to the buffer, followed by a space.
     *
     * @param value The boolean value to append.
     */
    void append(bool value) noexcept
    {
        appendSafe("%s ", value ? "true" : "false");
    }


    /**
     * @brief Appends an integral value to the log buffer.
     * @tparam T The integral type.
     * @param value The value to append.
     */
    template<log_concepts::Integral T>
    void append(T value) noexcept
    {
        if constexpr (std::is_same_v<T, int8_t>) {
            appendSafe("%d ", static_cast<int>(value));
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            appendSafe("%u ", static_cast<unsigned>(value));
        } else if constexpr (std::is_same_v<T, int16_t>) {
            appendSafe("%hd ", value);
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            appendSafe("%hu ", value);
        } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int>) {
            appendSafe("%d ", value);
        } else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, unsigned int>) {
            appendSafe("%u ", value);
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, long long>) {
            appendSafe("%lld ", static_cast<long long>(value));
        } else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, unsigned long long>) {
            appendSafe("%llu ", static_cast<unsigned long long>(value));
        } else if constexpr (std::is_same_v<T, size_t>) {
            appendSafe("%zu ", value);
        } else if constexpr (std::is_signed_v<T>) {
            appendSafe("%lld ", static_cast<long long>(value));
        } else {
            appendSafe("%llu ", static_cast<unsigned long long>(value));
        }
    }


    /**
     * @brief Appends an integral value as hexadecimal to the log buffer.
     * @tparam T The integral type.
     * @param value The value to append.
     */
    template<log_concepts::Integral T>
    void appendHex(T value) noexcept
    {
        if constexpr (std::is_same_v<T, uint8_t>) {
            appendSafe("0x%02X ", static_cast<unsigned>(value));
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            appendSafe("0x%04X ", value);
        } else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, unsigned int>) {
            appendSafe("0x%08X ", value);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            appendSafe("0x%016llX ", static_cast<unsigned long long>(value));
        } else if constexpr (std::is_same_v<T, size_t>) {
            if constexpr (sizeof(size_t) == 8) {
                appendSafe("0x%016zX ", value);
            } else {
                appendSafe("0x%08zX ", value);
            }
        } else {
            // Generic fallback for other integral types
            appendSafe("0x%llX ", static_cast<unsigned long long>(value));
        }
    }


    /**
     * @brief Appends a floating-point value to the log buffer.
     * @tparam T The floating-point type.
     * @param value The value to append.
     */
    template<log_concepts::FloatingPoint T>
    void append(T value) noexcept
    {
        appendSafe("%.8f ", static_cast<double>(value));
    }


    /**
     * @brief Appends a pointer to the log buffer.
     * @tparam T The pointer type.
     * @param ptr The pointer to append.
     */
    template<log_concepts::Pointer T>
    void append(T ptr) noexcept
    {
        appendSafe("%p ", static_cast<const void*>(ptr));
    }


    /**
     * @brief Gets the current timestamp (cached for performance).
     * @return The current timestamp as a string.
     */
    [[nodiscard]] std::string getTimestamp() const
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto duration = now.time_since_epoch();
        auto micros = duration_cast<microseconds>(duration) % 1'000'000;

        std::time_t t = system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        // Pre-allocate string with estimated size
        std::ostringstream oss;
        oss.imbue(std::locale::classic()); // Use C locale for consistent formatting
        
        if (includeDate) {
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        } else {
            oss << std::put_time(&tm, "%H:%M:%S");
        }
        oss << '.' << std::setfill('0') << std::setw(6) << micros.count() << " | ";
        
        return oss.str();
    }


    /**
     * @brief Prints the log message with optimized string concatenation.
     */
    void print()
    {
        // LOG_EMPTY: bypass timestamp/severity prefix entirely.
        // Prints the raw buffer content followed by a newline, or just a blank
        // line when the buffer is empty (i.e. called with an empty string).
        if (currentLevel == LOG_EMPTY) {
            std::lock_guard<std::mutex> lock(logMutex);
            const char* content = (size > 0) ? buffer : "";

            if (g_gui_mode) {
                // GUI mode: emit structured line for w3, no ANSI codes.
                std::printf("GUI:LOG:%s\n", content);
                std::fflush(stdout);
            } else {
                if (useColors) {
                    std::printf("%s%s\n%s", getColor(LOG_EMPTY), content, RESET_COLOR);
                } else {
                    std::printf("%s\n", content);
                }
                std::fflush(stdout);
            }
			
            if (fileLoggingEnabled && logFile.is_open()) {
                logFile << content << '\n';
                logFile.flush();
            }
            reset();
            return;
        }

        if (size == 0) {
            reset();
            return;
        }

        std::lock_guard<std::mutex> lock(logMutex);
        
        // Build the message once
        std::string timestamp = getTimestamp();
        const char* levelStr = toString(currentLevel);
        
        // Pre-calculate total size to avoid reallocations
        size_t totalSize = timestamp.size() + std::strlen(levelStr) + 3 + size + 1; // " | " + buffer + "\n"
        std::string fullMessage;
        fullMessage.reserve(totalSize);
        
        fullMessage.append(timestamp);
        fullMessage.append(levelStr);
        fullMessage.append(" | ");
        fullMessage.append(buffer, size);
        fullMessage.push_back('\n');

        // Console output
        if (currentLevel >= consoleThreshold) {
            if (useColors) {
                // More efficient: print with color codes in one call
                std::printf("%s%s%s", getColor(currentLevel), fullMessage.c_str(), RESET_COLOR);
            } else {
                std::fputs(fullMessage.c_str(), stdout);
            }
            std::fflush(stdout); // Ensure immediate output
        }

        // File output
        if (fileLoggingEnabled && currentLevel >= fileThreshold && logFile.is_open()) {
            logFile.write(fullMessage.data(), fullMessage.size());
            logFile.flush();
        }

        reset();
    }


    /**
     * @brief Sets the current log level.
     * @param level The log level to set.
     */
    void setLevel(LogLevel level) noexcept
    {
        currentLevel = level;
    }


    /**
     * @brief Sets the console log level threshold.
     * @param level The log level threshold to set.
     */
    void setConsoleThreshold(LogLevel level) noexcept
    {
        consoleThreshold = level;
    }


    /**
     * @brief Sets the file log level threshold.
     * @param level The log level threshold to set.
     */
    void setFileThreshold(LogLevel level) noexcept
    {
        fileThreshold = level;
    }

    /**
     * @brief Sets the usage of colored logs
     * @param value The boolean value to set.
     */
    void setColoredLogs(bool value) noexcept
    {
        useColors = value;
    }

    /**
     * @brief Sets the usage of date in logs
     * @param value The boolean value to set.
     */
    void setIncludeDate(bool value) noexcept
    {
        includeDate = value;
    }


    /**
     * @brief Enables file logging with optional custom filename.
     * @param filename Optional custom filename. If empty, auto-generates timestamp-based name.
     * @return true if file logging was successfully enabled, false otherwise.
     */
    bool enableFileLogging(const std::string& filename = "")
    {
        std::lock_guard<std::mutex> lock(logMutex);
        
        if (fileLoggingEnabled && logFile.is_open()) {
            return true; // Already enabled
        }

        std::string actualFilename;
        if (filename.empty()) {
            // Auto-generate filename with timestamp
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            std::ostringstream oss;
            oss << "log_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";
            actualFilename = oss.str();
        } else {
            actualFilename = filename;
        }

        logFile.open(actualFilename, std::ios::out | std::ios::app);
        fileLoggingEnabled = logFile.is_open();
        
        return fileLoggingEnabled;
    }


    /**
     * @brief Disables file logging.
     */
    void disableFileLogging()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        
        if (logFile.is_open()) {
            logFile.flush();
            logFile.close();
        }
        fileLoggingEnabled = false;
    }


    /**
     * @brief Check if file logging is currently enabled
     */
    [[nodiscard]] bool isFileLoggingEnabled() const noexcept
    {
        return fileLoggingEnabled && logFile.is_open();
    }


    /**
     * @brief Destructor - ensures file is closed properly
     */
    ~LogBuffer()
    {
        disableFileLogging();
    }
};



/**
 * @brief Global logger instance with proper initialization.
 */
inline std::shared_ptr<LogBuffer> log_local = std::make_shared<LogBuffer>();


/**
 * @brief Gets the global log buffer instance.
 * @return The global log buffer instance.
 */
[[nodiscard]] inline std::shared_ptr<LogBuffer> getLogger() noexcept
{
    return log_local;
}


/**
 * @brief Sets the global log buffer instance.
 * @param logger The log buffer instance to set.
 */
inline void setLogger(std::shared_ptr<LogBuffer> logger) noexcept
{
    if (logger) {
        log_local = std::move(logger);
    }
}

inline void log_separator(const char* color = "\033[95m") noexcept
{
    std::printf("%s%s\033[0m\n", color, g_pstrLogSeparator);
}

/** --------------------------------  Macros ----------------------------------------------- */

#define LOG_STRING(TEXT)        log_local->append(TEXT);                                     /** @brief Macro for logging a string message.*/
#define LOG_PTR(PTR)            log_local->append(PTR);                                      /** @brief Macro for logging a pointer.*/
#define LOG_BOOL(V)             log_local->append(static_cast<bool>(V));                     /** @brief Macro for logging a boolean value.*/
#define LOG_CHAR(C)             log_local->append(static_cast<char>(C));                     /** @brief Macro for logging a char value. */
#define LOG_UINT8(V)            log_local->append(static_cast<uint8_t>(V));                  /** @brief Macro for logging a uint8_t value.*/
#define LOG_UINT16(V)           log_local->append(static_cast<uint16_t>(V));                 /** @brief Macro for logging a uint16_t value.*/
#define LOG_UINT32(V)           log_local->append(static_cast<uint32_t>(V));                 /** @brief Macro for logging a uint32_t value.*/
#define LOG_UINT64(V)           log_local->append(static_cast<uint64_t>(V));                 /** @brief Macro for logging a uint64_t value.*/
#define LOG_SIZET(V)            log_local->append(static_cast<size_t>(V));                   /** @brief Macro for logging a size_t value.*/
#define LOG_INT8(V)             log_local->append(static_cast<int8_t>(V));                   /** @brief Macro for logging an int8_t value.*/
#define LOG_INT16(V)            log_local->append(static_cast<int16_t>(V));                  /** @brief Macro for logging an int16_t value.*/
#define LOG_INT32(V)            log_local->append(static_cast<int32_t>(V));                  /** @brief Macro for logging an int32_t value.*/
#define LOG_INT64(V)            log_local->append(static_cast<int64_t>(V));                  /** @brief Macro for logging an int64_t value.*/
#define LOG_INT(V)              log_local->append(static_cast<int>(V));                      /** @brief Macro for logging an int value. */
#define LOG_FLOAT(V)            log_local->append(static_cast<float>(V));                    /** @brief Macro for logging a float value.*/
#define LOG_DOUBLE(V)           log_local->append(static_cast<double>(V));                   /** @brief Macro for logging a double value.*/
#define LOG_HEX8(V)             log_local->appendHex(static_cast<uint8_t>(V));               /** @brief Macro for logging a uint8_t value in hexadecimal format.*/
#define LOG_HEX16(V)            log_local->appendHex(static_cast<uint16_t>(V));              /** @brief Macro for logging a uint16_t value in hexadecimal format.*/
#define LOG_HEX32(V)            log_local->appendHex(static_cast<uint32_t>(V));              /** @brief Macro for logging a uint32_t value in hexadecimal format */
#define LOG_HEX64(V)            log_local->appendHex(static_cast<uint64_t>(V));              /** @brief Macro for logging a uint64_t value in hexadecimal format */
#define LOG_HEXSIZET(V)         log_local->appendHex(static_cast<size_t>(V));                /** @brief Macro for logging a size_t value in hexadecimal format */
#define LOG_SEP()               log_separator()
#define LOG_SEPARATOR(COLOR)    log_separator(COLOR)

/**
 * @brief Macro for printing a log message with a specified severity.
 * @param SEVERITY The severity level of the log message.
 * @param ... The log message to print.
 */
#define LOG_PRINT(SEVERITY, ...)  \
                    do { \
                        log_local->setLevel(SEVERITY); \
                        __VA_ARGS__ \
                        log_local->print(); \
                    } while(0)


/**
 * @brief Macro for initializing the logger.
 * @param CONSOLE_LEVEL The console log level threshold.
 * @param FILE_LEVEL The file log level threshold.
 * @param ENABLE_FILE Flag indicating if file logging is enabled.
 * @param ENABLE_COLORS Flag indicating if colors are used in console logging.
 * @param INCLUDE_DATE Flag indicating if date is included in log messages.
 */
#define LOG_INIT(CONSOLE_LEVEL, FILE_LEVEL, ENABLE_FILE, ENABLE_COLORS, INCLUDE_DATE) \
                    do { \
                        log_local->setConsoleThreshold(CONSOLE_LEVEL); \
                        log_local->setFileThreshold(FILE_LEVEL); \
                        log_local->setColoredLogs(ENABLE_COLORS); \
                        log_local->setIncludeDate(INCLUDE_DATE); \
                        if (ENABLE_FILE) { \
                            log_local->enableFileLogging(); \
                        } else { \
                            log_local->disableFileLogging(); \
                        } \
                    } while(0)


/**
 * @brief Macro for deinitializing the logger.
 */
#define LOG_DEINIT() \
                    log_local->disableFileLogging()


#endif // ULOGGER_H
