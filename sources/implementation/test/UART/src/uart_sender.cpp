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

    std::cout << "Sending messages from responses.txt on " << port << "..." << std::endl;

    char buffer[UART::UART_MAX_BUFLENGTH] = {0};

    for (const auto& pair : responses) {
        const std::string& message = pair.first;
        const std::string& expectedResponse = pair.second;

        // Send the message
        uart.timeout_write(UART::UART_WRITE_DEFAULT_TIMEOUT, message.c_str(), message.length());
        std::cout << "Sent: " << message << std::endl;

        // Wait for the response
        memset(buffer, 0, sizeof(buffer));
        auto status = uart.timeout_readline(UART::UART_READ_DEFAULT_TIMEOUT, buffer, sizeof(buffer));
        if (status == UART::Status::SUCCESS) {
            std::string received(buffer);
            std::cout << "Received: " << received << std::endl;

            if (received == expectedResponse) {
                std::cout << "Response matches expected: " << expectedResponse << std::endl;
            } else {
                std::cout << "Unexpected response: " << received << std::endl;
            }
        } else {
            std::cout << "No response received for message: " << message << std::endl;
        }
    }

    return 0;
}
