#include "test_uart_common.hpp"
#include "uUart.hpp"
#include "uHexdump.hpp"

#include <cstring>
#include <span>
#include <array>

int main(int argc, char* argv[])
{
    if (2 != argc) {
        std::cerr << argv[0] << ": expected one argument: port" << std::endl;
        return 1;
    }

    if (false == fileExistsAndNotEmpty(TEST_FILENAME)) {
        std::cout << argv[0] << ": file does not exist or is empty: " << TEST_FILENAME << std::endl;
        return 1;
    }

    // Load responses from file
    auto responses = loadResponses(TEST_FILENAME);

    std::string port = argv[1];
    const uint32_t baudRate = 9600;

    // Open the serial port
    UART uart(port, baudRate);

    if (false == uart.is_open()) {
        std::cerr << argv[0] << ": failed to open port " << port << std::endl;
        return 1;
    }

    std::cout << argv[0] << ": sending messages from " << TEST_FILENAME << " on " << port << "..." << std::endl;

    std::array<uint8_t, UART::UART_MAX_BUFLENGTH> buffer = {0};

    for (const auto& pair : responses)
    {
        const std::string& message = pair.first;
        const std::string& expectedResponse = pair.second;

        // Send the message
        std::span<const uint8_t> writeSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size());
        if (UART::Status::SUCCESS == uart.timeout_write(UART::UART_WRITE_DEFAULT_TIMEOUT, writeSpan))
        {
            std::cout << argv[0] << ": sent [" << message << "] expecting [" << expectedResponse << "]" << std::endl;

            size_t szSizeRead = 0;
            std::span<uint8_t> readSpan(buffer.data(), buffer.size());

            if (UART::Status::SUCCESS == uart.timeout_read(UART::UART_READ_DEFAULT_TIMEOUT, readSpan, szSizeRead))
            {
                std::cout << argv[0] << ": received ok" << std::endl;
                hexutils::HexDump2(buffer.data(), szSizeRead);

                std::string received(reinterpret_cast<char*>(buffer.data()), szSizeRead);
                std::cout << argv[0] << ": received [" << received << "]" << std::endl;

                if (received == expectedResponse)
                {
                    std::cout << argv[0] << ": response matches expected: " << expectedResponse << std::endl;
                }
                else
                {
                    std::cout << argv[0] << ": unexpected response: " << received << std::endl;
                }
            }
            else
            {
                std::cout << argv[0] << ": no response received for message: " << message << std::endl;
            }
        }
    }

    return 0;
}
