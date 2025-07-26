#include "uart_common.hpp"

#include "uUart.hpp"
#include "uHexdump.hpp"

#include <cstring>

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

    char buffer[UART::UART_MAX_BUFLENGTH] = {0};

    for (const auto& pair : responses)
    {
        const std::string& message = pair.first;
        const std::string& expectedResponse = pair.second;

        // Wait for the response
        size_t szSizeRead = 0;
        memset(buffer, 0, sizeof(buffer));

        // Send the message
        if (UART::Status::SUCCESS == uart.timeout_write(UART::UART_WRITE_DEFAULT_TIMEOUT, message.c_str(), message.length()))
        {
            std::cout << argv[0] << ": sent [" << message << "]" << " expecting [" << expectedResponse << "]" << std::endl;

            if (UART::Status::SUCCESS == uart.timeout_read(UART::UART_READ_DEFAULT_TIMEOUT, buffer, sizeof(buffer), &szSizeRead))
            {
                std::cout << argv[0] << ": received ok" << std::endl;
                hexutils::HexDump2(reinterpret_cast<const uint8_t*>(buffer), szSizeRead);

                std::string received(buffer);
                std::cout << argv[0] << ": received [" << received << "]" <<std::endl;

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
