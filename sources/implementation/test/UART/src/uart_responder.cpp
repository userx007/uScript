
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

    std::cout << argv[0] << ": listening on " << port << "..." << std::endl;

    char buffer[UART::UART_MAX_BUFLENGTH] = {0};

    while (true)
    {
        size_t szSizeRead = 0;
        memset(buffer, 0, sizeof(buffer));

        if (UART::Status::SUCCESS == uart.timeout_read(UART::UART_READ_DEFAULT_TIMEOUT, buffer, sizeof(buffer), &szSizeRead))
        {
            std::cout << argv[0] << ": received:" << std::endl;
            hexutils::HexDump2(reinterpret_cast<const uint8_t*>(buffer), szSizeRead);

            std::string received(buffer);

            if (std::string("EXIT") == received)
            {
                std::cout << argv[0] << " : exiting..." << std::endl;
                break;
            }

            auto it = responses.find(received);
            if (it != responses.end())
            {
                std::string fullMessage = it->second;

                std::cout << argv[0] << ": sending:" << std::endl;
                hexutils::HexDump2(reinterpret_cast<const uint8_t*>(fullMessage.c_str()), fullMessage.length());

                if (UART::Status::SUCCESS == uart.timeout_write(UART::UART_WRITE_DEFAULT_TIMEOUT, fullMessage.c_str(), fullMessage.length()))
                {
                    std::cout << argv[0] << ": sent ok" << std::endl;
                }
            }
            else
            {
                std::cout << argv[0] << ": unexpected response: " << received << std::endl;
            }
        }
    }

    return 0;
}
