#ifndef COMMSCRIPTCOMMANDINTERPRETER_HPP
#define COMMSCRIPTCOMMANDINTERPRETER_HPP

#include "SharedSettings.hpp"
#include "ICommDriver.hpp"
#include "IScriptItemInterpreter.hpp"
#include "CommScriptDataTypes.hpp"

#include "uLogger.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"
#include "uFile.hpp"

#include <regex>
#include <string>
#include <memory>
#include <string_view>
#include <filesystem>

/////////////////////////////////////////////////////////////////////////////////
//                             LOG DEFINITIONS                                 //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "SCRIPT_INTERP:"
#define LOG_HDR    LOG_STRING(LT_HDR); LOG_STRING(__FUNCTION__)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Interprets and executes communication commands from scripts
 * 
 * This class takes CommCommand objects (parsed script lines) and executes them
 * using any ICommDriver-derived driver. It handles:
 * - Send/receive operations in specified order
 * - Multiple data formats (hex, string, file, token, etc.)
 * - Pattern matching and validation
 * - File transfers in chunks
 * 
 * @tparam TDriver The concrete driver type (must derive from ICommDriver)
 */
template <typename TDriver>
class CommScriptCommandInterpreter : public IScriptItemInterpreter<CommCommand, TDriver>
{
public:

    /**
     * @brief Constructor
     * @param driver Shared pointer to the communication driver
     * @param maxRecvSize Maximum buffer size for receive operations
     * @param defaultTimeout Default timeout in milliseconds (0 = use driver default)
     */
    explicit CommScriptCommandInterpreter(
        std::shared_ptr<const TDriver> driver,
        size_t maxRecvSize = 4096,
        uint32_t defaultTimeout = 5000)
        : m_driver(driver)
        , m_maxRecvSize(maxRecvSize)
        , m_defaultTimeout(defaultTimeout)
    {
        static_assert(std::is_base_of<ICommDriver, TDriver>::value,
                     "TDriver must derive from ICommDriver");
    }

    /**
     * @brief Interpret and execute a single command
     * @param command The parsed command to execute
     * @return true if execution successful, false otherwise
     */
    bool interpretItem(const CommCommand& command) override
    {
        if (!m_driver || !m_driver->is_open()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Driver not available or port not open"));
            return false;
        }

        LOG_PRINT(LOG_DEBUG, LOG_HDR; 
                  LOG_STRING("Executing:"); 
                  LOG_STRING(getDirectionName(command.direction));
                  LOG_STRING("["); LOG_STRING(command.values.first); 
                  LOG_STRING("|"); LOG_STRING(command.values.second);
                  LOG_STRING("] => ["); 
                  LOG_STRING(getTokenTypeName(command.tokens.first));
                  LOG_STRING(":"); 
                  LOG_STRING(getTokenTypeName(command.tokens.second));
                  LOG_STRING("]"));

        bool result = false;

        // Execute based on direction
        if (command.direction == CommCommandDirection::SEND_RECV) {
            // Send first, then receive
            result = executeSend(command.values.first, command.tokens.first);
            if (result && command.tokens.second != CommCommandTokenType::EMPTY) {
                result = executeReceive(command.values.second, command.tokens.second);
            }
        } else if (command.direction == CommCommandDirection::RECV_SEND) {
            // Receive first, then send
            result = executeReceive(command.values.first, command.tokens.first);
            if (result && command.tokens.second != CommCommandTokenType::EMPTY) {
                result = executeSend(command.values.second, command.tokens.second);
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid command direction"));
            return false;
        }

        if (!result) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Command execution failed"));
        }

        return result;
    }

    /**
     * @brief Get last received data
     */
    const std::vector<uint8_t>& getLastReceived() const
    {
        return m_lastReceived;
    }

    /**
     * @brief Set default timeout for operations
     */
    void setDefaultTimeout(uint32_t timeout)
    {
        m_defaultTimeout = timeout;
    }

    /**
     * @brief Set maximum receive buffer size
     */
    void setMaxRecvSize(size_t size)
    {
        m_maxRecvSize = size;
    }

private:

    std::shared_ptr<const TDriver> m_driver;
    size_t m_maxRecvSize;
    uint32_t m_defaultTimeout;
    std::vector<uint8_t> m_lastReceived;

