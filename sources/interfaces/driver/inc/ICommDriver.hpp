#ifndef ICOMMDRIVER_HPP
#define ICOMMDRIVER_HPP

#include <span>
#include <string>
#include <cstdint>

class ICommDriver
{

    public:

        enum class Status : int32_t {
            SUCCESS = 0,
            INVALID_PARAM = -1,
            PORT_ACCESS = -2,
            READ_ERROR = -3,
            WRITE_ERROR = -4,
            FLUSH_FAILED = -5,
            READ_TIMEOUT = -6,
            WRITE_TIMEOUT = -7,
            OUT_OF_MEMORY = -8,
            BUFFER_OVERFLOW = -9,
            RETVAL_NOT_SET = -10
        };

        virtual ~ICommDriver() = default;

        virtual bool is_open() const = 0;

        virtual Status timeout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t* pBytesRead) const = 0;
        virtual Status timeout_readline(uint32_t u32ReadTimeout, std::span<uint8_t> buffer) const = 0;
        virtual Status timeout_write(uint32_t u32WriteTimeouts, std::span<const uint8_t> buffer) const = 0;
        virtual Status timeout_wait_for_token(uint32_t u32ReadTimeout, std::span<const uint8_t> token) const = 0;
        virtual Status timeout_wait_for_token_buffer(uint32_t u32ReadTimeout, std::span<const uint8_t> token, size_t szTokenLength) const = 0;

        static std::string to_string(Status code)
        {
            switch (code)
            {
                case Status::SUCCESS:           return "SUCCESS";
                case Status::INVALID_PARAM:     return "INVALID_PARAM";
                case Status::PORT_ACCESS:       return "PORT_ACCESS";
                case Status::READ_TIMEOUT:      return "READ_TIMEOUT";
                case Status::WRITE_TIMEOUT:     return "WRITE_TIMEOUT";
                case Status::OUT_OF_MEMORY:     return "OUT_OF_MEMORY";
                case Status::RETVAL_NOT_SET:    return "RETVAL_NOT_SET";
                default:                        return "UNKNOWN_ERROR";
            }
        };
};

#endif // ICOMMDRIVER_HPP
