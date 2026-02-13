#ifndef ICOMMDRIVER_HPP
#define ICOMMDRIVER_HPP

#include <span>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>

/**
 * @brief Class declaration
 */
class ICommDriver
{

    public:

        enum class Status : int32_t {
            SUCCESS = 0,
            INVALID_PARAM = -1,
            PORT_ACCESS = -2,
            READ_ERROR = -3,
            WRITE_ERROR = -4,
            READ_TIMEOUT = -5,
            WRITE_TIMEOUT = -6,
            OUT_OF_MEMORY = -7,
            BUFFER_OVERFLOW = -8,
            FLUSH_FAILED = -9,
            RETVAL_NOT_SET = -10
        };

        /**
         * @brief Read operation mode
         */
        enum class ReadMode {
            Exact,           ///< Read exact number of bytes (fill buffer)
            UntilDelimiter,  ///< Read until delimiter character found
            UntilToken       ///< Read until token sequence found
        };

        /**
         * @brief Options for configuring read operations
         */
        struct ReadOptions {
            ReadMode mode = ReadMode::Exact;           ///< Operation mode
            uint8_t delimiter = '\n';                  ///< Delimiter for UntilDelimiter mode
            std::span<const uint8_t> token = {};       ///< Token for UntilToken mode
            bool use_buffer = true;                    ///< Enable internal buffering for token search
        };

        /**
         * @brief Result of a read operation
         */
        struct ReadResult {
            Status status = Status::RETVAL_NOT_SET;    ///< Operation status
            size_t bytes_read = 0;                     ///< Number of bytes actually read
            bool found_terminator = false;             ///< True if delimiter/token was found
        };

        /**
         * @brief Result of a write operation
         */
        struct WriteResult {
            Status status = Status::RETVAL_NOT_SET;    ///< Operation status
            size_t bytes_written = 0;                  ///< Number of bytes actually written
        };

        virtual ~ICommDriver() = default;

        /**
         * @brief Check if the communication port is open
         * @return true if port is open and ready for operations
         */
        virtual bool is_open() const = 0;

        /**
         * @brief Unified read interface supporting multiple operation modes
         * 
         * @param u32ReadTimeout Timeout in milliseconds (0 = use default)
         * @param buffer Buffer to read data into
         * @param options Read operation configuration
         * @return ReadResult containing status, bytes read, and terminator found flag
         * 
         * @details
         * - ReadMode::Exact: Reads up to buffer.size() bytes
         * - ReadMode::UntilDelimiter: Reads until delimiter is found, null-terminates
         * - ReadMode::UntilToken: Searches for token sequence using KMP algorithm
         */
        virtual ReadResult tout_read(uint32_t u32ReadTimeout, 
                               std::span<uint8_t> buffer, 
                               const ReadOptions& options) const = 0;

        /**
         * @brief Unified write interface
         * 
         * @param u32WriteTimeout Timeout in milliseconds (0 = use default)
         * @param buffer Data to write
         * @return WriteResult containing status and bytes written
         */
        virtual WriteResult tout_write(uint32_t u32WriteTimeout, 
                                 std::span<const uint8_t> buffer) const = 0;

        /**
         * @brief Convert Status enum to human-readable string
         * @param code Status code to convert
         * @return String representation of the status
         */
        static std::string to_string(Status code)
        {
            switch (code)
            {
                case Status::SUCCESS:           return "SUCCESS";
                case Status::INVALID_PARAM:     return "INVALID_PARAM";
                case Status::PORT_ACCESS:       return "PORT_ACCESS";
                case Status::READ_ERROR:        return "READ_ERROR";
                case Status::WRITE_ERROR:       return "WRITE_ERROR";
                case Status::READ_TIMEOUT:      return "READ_TIMEOUT";
                case Status::WRITE_TIMEOUT:     return "WRITE_TIMEOUT";
                case Status::OUT_OF_MEMORY:     return "OUT_OF_MEMORY";
                case Status::BUFFER_OVERFLOW:   return "BUFFER_OVERFLOW";
                case Status::FLUSH_FAILED:      return "FLUSH_FAILED";
                case Status::RETVAL_NOT_SET:    return "RETVAL_NOT_SET";
                default:                        return "UNKNOWN_ERROR";
            }
        };
 };

/**
 * @brief Function pointer type for write/send operations
 * @tparam TDriver The concrete driver type
 * @param timeout Timeout in milliseconds
 * @param buffer Data to send
 * @param driver Shared pointer to the driver instance
 * @return WriteResult containing status and bytes written
 */
template<typename TDriver>
using PFSEND = std::function<typename ICommDriver::WriteResult(
    uint32_t timeout,
    std::span<const uint8_t> buffer,
    std::shared_ptr<const TDriver> driver)>;

/**
 * @brief Function pointer type for read/receive operations
 * @tparam TDriver The concrete driver type
 * @param timeout Timeout in milliseconds
 * @param buffer Buffer to receive data
 * @param options Read operation configuration
 * @param driver Shared pointer to the driver instance
 * @return ReadResult containing status, bytes read, and terminator found flag
 */
template<typename TDriver>
using PFRECV = std::function<typename ICommDriver::ReadResult(
    uint32_t timeout,
    std::span<uint8_t> buffer,
    const typename ICommDriver::ReadOptions& options,
    std::shared_ptr<const TDriver> driver)>;

/**
 * @brief Nested template aliase to PFSEND
 */
template<typename TDriver>
using SendFunc = PFSEND<TDriver>;

/**
 * @brief Nested template aliase to PFRECV
 */
template<typename TDriver>
using RecvFunc = PFRECV<TDriver>;

#endif // ICOMMDRIVER_HPP