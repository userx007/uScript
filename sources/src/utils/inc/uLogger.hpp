
#ifndef ULOGGER_H
#define ULOGGER_H

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>
#include <memory>


/**
 * @brief Enumeration for log levels.
 */
enum class LogLevel {
    EC_VERBOSE,        /**< Verbose log level. */
    EC_DEBUG,          /**< Debug log level. */
    EC_INFO,           /**< Info log level. */
    EC_WARNING,        /**< Warning log level. */
    EC_ERROR,          /**< Error log level. */
    EC_FATAL,          /**< Fatal log level. */
    EC_FIXED           /**< Fixed log level. */
};


inline constexpr auto LOG_VERBOSE = LogLevel::EC_VERBOSE;      /**< Verbose log level constant. */
inline constexpr auto LOG_DEBUG   = LogLevel::EC_DEBUG;        /**< Debug log level constant. */
inline constexpr auto LOG_INFO    = LogLevel::EC_INFO;         /**< Info log level constant. */
inline constexpr auto LOG_WARNING = LogLevel::EC_WARNING;      /**< Warning log level constant. */
inline constexpr auto LOG_ERROR   = LogLevel::EC_ERROR;        /**< Error log level constant. */
inline constexpr auto LOG_FATAL   = LogLevel::EC_FATAL;        /**< Fatal log level constant. */
inline constexpr auto LOG_FIXED   = LogLevel::EC_FIXED;        /**< Fixed log level constant. */

using ConsoleLogLevel = LogLevel;                           /**< Console log level threshold. */
using FileLogLevel    = LogLevel;                           /**< File log level threshold. */


/**
 * @brief Converts a log level to a string.
 * @param level The log level to convert.
 * @return The string representation of the log level.
 */
inline const char* toString(LogLevel level)
{
    switch (level) {
        case LOG_VERBOSE:
            return "VERBOSE";
        case LOG_DEBUG:
            return "  DEBUG";
        case LOG_INFO:
            return "   INFO";
        case LOG_WARNING:
            return "WARNING";
        case LOG_ERROR:
            return "  ERROR";
        case LOG_FATAL:
            return "  FATAL";
        case LOG_FIXED:
            return "  FIXED";
        default:
            return "UNKNOWN";
    }
}


/**
 * @brief Gets the color code for a log level.
 * @param level The log level to get the color code for.
 * @return The color code for the log level.
 */
inline const char* getColor(LogLevel level)
{
    switch (level) {
        case LOG_VERBOSE:
            return "\033[90m"; // Bright Black (Gray)
        case LOG_DEBUG:
            return "\033[36m"; // Cyan
        case LOG_INFO:
            return "\033[32m"; // Green
        case LOG_WARNING:
            return "\033[33m"; // Yellow
        case LOG_ERROR:
            return "\033[31m"; // Red
        case LOG_FATAL:
            return "\033[91m"; // Bright Red
        case LOG_FIXED:
            return "\033[97m"; // Bright White
        default:
            return "\033[0m" ; // Reset
    }
}


/**
 * @brief Structure for log buffer.
 */
struct LogBuffer {
    static constexpr size_t BUFFER_SIZE = 1024;     /**< Buffer size constant. */
    char buffer[BUFFER_SIZE] {};                    /**< Buffer for storing log messages. */
    size_t size = 0;                                /**< Size of the log message in the buffer. */
    LogLevel currentLevel = LOG_INFO;               /**< Current log level. */

    LogLevel consoleThreshold = LOG_VERBOSE;        /**< Console log level threshold. */
    LogLevel fileThreshold = LOG_VERBOSE;           /**< File log level threshold. */

    std::ofstream logFile;                          /**< File stream for logging to a file. */
    std::mutex logMutex;                            /**< Mutex for synchronizing log access. */

    bool fileLoggingEnabled = false;                /**< Flag indicating if file logging is enabled. */
    bool useColors = true;                          /**< Flag indicating if colors are used in console logging. */
    bool includeDate = true;                        /**< Flag indicating if date is included in log messages. */


