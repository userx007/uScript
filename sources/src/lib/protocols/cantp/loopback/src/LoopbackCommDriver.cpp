#include "LoopbackCommDriver.hpp"

#include <chrono>
#include <cstdio>
#include <algorithm>

namespace
{
    void print_frame(const char *dir, const std::string &id, std::span<const uint8_t> bytes)
    {
        std::fprintf(stderr, "  [bus] %s id=%-12s len=%2zu  ", dir, id.c_str(), bytes.size());
        for (uint8_t b : bytes)
        {
            std::fprintf(stderr, "%02X ", b);
        }
        std::fprintf(stderr, "\n");
    }
}

ICommDriver::WriteResult LoopbackCommDriver::tout_write(
    uint32_t /*u32WriteTimeout*/,
    std::span<const uint8_t> data,
    std::string_view xtra_params) const
{
    WriteResult result;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queues[std::string(xtra_params)].push_back(Frame{ std::vector<uint8_t>(data.begin(), data.end()) });
    }
    m_cv.notify_all();

    if (m_verbose)
    {
        print_frame("TX ->", std::string(xtra_params), data);
    }

    result.status = Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}

ICommDriver::ReadResult LoopbackCommDriver::tout_read(
    uint32_t u32ReadTimeout,
    std::span<uint8_t> buffer,
    const ReadOptions& opts,
    std::string_view xtra_params) const
{
    ReadResult result;
    const std::string id(xtra_params);

    std::unique_lock<std::mutex> lock(m_mutex);
    const bool gotOne = m_cv.wait_for(lock, std::chrono::milliseconds(u32ReadTimeout), [&] {
        auto it = m_queues.find(id);
        return it != m_queues.end() && !it->second.empty();
    });

    if (!gotOne)
    {
        result.status = Status::READ_TIMEOUT;
        return result;
    }

    Frame frame = std::move(m_queues[id].front());
    m_queues[id].pop_front();
    lock.unlock();

    if (m_verbose)
    {
        print_frame("<- RX", id, std::span<const uint8_t>(frame.bytes));
    }

    if (frame.bytes.size() > buffer.size())
    {
        // Every protocol here reads into a fixed 8-byte array matching the
        // classic-CAN frame size, so this should never trigger in practice;
        // it's a defensive check, not an expected path.
        result.status = Status::BUFFER_OVERFLOW;
        return result;
    }

    std::copy(frame.bytes.begin(), frame.bytes.end(), buffer.begin());
    result.status = Status::SUCCESS;
    result.bytes_read = frame.bytes.size();
    return result;
}

void LoopbackCommDriver::reset() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queues.clear();
}
