#ifndef USERIAL_DRIVER_H
#define USERIAL_DRIVER_H


#include <string>
#include <vector>
#ifndef _WIN32
#include <termios.h>
#endif


/**
 * @brief UART communication driver class.
 *
 * Provides methods for opening, reading from, writing to, and managing UART devices.
 */
class UART
{

public:

    /**
     * @brief Status codes returned by UART driver operations.
     */
    enum class Status : int32_t {
        SUCCESS = 0,              /**< Operation completed successfully. */
        INVALID_PARAM = -1,       /**< One or more parameters are invalid. */
        PORT_ACCESS = -2,         /**< Failed to access the UART port. */
        READ_TIMEOUT = -3,        /**< Read operation timed out. */
        WRITE_TIMEOUT = -4,       /**< Write operation timed out. */
        OUT_OF_MEMORY = -5,       /**< Memory allocation failed. */
        RETVAL_NOT_SET = -6       /**< Return value was not properly set. */
    };

    /**
     * @brief Converts a Status to a human-readable string.
     * @param code The status code to convert.
     * @return A string representation of the status code.
     */
    static std::string to_string(Status code);

    static constexpr size_t   UART_MAX_BUFLENGTH = 256; /**< Maximum UART buffer length. */
    static constexpr uint32_t UART_READ_DEFAULT_TIMEOUT  = 5000; /**< Default UART read timeout in milliseconds. */
    static constexpr uint32_t UART_WRITE_DEFAULT_TIMEOUT = 5000; /**< Default UART write timeout in milliseconds. */

    /**
     * @brief Constructs a new UART object.
     */
    explicit UART(const std::string& strDevice, uint32_t u32Speed) : m_iHandle(-1)
    {
        open(strDevice, u32Speed);
    }

    /**
     * @brief Destroys the UART object and releases resources.
     */
    virtual ~UART()
    {
        close();
    }

    /**
     * @brief Check if the driver is open for work
     */
    bool is_open();

    /**
     * @brief Reads data from the UART device with a timeout.
     * @param u32ReadTimeout Timeout in milliseconds.
     * @param pBuffer Pointer to the buffer to store read data.
     * @param szSizeToRead Number of bytes to read.
     * @return Status code indicating the result of the operation.
     */
    //Status timeout_read(uint32_t u32ReadTimeout, char *pBuffer, size_t szSizeToRead);
    Status timeout_read(uint32_t u32ReadTimeout, char *pBuffer, size_t szSizeToRead, size_t* pBytesRead);


    /**
     * @brief Reads a line from the UART device with a timeout.
     * @param u32ReadTimeout Timeout in milliseconds.
     * @param pBuffer Pointer to the buffer to store the line.
     * @param szBufferSize Size of the buffer.
     * @return Status code indicating the result of the operation.
     */
    Status timeout_readline(uint32_t u32ReadTimeout, char *pBuffer, size_t szBufferSize);

    /**
     * @brief Writes data to the UART device with a timeout.
     * @param u32WriteTimeout Timeout in milliseconds.
     * @param pBuffer Pointer to the data to write.
     * @param szSizeToWrite Number of bytes to write.
     * @return Status code indicating the result of the operation.
     */
    Status timeout_write(uint32_t u32WriteTimeout, const char *pBuffer, size_t szSizeToWrite);

    /**
     * @brief Waits for a specific token from the UART device within a timeout.
     * @param u32ReadTimeout Timeout in milliseconds.
     * @param pstrToken Token string to wait for.
     * @return Status code indicating the result of the operation.
     */
    Status timeout_wait_for_token(uint32_t u32ReadTimeout, const char *pstrToken);

    /**
     * @brief Waits for a specific token of known length from the UART device within a timeout.
     * @param u32ReadTimeout Timeout in milliseconds.
     * @param pstrToken Token string to wait for.
     * @param u32TokenLength Length of the token.
     * @return Status code indicating the result of the operation.
     */
    Status timeout_wait_for_token_buffer(uint32_t u32ReadTimeout, const char *pstrToken, uint32_t u32TokenLength);

private:

    int m_iHandle; /**< Internal handle to the UART device. */

    /**
     * @brief Opens a UART device with the specified speed.
     * @param strDevice Path to the UART device (e.g., "/dev/ttyUSB0").
     * @param u32Speed Baud rate for communication.
     * @return Status code indicating the result of the operation.
     */
    Status open(const std::string& strDevice, uint32_t u32Speed);

    /**
     * @brief Purges input and/or output buffers of the UART device.
     * @param bInput Whether to purge the input buffer.
     * @param bOutput Whether to purge the output buffer.
     * @return Status code indicating the result of the operation.
     */
    Status purge(bool bInput, bool bOutput);

    /**
     * @brief Closes the UART device.
     * @return Status code indicating the result of the operation.
     */
    Status close();

    /**
     * @brief Configures the UART device with the specified speed.
     * @param u32Speed Baud rate to configure.
     * @return Status code indicating the result of the operation.
     */
    Status setup(uint32_t u32Speed);

    /**
     * @brief Builds the KMP (Knuth-Morris-Pratt) table for pattern matching.
     * @param pstrPattern The pattern to search for.
     * @param u32Length Length of the pattern.
     * @param viLps Vector to store the computed LPS (Longest Prefix Suffix) table.
     */
    void build_kmp_table(const char *pstrPattern, uint32_t u32Length, std::vector<int>& viLps);

    /**
     * @brief Reads data from the UART device until a delimiter is found or timeout occurs.
     * @param u32TimeoutMs Timeout in milliseconds.
     * @param pstrBuffer Buffer to store the read data.
     * @param szBufferSize Size of the buffer.
     * @param cDelimiter Character to stop reading at.
     * @return Status code indicating the result of the operation.
     */
    Status read_until(uint32_t u32TimeoutMs, char *pstrBuffer, size_t szBufferSize, char cDelimiter);

#ifndef _WIN32
    /**
     * @brief Converts a baud rate to a platform-specific speed_t value (POSIX).
     * @param u32Speed Baud rate.
     * @return Corresponding speed_t value.
     */
    static speed_t getBaud(uint32_t u32Speed);
#else
    /**
     * @brief Returns the baud rate directly (Windows-specific).
     * @param u32Speed Baud rate.
     * @return Baud rate as uint32_t.
     */
    static uint32_t getBaud(uint32_t u32Speed);
#endif

};


#endif // DRIVER_HPP