    /**
     * @brief Resets the log buffer.
     */

    void reset()
    {
        size = 0;
        buffer[0] = '\0';
        currentLevel = LOG_INFO;
    }



    /**
     * @brief Appends a text message to the log buffer.
     * @param text The text message to append. If 'text' is 'nullptr', no action is taken.
     */

    void append(const char* text)
    {
        if (nullptr != text) {
            size += std::snprintf(buffer + size, BUFFER_SIZE - size, "%s ", text);
        }
    }



    /**
     * @brief Appends a string message to the log buffer.
     * @param text The string message to append. If 'text' is empty, no action is taken.
     */

    void append(const std::string& text)
    {
        if (!text.empty()) {
            append(text.c_str());
        }
    }



    /**
     * @brief Appends a boolean value to the internal buffer as a string.
     *
     * It appends the string "true" or "false" to the buffer, followed by a space.
     *
     * @param value The boolean value to append.
     */
    void append(bool value)
    {
        size += std::snprintf(buffer + size, BUFFER_SIZE - size, "%s ", value ? "true" : "false");
    }



    /**
     * @brief Appends a value to the log buffer.
     * @tparam T The type of the value.
     * @param value The value to append.
     */
    /**< Specialization for integral types. */

    template<typename T>
    typename std::enable_if < std::is_integral<T>::value && !std::is_same<T, bool>::value >::type
    append(T value)
    {
        const char* format = std::is_signed<T>::value ? "%d " : "%u ";
        size += std::snprintf(buffer + size, BUFFER_SIZE - size, format, value);
    }



    /**
     * @brief Appends a value to the log buffer.
     * @tparam T The type of the value.
     * @param value The value to append.
     */
    /**< Specialization for integral types. */

    template<typename T>
    typename std::enable_if < std::is_integral<T>::value && !std::is_same<T, bool>::value >::type
    appendHex(T value)
    {
        size += std::snprintf(buffer + size, BUFFER_SIZE - size, "0x%X ", value);
    }



    /**
     * @brief Appends a value to the log buffer.
     * @tparam T The type of the value.
     * @param value The value to append.
     */
    /**< Specialization for floating-point types. */

    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value>::type
    append(T value)
    {
        size += std::snprintf(buffer + size, BUFFER_SIZE - size, "%.8f ", value);
    }



    /**
     * @brief Appends a value to the log buffer.
     * @tparam T The type of the value.
     * @param value The value to append.
     */
    /**< Specialization for pointer types. */

    template<typename T>
    typename std::enable_if<std::is_pointer<T>::value>::type
    append(T ptr)
    {
        size += std::snprintf(buffer + size, BUFFER_SIZE - size, "%p ", static_cast<const void*>(ptr));
    }



    /**
     * @brief Gets the current timestamp.
     * @return The current timestamp as a string.
     */

    std::string getTimestamp() const
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

        std::ostringstream oss;
        if (includeDate) {
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        } else {
            oss << std::put_time(&tm, "%H:%M:%S");
        }
        oss << "." << std::setfill('0') << std::setw(6) << micros.count() << " | ";
        return oss.str();
    }



    /**
     * @brief Prints the log message.
     */

    void print()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        std::string timestamp = getTimestamp();
        std::string levelStr = toString(currentLevel);
        std::string fullMessage = timestamp + levelStr + " | " + buffer + "\n";

        if (currentLevel >= consoleThreshold) {
            if (useColors) {
                std::printf("%s%s\033[0m", getColor(currentLevel), fullMessage.c_str());
            } else {
                std::printf("%s", fullMessage.c_str());
            }
        }

        if (fileLoggingEnabled && currentLevel >= fileThreshold && logFile.is_open()) {
            logFile << fullMessage;
            logFile.flush();
        }

        reset();
    }



    /**
     * @brief Sets the current log level.
     * @param level The log level to set.
     */
    void setLevel(LogLevel level)
    {
        currentLevel = level;
    }



    /**
     * @brief Sets the console log level threshold.
     * @param level The log level threshold to set.
     */
    void setConsoleThreshold(LogLevel level)
    {
        consoleThreshold = level;
    }



    /**
     * @brief Sets the file log level threshold.
     * @param level The log level threshold to set.
     */
    void setFileThreshold(LogLevel level)
    {
        fileThreshold = level;
    }



    /**
     * @brief Enables file logging.
     */
    void enableFileLogging()
    {
        if (!fileLoggingEnabled) {
            std::ostringstream filename;
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            filename << "log_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";
            logFile.open(filename.str(), std::ios::out);
            fileLoggingEnabled = logFile.is_open();
        }
    }



    /**
     * @brief Disables file logging.
     */
    void disableFileLogging()
    {
        if (logFile.is_open()) {
            logFile.close();
        }
        fileLoggingEnabled = false;
    }
};



