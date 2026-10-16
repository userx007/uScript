// uart_channel.hpp - serial port driver.
//
// Behaviour matches the original uart_loopback.c: the port is opened raw
// (no canonical mode/echo/signals) at a fixed POSIX baud rate, so arbitrary
// binary payloads pass through unmodified. Reads block for at least one
// byte (VMIN=1, VTIME=0) and return whatever is available at that point,
// exactly like the original.
//
// Windows: separate implementation below (Win32 comm API: CreateFile / DCB /
// COMMTIMEOUTS / ReadFile / WriteFile). Reads use a 100 ms timeout instead of
// blocking forever, so the g_stop flag set by Ctrl-C is noticed promptly (a
// blocking ReadFile is not interrupted by the console handler). Any positive
// baud rate is accepted; "COM10" and above are mapped to \\.\COMn.
#pragma once

#include "ichannel.hpp"

#if defined(_WIN32)

#include <windows.h>

namespace loopback {

    class UartChannel : public IChannel {
        public:
            UartChannel(std::string strDevice, long baud)
                : device_(std::move(strDevice))
                , baud_(baud)
            {
            }

            ~UartChannel() override
            {
                UartChannel::close();
            }

            bool open() override
            {
                if (baud_ <= 0) {
                    log_err(name(), "invalid baud rate " + std::to_string(baud_));
                    return false;
                }

                // COM10 and above need the \\.\ device-namespace prefix (harmless for COM1-9).
                std::string path = device_;
                if (path.compare(0, 4, "\\\\.\\") != 0 && !path.empty() && (path[0] == 'C' || path[0] == 'c')) {
                    path = "\\\\.\\" + path;
                }

                h_ = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h_ == INVALID_HANDLE_VALUE) {
                    log_err(name(), "open '" + path + "': " + lastError());
                    return false;
                }

                DCB dcb;
                std::memset(&dcb, 0, sizeof(dcb));
                dcb.DCBlength = sizeof(dcb);
                if (!::GetCommState(h_, &dcb)) {
                    log_err(name(), "GetCommState: " + lastError());
                    close();
                    return false;
                }

                dcb.BaudRate        = static_cast<DWORD>(baud_);
                dcb.ByteSize        = 8; // 8N1, raw, no flow control, no XON/XOFF, NULs preserved
                dcb.Parity          = NOPARITY;
                dcb.StopBits        = ONESTOPBIT;
                dcb.fBinary         = TRUE;
                dcb.fParity         = FALSE;
                dcb.fOutxCtsFlow    = FALSE;
                dcb.fOutxDsrFlow    = FALSE;
                dcb.fDtrControl     = DTR_CONTROL_ENABLE;
                dcb.fDsrSensitivity = FALSE; // ignore modem control lines (CLOCAL)
                dcb.fOutX           = FALSE;
                dcb.fInX            = FALSE;
                dcb.fErrorChar      = FALSE;
                dcb.fNull           = FALSE;
                dcb.fRtsControl     = RTS_CONTROL_ENABLE;
                dcb.fAbortOnError   = FALSE;
                if (!::SetCommState(h_, &dcb)) {
                    log_err(name(), "SetCommState: " + lastError());
                    close();
                    return false;
                }

                // Return as soon as >= 1 byte is available, else after 100 ms with 0 bytes.
                COMMTIMEOUTS to;
                to.ReadIntervalTimeout         = MAXDWORD;
                to.ReadTotalTimeoutMultiplier  = MAXDWORD;
                to.ReadTotalTimeoutConstant    = 100;
                to.WriteTotalTimeoutMultiplier = 0;
                to.WriteTotalTimeoutConstant   = 5000;
                if (!::SetCommTimeouts(h_, &to)) {
                    log_err(name(), "SetCommTimeouts: " + lastError());
                    close();
                    return false;
                }

                ::PurgeComm(h_, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

                log_info(name(), "opened @ " + std::to_string(baud_) + " baud");
                return true;
            }

            void close() override
            {
                if (h_ != INVALID_HANDLE_VALUE) {
                    ::CloseHandle(h_);
                    h_ = INVALID_HANDLE_VALUE;
                }
            }

            bool readMessage(Message &sMsg) override
            {
                unsigned char buf[4096];
                while (!g_stop) {
                    DWORD got = 0;
                    if (!::ReadFile(h_, buf, static_cast<DWORD>(sizeof(buf)), &got, nullptr)) {
                        log_err(name(), "read: " + lastError());
                        return false;
                    }
                    if (got == 0) {
                        continue; // 100 ms timeout, nothing available yet: re-check g_stop and try again
                    }

                    sMsg.data.assign(buf, buf + got);
                    sMsg.has_can_id = false;
                    return true;
                }
                return false;
            }

            bool writeMessage(Message &sMsg) override
            {
                size_t total = 0;
                while (total < sMsg.data.size()) {
                    DWORD put = 0;
                    if (!::WriteFile(h_, sMsg.data.data() + total, static_cast<DWORD>(sMsg.data.size() - total), &put,
                                     nullptr)) {
                        log_err(name(), "write: " + lastError());
                        return false;
                    }
                    total += put;
                }
                return true;
            }

            std::string name() const override
            {
                return "uart:" + device_ + "@" + std::to_string(baud_);
            }

