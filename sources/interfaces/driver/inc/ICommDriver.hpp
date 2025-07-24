#ifndef ICommDriver_H
#define ICOMMDRIVER_HPP

#include <string>
#include <cstdint>

class ICommDriver
{

    public:

        enum class Status : int32_t {
            SUCCESS = 0,
            INVALID_PARAM = -1,
            PORT_ACCESS = -2,
            READ_TIMEOUT = -3,
            WRITE_TIMEOUT = -4,
            OUT_OF_MEMORY = -5,
            RETVAL_NOT_SET = -6
        };

        virtual ~ICommDriver() = default;

        virtual bool is_open() = 0;
        virtual Status timeout_read(uint32_t timeout, char* buffer, size_t size, size_t* bytesRead) = 0;
        virtual Status timeout_readline(uint32_t timeout, char* buffer, size_t bufferSize) = 0;
        virtual Status timeout_write(uint32_t timeout, const char* buffer, size_t size) = 0;
        virtual Status timeout_wait_for_token(uint32_t timeout, const char* token) = 0;
        virtual Status timeout_wait_for_token_buffer(uint32_t timeout, const char* token, uint32_t tokenLength) = 0;

        static std::string to_string(Status code)
        {
            switch (code)
            {
                case Status::SUCCESS: return "SUCCESS";
                case Status::INVALID_PARAM: return "INVALID_PARAM";
                case Status::PORT_ACCESS: return "PORT_ACCESS";
                case Status::READ_TIMEOUT: return "READ_TIMEOUT";
                case Status::WRITE_TIMEOUT: return "WRITE_TIMEOUT";
                case Status::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
                case Status::RETVAL_NOT_SET: return "RETVAL_NOT_SET";
                default: return "UNKNOWN_ERROR";
            }
        };
};

#endif // ICOMMDRIVER_HPP