    /**
     * @brief Execute a send operation
     * @param value The data value to send (string representation)
     * @param type The token type indicating how to interpret the value
     * @return true if send successful, false otherwise
     */
    bool executeSend(const std::string& value, CommCommandTokenType type)
    {
        // Empty token means no send operation
        if (type == CommCommandTokenType::EMPTY) {
            return true;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("Send:"); LOG_STRING(value); 
                  LOG_STRING("Type:"); LOG_STRING(getTokenTypeName(type)));

        // Handle file send specially
        if (type == CommCommandTokenType::FILENAME) {
            return sendFile(value);
        }

        // Convert value to bytes based on type
        std::vector<uint8_t> data;
        if (!convertToData(value, type, data)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Failed to convert data for send"));
            return false;
        }

        // Send the data
        auto result = m_driver->tout_write(m_defaultTimeout, std::span<const uint8_t>(data));
        
        if (result.status != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Write failed:"); 
                      LOG_STRING(ICommDriver::to_string(result.status));
                      LOG_STRING("Bytes written:"); LOG_SIZET(result.bytes_written));
            return false;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("Sent:"); LOG_SIZET(result.bytes_written); 
                  LOG_STRING("bytes"));
        return true;
    }

    /**
     * @brief Execute a receive operation
     * @param value The expected data value or pattern (string representation)
     * @param type The token type indicating how to interpret the value
     * @return true if receive successful and data matches expectation, false otherwise
     */
    bool executeReceive(const std::string& value, CommCommandTokenType type)
    {
        // Empty token means no receive operation
        if (type == CommCommandTokenType::EMPTY) {
            return true;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("Recv:"); LOG_STRING(value); 
                  LOG_STRING("Type:"); LOG_STRING(getTokenTypeName(type)));

        switch (type) {
            case CommCommandTokenType::REGEX:
                return receiveAndMatchRegex(value);

            case CommCommandTokenType::TOKEN:
                return receiveUntilToken(value);

            case CommCommandTokenType::SIZE:
                return receiveExactSize(value);

            case CommCommandTokenType::LINE:
                return receiveUntilDelimiter('\n', value);

            case CommCommandTokenType::FILENAME:
                return receiveToFile(value);

            case CommCommandTokenType::HEXSTREAM:
            case CommCommandTokenType::STRING_DELIMITED:
            case CommCommandTokenType::STRING_DELIMITED_EMPTY:
            case CommCommandTokenType::STRING_RAW:
                return receiveAndCompare(value, type);

            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("Unsupported receive token type"));
                return false;
        }
    }

    /**
     * @brief Receive data and match against regex pattern
     */
    bool receiveAndMatchRegex(const std::string& pattern)
    {
        // Read exact bytes from driver
        m_lastReceived.resize(m_maxRecvSize);
        ICommDriver::ReadOptions options;
        options.mode = ICommDriver::ReadMode::Exact;

        auto result = m_driver->tout_read(m_defaultTimeout, 
                                          std::span<uint8_t>(m_lastReceived), 
                                          options);

        if (result.status != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Read failed:"); 
                      LOG_STRING(ICommDriver::to_string(result.status)));
            return false;
        }

        // Resize to actual bytes read
        m_lastReceived.resize(result.bytes_read);

        // Convert to string for regex matching
        std::string received(m_lastReceived.begin(), m_lastReceived.end());

        // Match against pattern
        try {
            std::regex re(pattern);
            bool matched = std::regex_match(received, re);
            
            if (!matched) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("Regex match failed. Received:"); 
                          LOG_STRING(received));
            }
            
            return matched;
        } catch (const std::regex_error& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Invalid regex pattern:"); 
                      LOG_STRING(e.what()));
            return false;
        }
    }

    /**
     * @brief Receive data until a specific token is found
     */
    bool receiveUntilToken(const std::string& tokenStr)
    {
        // Convert token string to bytes
        std::vector<uint8_t> token;
        if (!convertToData(tokenStr, CommCommandTokenType::TOKEN, token)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Failed to convert token"));
            return false;
        }

        // Setup read options for token search
        m_lastReceived.resize(m_maxRecvSize);
        ICommDriver::ReadOptions options;
        options.mode = ICommDriver::ReadMode::UntilToken;
        options.token = std::span<const uint8_t>(token);
        options.use_buffer = true;

        auto result = m_driver->tout_read(m_defaultTimeout, 
                                          std::span<uint8_t>(m_lastReceived), 
                                          options);

        if (result.status != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Token search failed:"); 
                      LOG_STRING(ICommDriver::to_string(result.status)));
            return false;
        }

        if (!result.found_terminator) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Token not found within timeout"));
            return false;
        }

        m_lastReceived.resize(result.bytes_read);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("Token found after"); LOG_SIZET(result.bytes_read); 
                  LOG_STRING("bytes"));
        return true;
    }

    /**
     * @brief Receive exact number of bytes specified as size
     */
    bool receiveExactSize(const std::string& sizeStr)
    {
        size_t expectedSize = 0;
        if (!numeric::str2sizet(sizeStr, expectedSize)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Invalid size value:"); LOG_STRING(sizeStr));
            return false;
        }

        if (expectedSize == 0 || expectedSize > m_maxRecvSize) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Size out of range:"); LOG_SIZET(expectedSize));
            return false;
        }

        m_lastReceived.resize(expectedSize);
        ICommDriver::ReadOptions options;
        options.mode = ICommDriver::ReadMode::Exact;

        auto result = m_driver->tout_read(m_defaultTimeout, 
                                          std::span<uint8_t>(m_lastReceived), 
                                          options);

        if (result.status != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Read failed:"); 
                      LOG_STRING(ICommDriver::to_string(result.status)));
            return false;
        }

        m_lastReceived.resize(result.bytes_read);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("Received:"); LOG_SIZET(result.bytes_read); 
                  LOG_STRING("bytes"));
        return (result.bytes_read == expectedSize);
    }

    /**
     * @brief Receive data until delimiter character
     */
    bool receiveUntilDelimiter(uint8_t delimiter, const std::string& expectedStr)
    {
        m_lastReceived.resize(m_maxRecvSize);
        ICommDriver::ReadOptions options;
        options.mode = ICommDriver::ReadMode::UntilDelimiter;
        options.delimiter = delimiter;

        auto result = m_driver->tout_read(m_defaultTimeout, 
                                          std::span<uint8_t>(m_lastReceived), 
                                          options);

        if (result.status != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Read until delimiter failed:"); 
                      LOG_STRING(ICommDriver::to_string(result.status)));
            return false;
        }

        m_lastReceived.resize(result.bytes_read);

        // If no expected string provided, just return success
        if (expectedStr.empty()) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                      LOG_STRING("Received line:"); LOG_SIZET(result.bytes_read); 
                      LOG_STRING("bytes"));
            return true;
        }

        // Compare with expected (add newline to expected for comparison)
        std::vector<uint8_t> expected;
        if (!convertToData(expectedStr, CommCommandTokenType::LINE, expected)) {
            return false;
        }

        // Note: m_lastReceived won't have the delimiter, but expected will have '\n'
        // So compare without the trailing newline from expected
        if (expected.size() > 0 && expected.back() == '\n') {
            expected.pop_back();
        }

        bool matched = std::equal(m_lastReceived.begin(), m_lastReceived.end(), 
                                 expected.begin(), expected.end());

        if (!matched) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Line content mismatch"));
        }

        return matched;
    }

    /**
     * @brief Receive data and compare with expected value
     */
    bool receiveAndCompare(const std::string& expectedStr, CommCommandTokenType type)
    {
        // First receive the data
        m_lastReceived.resize(m_maxRecvSize);
        ICommDriver::ReadOptions options;
        options.mode = ICommDriver::ReadMode::Exact;

        auto result = m_driver->tout_read(m_defaultTimeout, 
                                          std::span<uint8_t>(m_lastReceived), 
                                          options);

        if (result.status != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Read failed:"); 
                      LOG_STRING(ICommDriver::to_string(result.status)));
            return false;
        }

        m_lastReceived.resize(result.bytes_read);

        // Convert expected string to bytes
        std::vector<uint8_t> expected;
        if (!convertToData(expectedStr, type, expected)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Failed to convert expected data"));
            return false;
        }

        // Compare
        bool matched = (result.bytes_read == expected.size()) &&
                      std::equal(m_lastReceived.begin(), m_lastReceived.end(),
                                expected.begin(), expected.end());

        if (!matched) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Data mismatch. Expected:"); LOG_SIZET(expected.size());
                      LOG_STRING("Received:"); LOG_SIZET(result.bytes_read));
        }

        return matched;
    }

    /**
     * @brief Send file in chunks
     * Format: "filename" or "filename,chunksize"
     */
    bool sendFile(const std::string& fileSpec)
    {
        // Parse filename and optional chunk size
        std::pair<std::string, std::string> parts;
        ustring::splitAtFirst(fileSpec, CHAR_SEPARATOR_COMMA, parts);

        std::string filepath = parts.first;
        size_t chunkSize = 1024; // Default chunk size

        // Parse chunk size if provided
        if (!parts.second.empty()) {
            if (!numeric::str2sizet(parts.second, chunkSize)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("Invalid chunk size:"); LOG_STRING(parts.second));
                return false;
            }
        }

        // Validate file exists
        if (!std::filesystem::exists(filepath) || 
            !std::filesystem::is_regular_file(filepath)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("File not found:"); LOG_STRING(filepath));
            return false;
        }

        // Get file size
        auto fileSize = std::filesystem::file_size(filepath);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("Sending file:"); LOG_STRING(filepath);
                  LOG_STRING("Size:"); LOG_UINT64(fileSize);
                  LOG_STRING("Chunk:"); LOG_SIZET(chunkSize));

        // Open file
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Failed to open file:"); LOG_STRING(filepath));
            return false;
        }

        // Send file in chunks
        std::vector<uint8_t> chunk(chunkSize);
        size_t totalSent = 0;

        while (file) {
            file.read(reinterpret_cast<char*>(chunk.data()), chunkSize);
            std::streamsize bytesRead = file.gcount();

            if (bytesRead > 0) {
                std::span<const uint8_t> dataSpan(chunk.data(), bytesRead);
                auto result = m_driver->tout_write(m_defaultTimeout, dataSpan);

                if (result.status != ICommDriver::Status::SUCCESS) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; 
                              LOG_STRING("File write failed at offset:"); 
                              LOG_SIZET(totalSent);
                              LOG_STRING("Status:"); 
                              LOG_STRING(ICommDriver::to_string(result.status)));
                    return false;
                }

                totalSent += result.bytes_written;
            }
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("File sent successfully. Total:"); 
                  LOG_SIZET(totalSent); LOG_STRING("bytes"));
        return true;
    }

    /**
     * @brief Receive data to file
     * Format: "filename" or "filename,expected_size" or "filename,expected_size,chunksize"
     */
    bool receiveToFile(const std::string& fileSpec)
    {
        // Parse the file specification
        std::vector<std::string> parts;
        ustring::tokenize(fileSpec, CHAR_SEPARATOR_COMMA, parts);

        if (parts.empty() || parts[0].empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Invalid file specification"));
            return false;
        }

        std::string filepath = parts[0];
        size_t expectedSize = 0;
        size_t chunkSize = 1024;

        // Parse optional expected size
        if (parts.size() > 1 && !parts[1].empty()) {
            if (!numeric::str2sizet(parts[1], expectedSize)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("Invalid expected size:"); LOG_STRING(parts[1]));
                return false;
            }
        }

        // Parse optional chunk size
        if (parts.size() > 2 && !parts[2].empty()) {
            if (!numeric::str2sizet(parts[2], chunkSize)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("Invalid chunk size:"); LOG_STRING(parts[2]));
                return false;
            }
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("Receiving to file:"); LOG_STRING(filepath);
                  LOG_STRING("Expected:"); LOG_SIZET(expectedSize);
                  LOG_STRING("Chunk:"); LOG_SIZET(chunkSize));

        // Open output file
        std::ofstream file(filepath, std::ios::binary);
        if (!file) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Failed to create file:"); LOG_STRING(filepath));
            return false;
        }

        // Receive file in chunks
        std::vector<uint8_t> chunk(chunkSize);
        size_t totalReceived = 0;
        ICommDriver::ReadOptions options;
        options.mode = ICommDriver::ReadMode::Exact;

        while (true) {
            // Determine how many bytes to read in this iteration
            size_t bytesToRead = chunkSize;
            if (expectedSize > 0 && (totalReceived + chunkSize > expectedSize)) {
                bytesToRead = expectedSize - totalReceived;
                if (bytesToRead == 0) break; // All expected data received
            }

            // Read chunk
            std::span<uint8_t> buffer(chunk.data(), bytesToRead);
            auto result = m_driver->tout_read(m_defaultTimeout, buffer, options);

            if (result.status != ICommDriver::Status::SUCCESS) {
                // Check if we've received all expected data
                if (expectedSize > 0 && totalReceived == expectedSize) {
                    break;
                }
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("File read failed:"); 
                          LOG_STRING(ICommDriver::to_string(result.status)));
                return false;
            }

            if (result.bytes_read == 0) {
                break; // No more data
            }

            // Write to file
            file.write(reinterpret_cast<const char*>(chunk.data()), result.bytes_read);
            if (!file) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("File write failed"));
                return false;
            }

            totalReceived += result.bytes_read;

            // Stop if we've received expected amount
            if (expectedSize > 0 && totalReceived >= expectedSize) {
                break;
            }
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; 
                  LOG_STRING("File received successfully. Total:"); 
                  LOG_SIZET(totalReceived); LOG_STRING("bytes"));
        return true;
    }

    /**
     * @brief Convert string value to data bytes based on token type
     */
    bool convertToData(const std::string& value, 
                      CommCommandTokenType type, 
                      std::vector<uint8_t>& data) const
    {
        switch (type) {
            case CommCommandTokenType::HEXSTREAM:
                return hexutils::hexstringToVector(value, data);

            case CommCommandTokenType::LINE:
                if (ustring::stringToVector(value, data)) {
                    data.push_back('\n'); // Add newline for LINE type
                    return true;
                }
                return false;

            case CommCommandTokenType::TOKEN:
            case CommCommandTokenType::STRING_RAW:
            case CommCommandTokenType::STRING_DELIMITED:
            case CommCommandTokenType::STRING_DELIMITED_EMPTY:
                return ustring::stringToVector(value, data);

            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR; 
                          LOG_STRING("Unsupported token type for data conversion"));
                return false;
        }
    }
};

#endif // COMMSCRIPTCOMMANDINTERPRETER_HPP
