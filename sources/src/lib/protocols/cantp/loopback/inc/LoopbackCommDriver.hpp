#ifndef CAN_TP_LOOPBACK_COMM_DRIVER_HPP
#define CAN_TP_LOOPBACK_COMM_DRIVER_HPP

#include "ICommDriver.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file LoopbackCommDriver.hpp
 * @brief In-memory "CAN bus" ICommDriver: every tout_write() on an id enqueues
 *        one frame; tout_read() on the same id blocks (up to its timeout)
 *        until a frame is available, then pops it FIFO.
 *
 * This is what makes the loopback app a *loopback*: two threads each holding
 * a reference to the same LoopbackCommDriver instance — one calling
 * ITransportProtocol::send(), the other calling ::receive() (or, for
 * CANopen SDO, one client + SdoLoopbackServer) — talk to each other purely
 * through this object, with no real CAN hardware involved.
 *
 * Delivery is instantaneous (tout_write() never blocks and never fails on
 * its own account); every timeout that can fire comes from tout_read()
 * finding no frame for its id before u32ReadTimeout elapses — exactly the
 * situation a real bus produces when the peer never answers.
 *
 * Thread-safe: every method takes m_mutex, so one instance can safely be
 * shared by the sender thread, the receiver thread, and (for CANopen SDO)
 * the server thread all at once.
 */
class LoopbackCommDriver final : public ICommDriver
{
public:
    /** @param verbose  If true, prints every frame (id, direction, hex bytes) as it crosses the bus. */
    explicit LoopbackCommDriver(bool verbose = false) : m_verbose(verbose) {}

    WriteResult tout_write(uint32_t u32WriteTimeout,
                            std::span<const uint8_t> data,
                            std::string_view xtra_params) const override;

    ReadResult tout_read(uint32_t u32ReadTimeout,
                          std::span<uint8_t> buffer,
                          const ReadOptions& opts,
                          std::string_view xtra_params) const override;

    /** Drops every queued-but-unread frame on every id. Call between test cases. */
    void reset() const;

    CommDetails describeConnection(std::string_view xtra_params = {}) const {
        CommDetails det = {
            .family = CommFamily::CAN,
        };
        return det;
    }

    bool is_open() const { return true; }

private:
    struct Frame
    {
        std::vector<uint8_t> bytes;
    };

    bool m_verbose;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    mutable std::unordered_map<std::string, std::deque<Frame>> m_queues;
};

#endif // CAN_TP_LOOPBACK_COMM_DRIVER_HPP
