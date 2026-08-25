// uart_channel.hpp - serial port driver.
//
// Behaviour matches the original uart_loopback.c: the port is opened raw
// (no canonical mode/echo/signals) at a fixed POSIX baud rate, so arbitrary
// binary payloads pass through unmodified. Reads block for at least one
// byte (VMIN=1, VTIME=0) and return whatever is available at that point,
// exactly like the original.
#pragma once

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>

#include "ichannel.hpp"

namespace loopback
{

class UartChannel : public IChannel
{
public:
    UartChannel(std::string device, long baud)
        : device_(std::move(device)), baud_(baud)
    {
    }

    ~UartChannel() override { UartChannel::close(); }

    bool open() override
    {
        speed_t speed;
        if (!baudToFlag(baud_, speed))
        {
            log_err(name(), "unsupported baud rate " + std::to_string(baud_) +
                                 " (edit BAUD_TABLE in uart_channel.hpp to add it)");
            return false;
        }

        // O_NDELAY/O_NONBLOCK at open time avoids blocking on DCD for modem
        // lines; cleared again right after so read() blocks normally.
        fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0)
        {
            log_err(name(), "open '" + device_ + "': " + std::strerror(errno));
            return false;
        }

        if (::fcntl(fd_, F_SETFL, 0) < 0)
        {
            log_err(name(), std::string("fcntl F_SETFL: ") + std::strerror(errno));
            close();
            return false;
        }

        struct termios tty;
        std::memset(&tty, 0, sizeof(tty));
        if (::tcgetattr(fd_, &tty) < 0)
        {
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

        if (::tcsetattr(fd_, TCSANOW, &tty) < 0)
        {
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
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool readMessage(Message &msg) override
    {
        unsigned char buf[4096];
        while (!g_stop)
        {
            ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                log_err(name(), std::string("read: ") + std::strerror(errno));
                return false;
            }
            if (n == 0)
                continue; // nothing available yet, try again

            msg.data.assign(buf, buf + n);
            msg.has_can_id = false;
            return true;
        }
        return false;
    }

    bool writeMessage(Message &msg) override
    {
        size_t total = 0;
        while (total < msg.data.size())
        {
            ssize_t w = ::write(fd_, msg.data.data() + total, msg.data.size() - total);
            if (w < 0)
            {
                if (errno == EINTR)
                    continue;
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

    std::string identity() const override { return "uart:" + device_; }

    void dump(const char *dir, const Message &msg) const override
    {
        dump_bytes(name(), dir, msg.data.data(), msg.data.size());
    }

private:
    struct BaudEntry { long value; speed_t flag; };

    static bool baudToFlag(long baud, speed_t &out)
    {
        static const BaudEntry table[] = {
            {     50,     B50 }, {     75,     B75 }, {    110,    B110 },
            {    134,    B134 }, {    150,    B150 }, {    200,    B200 },
            {    300,    B300 }, {    600,    B600 }, {   1200,   B1200 },
            {   1800,   B1800 }, {   2400,   B2400 }, {   4800,   B4800 },
            {   9600,   B9600 }, {  19200,  B19200 }, {  38400,  B38400 },
            {  57600,  B57600 }, { 115200, B115200 }, { 230400, B230400 },
#ifdef B460800
            { 460800, B460800 },
#endif
#ifdef B921600
            { 921600, B921600 },
#endif
        };
        for (const auto &e : table)
            if (e.value == baud) { out = e.flag; return true; }
        return false;
    }

    std::string device_;
    long        baud_;
    int         fd_ = -1;
};

} // namespace loopback
