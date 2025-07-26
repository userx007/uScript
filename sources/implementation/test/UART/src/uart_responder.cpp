
#include "uart_common.hpp"

#include "uUart.hpp"
#include "uHexdump.hpp"

#include <cstring>


int main(int argc, char* argv[])
{
    if (2 != argc) {
        std::cerr << argv[0] << " Expected one argument: port" << std::endl;
        return 1;
    }

    if (false == fileExistsAndNotEmpty(TEST_FILENAME)) {
        std::cout << "File does not exist or is empty: " << TEST_FILENAME << std::endl;
        return 1;
    }

    // Load responses from file
    auto responses = loadResponses(TEST_FILENAME);

    std::string port = argv[1];
    const uint32_t baudRate = 9600;

    // Open the serial port
    UART uart(port, baudRate);

    if (false == uart.is_open()) {
        std::cerr << "Failed to open port " << port << std::endl;
        return 1;
    }

    std::cout << "Listening on " << port << "..." << std::endl;

    char buffer[UART::UART_MAX_BUFLENGTH] = {0};

    while (true)
    {
        size_t szSizeRead = 0;
        memset(buffer, 0, sizeof(buffer));

        if (UART::Status::SUCCESS == uart.timeout_read(UART::UART_READ_DEFAULT_TIMEOUT, buffer, sizeof(buffer), &szSizeRead))
        {
            std::cout << "Received:" << std::endl;
            hexutils::HexDump2(reinterpret_cast<const uint8_t*>(buffer), szSizeRead);

            std::string received(buffer);

            if(received == std::string("EXIT"))
            {
                std::cout << "Exiting..." << std::endl;
                break;
            }

            auto it = responses.find(received);
            if (it != responses.end())
            {
                std::string fullMessage = it->second;

                std::cout << "Sending:" << std::endl;
                hexutils::HexDump2(reinterpret_cast<const uint8_t*>(fullMessage.c_str()), fullMessage.length());

                if (UART::Status::SUCCESS == uart.timeout_write(UART::UART_WRITE_DEFAULT_TIMEOUT, fullMessage.c_str(), fullMessage.length()))
                {
                    std::cout << "Sent ok" << std::endl;
                }
            }
            else
            {
                std::cout << "Unexpected response: " << received << std::endl;
            }
        }
    }

    return 0;
}