            std::string identity() const override
            {
                return "uart:" + device_;
            }

            void dump(const char *pstrDir, const Message &sMsg) const override
            {
                dump_bytes(name(), pstrDir, sMsg.data.data(), sMsg.data.size());
            }

        private:
            static std::string lastError()
            {
                return tp_errstr(static_cast<int>(::GetLastError()));
            }

            std::string device_;
            long baud_;
            HANDLE h_ = INVALID_HANDLE_VALUE;
    };

} // namespace loopback

#else // POSIX (Linux, macOS, ...) - original implementation, unchanged

#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace loopback {

    class UartChannel : public IChannel {
        public:
            UartChannel(std::string strDevice, long baud)
                : device_(std::move(strDevice))
                , baud_(baud)
            {
            }

            ~UartChannel() override
            {
                UartChannel::close();
            }

            bool open() override
            {
                speed_t speed;
                if (!baudToFlag(baud_, speed)) {
                    log_err(name(), "unsupported baud rate " + std::to_string(baud_) +
                                        " (edit BAUD_TABLE in uart_channel.hpp to add it)");
                    return false;
                }

                // O_NDELAY/O_NONBLOCK at open time avoids blocking on DCD for modem
                // lines; cleared again right after so read() blocks normally.
                fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
                if (fd_ < 0) {
                    log_err(name(), "open '" + device_ + "': " + std::strerror(errno));
                    return false;
                }

                if (::fcntl(fd_, F_SETFL, 0) < 0) {
                    log_err(name(), std::string("fcntl F_SETFL: ") + std::strerror(errno));
                    close();
                    return false;
                }

                struct termios tty;
                std::memset(&tty, 0, sizeof(tty));
                if (::tcgetattr(fd_, &tty) < 0) {
                    log_err(name(), std::string("tcgetattr: ") + std::strerror(errno));
                    close();
                    return false;
                }

                cfmakeraw(&tty);
                cfsetispeed(&tty, speed);
                cfsetospeed(&tty, speed);

                tty.c_cflag |= (CLOCAL | CREAD);
                tty.c_cflag &= ~PARENB;
                tty.c_cflag &= ~CSTOPB;
                tty.c_cflag &= ~CSIZE;
                tty.c_cflag |= CS8;
                tty.c_cflag &= ~CRTSCTS;

                tty.c_cc[VMIN]  = 1;
                tty.c_cc[VTIME] = 0;

                if (::tcsetattr(fd_, TCSANOW, &tty) < 0) {
                    log_err(name(), std::string("tcsetattr: ") + std::strerror(errno));
                    close();
                    return false;
                }

                ::tcflush(fd_, TCIOFLUSH);

                log_info(name(), "opened @ " + std::to_string(baud_) + " baud");
                return true;
            }

            void close() override
            {
                if (fd_ >= 0) {
                    ::close(fd_);
                    fd_ = -1;
                }
            }

            bool readMessage(Message &sMsg) override
            {
                unsigned char buf[4096];
                while (!g_stop) {
                    ssize_t n = ::read(fd_, buf, sizeof(buf));
                    if (n < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        log_err(name(), std::string("read: ") + std::strerror(errno));
                        return false;
                    }
                    if (n == 0) {
                        continue; // nothing available yet, try again
                    }

                    sMsg.data.assign(buf, buf + n);
                    sMsg.has_can_id = false;
                    return true;
                }
                return false;
            }

            bool writeMessage(Message &sMsg) override
            {
                size_t total = 0;
                while (total < sMsg.data.size()) {
                    ssize_t w = ::write(fd_, sMsg.data.data() + total, sMsg.data.size() - total);
                    if (w < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        log_err(name(), std::string("write: ") + std::strerror(errno));
                        return false;
                    }
                    total += static_cast<size_t>(w);
                }
                return true;
            }

            std::string name() const override
            {
                return "uart:" + device_ + "@" + std::to_string(baud_);
            }

            std::string identity() const override
            {
                return "uart:" + device_;
            }

            void dump(const char *pstrDir, const Message &sMsg) const override
            {
                dump_bytes(name(), pstrDir, sMsg.data.data(), sMsg.data.size());
            }

        private:
            struct BaudEntry {
                    long value;
                    speed_t flag;
            };

            static bool baudToFlag(long baud, speed_t &out)
            {
                static const BaudEntry table[] = {
                    {50, B50},
                    {75, B75},
                    {110, B110},
                    {134, B134},
                    {150, B150},
                    {200, B200},
                    {300, B300},
                    {600, B600},
                    {1200, B1200},
                    {1800, B1800},
                    {2400, B2400},
                    {4800, B4800},
                    {9600, B9600},
                    {19200, B19200},
                    {38400, B38400},
                    {57600, B57600},
                    {115200, B115200},
                    {230400, B230400},
#ifdef B460800
                    {460800, B460800},
#endif
#ifdef B921600
                    {921600, B921600},
#endif
                };
                for (const auto &e : table) {
                    if (e.value == baud) {
                        out = e.flag;
                        return true;
                    }
                }
                return false;
            }

            std::string device_;
            long baud_;
            int fd_ = -1;
    };

} // namespace loopback

#endif // _WIN32