/**
 * @brief Global instance.
 */

inline std::shared_ptr<LogBuffer> log_local = std::make_shared<LogBuffer>();



/**
 * @brief Gets the global log buffer instance.
 * @return The global log buffer instance.
 */

inline std::shared_ptr<LogBuffer> getLogger()
{
    return log_local;
}



/**
 * @brief Sets the global log buffer instance.
 * @param logger The log buffer instance to set.
 */

inline void setLogger(std::shared_ptr<LogBuffer> logger)
{
    log_local = logger;
}


/** --------------------------------  Macros ----------------------------------------------- */

#define LOG_STRING(TEXT)   log_local->append(TEXT);                                     /** @brief Macro for logging a string message.*/
#define LOG_PTR(PTR)       log_local->append(PTR);                                      /** @brief Macro for logging a pointer.*/
#define LOG_BOOL(V)        log_local->append(static_cast<bool>(V));                     /** @brief Macro for logging a boolean value.*/
#define LOG_UINT8(V)       log_local->append(static_cast<uint8_t>(V));                  /** @brief Macro for logging a uint8_t value.*/
#define LOG_UINT16(V)      log_local->append(static_cast<uint16_t>(V));                 /** @brief Macro for logging a uint16_t value.*/
#define LOG_UINT32(V)      log_local->append(static_cast<uint32_t>(V));                 /** @brief Macro for logging a uint32_t value.*/
#define LOG_UINT64(V)      log_local->append(static_cast<uint64_t>(V));                 /** @brief Macro for logging a uint64_t value.*/
#define LOG_INT8(V)        log_local->append(static_cast<int8_t>(V));                   /** @brief Macro for logging an int8_t value.*/
#define LOG_INT16(V)       log_local->append(static_cast<int16_t>(V));                  /** @brief Macro for logging an int16_t value.*/
#define LOG_INT32(V)       log_local->append(static_cast<int32_t>(V));                  /** @brief Macro for logging an int32_t value.*/
#define LOG_INT64(V)       log_local->append(static_cast<int64_t>(V));                  /** @brief Macro for logging an int64_t value.*/
#define LOG_INT(V)         log_local->append(static_cast<int>(V));                      /** @brief Macro for logging an int value. */
#define LOG_FLOAT(V)       log_local->append(static_cast<float>(V));                    /** @brief Macro for logging a float value.*/
#define LOG_DOUBLE(V)      log_local->append(static_cast<double>(V));                   /** @brief Macro for logging a double value.*/
#define LOG_HEX8(V)        log_local->appendHex(static_cast<uint8_t>(V));               /** @brief Macro for logging a uint8_t value in hexadecimal format.*/
#define LOG_HEX16(V)       log_local->appendHex(static_cast<uint16_t>(V));              /** @brief Macro for logging a uint16_t value in hexadecimal format.*/
#define LOG_HEX32(V)       log_local->appendHex(static_cast<uint32_t>(V));              /** @brief Macro for logging a uint32_t value in hexadecimal format */
#define LOG_HEX64(V)       log_local->appendHex(static_cast<uint64_t>(V));              /** @brief Macro for logging a uint64_t value in hexadecimal format */



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
                        log_local->useColors = ENABLE_COLORS; \
                        log_local->includeDate = INCLUDE_DATE; \
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
