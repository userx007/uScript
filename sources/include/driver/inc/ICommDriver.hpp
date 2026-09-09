#ifndef I_COMM_DRIVER_HPP
#define I_COMM_DRIVER_HPP
#include "ICommDumpProtocol.hpp"   // CommFamily / CommDetails — shared with the GUI's comm-dump wire format

#include <span>
#include <string>
#include <string_view>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>

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
            RETVAL_NOT_SET = -10,
            OPERATION_FAILED = -12,
            PROTOCOL_ERROR = -13,
            NACK = -11
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
         * @brief Describe this driver's identity for the GUI comm-dump panel
         *        (see ICommDumpProtocol.hpp / uGuiNotify.hpp::gui_notify_comm_dump).
         *
         * @param xtra_params Same string a caller would pass to tout_read()/tout_write()
         *                    for this exchange. Empty selects the driver's own static
         *                    identity (its configured port/address/bus — set at
         *                    construction, see each driver's constructor). Non-empty
         *                    means the driver should describe *this specific exchange*:
         *                    e.g. a CAN driver reflects the resolved TX id including any
         *                    override, an I2C driver reflects the addressed slave, an
         *                    inet-style driver reflects the resolved peer. Drivers with no
         *                    per-call addressing concept (UART, a point-to-point stream)
         *                    simply ignore xtra_params and always return their static
         *                    identity.
         *
         * @return CommDetails{ family, label } — label is plain, driver-rendered display
         *         text (e.g. "/dev/ttyUSB0", "192.168.1.5:502", "PCAN-USB ch0 id=0x123"),
         *         truncated safely by commdump_details() if it doesn't fit k_labelSize.
         *
         * Cheap to call: implementations should do no I/O, just format already-known
         * state. Callers are expected to cache the xtra_params-empty result rather than
         * recomputing it on every send/receive (see CommScriptCommandInterpreter).
         */
        virtual CommDetails describeConnection(std::string_view xtra_params = {}) const = 0;

        /**
         * @brief Unified read interface supporting multiple operation modes
         *
         * @param u32ReadTimeout Timeout in milliseconds (0 = block indefinitely / infinite timeout)
         * @param buffer         Buffer to read data into
         * @param options        Read operation configuration
         * @param xtra_params     Optional driver-specific channel / address identifier.
         *                       When non-empty the driver interprets this string as an
         *                       addressing hint that overrides any previously configured
         *                       default (e.g. a CAN RX filter ID, an I2C slave address,
         *                       a UART port name, …).  An empty string selects the
         *                       driver's own default.  The format is driver-defined;
         *                       callers that do not need per-call addressing may omit
         *                       the argument entirely.
         * @param stop_tok       Cooperative cancellation token, appended last (after the
         *                       long-standing xtra_params) so every existing positional
         *                       call site (`tout_read(t, buf, options, xtra_params)`)
         *                       keeps compiling unchanged. Drivers that support cancellation
         *                       poll stop_tok.stop_requested() while blocked and return
         *                       Status::READ_TIMEOUT promptly once it fires — see individual
         *                       driver implementations for exactly how each blocking primitive
         *                       is made to observe it (bounded poll() slices, condition_variable_any,
         *                       stop_callback-driven event signalling, etc). A default-constructed
         *                       token (stop_possible() == false) behaves exactly as before this
         *                       parameter was added: the call can only end via data, error, or the
         *                       timeout itself.
         * @return ReadResult containing status, bytes read, and terminator found flag
         *
         * @details
         * - ReadMode::Exact:          Reads up to buffer.size() bytes
         * - ReadMode::UntilDelimiter: Reads until delimiter is found, null-terminates
         * - ReadMode::UntilToken:     Searches for token sequence using KMP algorithm
         */
        virtual ReadResult tout_read(uint32_t u32ReadTimeout,
                               std::span<uint8_t> buffer,
                               const ReadOptions& options,
                               std::string_view xtra_params = {},
                               std::stop_token stop_tok = {}) const = 0;

        /**
         * @brief Unified write interface
         *
         * @param u32WriteTimeout Timeout in milliseconds (0 = block indefinitely / infinite timeout)
         * @param buffer          Data to write
         * @param xtra_params      Optional driver-specific channel / address identifier.
         *                        When non-empty the driver interprets this string as an
         *                        addressing hint that overrides any previously configured
         *                        default (e.g. a CAN TX ID, an I2C slave address, …).
         *                        An empty string selects the driver's own default.
         *                        The format is driver-defined; callers that do not need
         *                        per-call addressing may omit the argument entirely.
         * @param stop_tok        Cooperative cancellation token, appended last for the same
         *                        call-site-compatibility reason as tout_read() — see its
         *                        @p stop_tok doc for the full contract. A default-constructed
         *                        token disables cancellation and preserves pre-existing behaviour.
         * @return WriteResult containing status and bytes written
         */
        virtual WriteResult tout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer,
                                 std::string_view xtra_params = {},
                                 std::stop_token stop_tok = {}) const = 0;

        /**
         * @brief Reset the driver state (e.g., flush buffers, reset filters).
         *        This is non-const because it may modify internal state.
         */
        virtual void reset() {}

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
                case Status::OPERATION_FAILED:  return "OPERATION_FAILED";
                case Status::PROTOCOL_ERROR:    return "PROTOCOL_ERROR";
                case Status::NACK:              return "NACK";
                default:                        return "UNKNOWN_ERROR";
            }
        };
 };

/**
 * @brief Function pointer type for write/send operations
 * @tparam TDriver The concrete driver type
 * @param timeout    Timeout in milliseconds
 * @param buffer     Data to send
 * @param driver     Shared pointer to the driver instance
 * @param xtra_params Optional channel / address identifier (driver-defined format)
 * @param stop_tok   Cooperative cancellation token, forwarded verbatim from whatever
 *                   ultimately called ICommDriver::tout_write() — see its own
 *                   @p stop_tok doc for the contract. A default-constructed token
 *                   disables cancellation and preserves pre-existing behaviour.
 * @return WriteResult containing status and bytes written
 */
template<typename TDriver>
using PFSEND = std::function<typename ICommDriver::WriteResult(
    uint32_t timeout,
    std::span<const uint8_t> buffer,
    std::shared_ptr<const TDriver> driver,
    std::string_view xtra_params,
    std::stop_token stop_tok)>;

/**
 * @brief Function pointer type for read/receive operations
 * @tparam TDriver The concrete driver type
 * @param timeout    Timeout in milliseconds
 * @param buffer     Buffer to receive data
 * @param options    Read operation configuration
 * @param driver     Shared pointer to the driver instance
 * @param xtra_params Optional channel / address identifier (driver-defined format)
 * @param stop_tok   Cooperative cancellation token — see PFSEND's @p stop_tok for
 *                   the contract; a default-constructed token disables cancellation.
 * @return ReadResult containing status, bytes read, and terminator found flag
 */
template<typename TDriver>
using PFRECV = std::function<typename ICommDriver::ReadResult(
    uint32_t timeout,
    std::span<uint8_t> buffer,
    const typename ICommDriver::ReadOptions& options,
    std::shared_ptr<const TDriver> driver,
    std::string_view xtra_params,
    std::stop_token stop_tok)>;

/**
 * @brief Nested template alias to PFSEND
 */
template<typename TDriver>
using SendFunction = PFSEND<TDriver>;

/**
 * @brief Nested template alias to PFRECV
 */
template<typename TDriver>
using RecvFunction = PFRECV<TDriver>;

#endif // I_COMM_DRIVER_HPP