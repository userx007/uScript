#ifndef U_VECTOR_NOTIFY_WAITER_H
#define U_VECTOR_NOTIFY_WAITER_H

#include "ICommDriver.hpp"
#include "vxlapi_platform.hpp"

#include <cstdint>
#include <stop_token>

/**
 * @brief Wraps xlSetNotification()'s notification handle (XLhandle) and the
 *        platform-specific primitive used to block on it, so uVector/
 *        uVectorEth's recvFrame() can share one drain-then-wait loop
 *        unchanged across platforms — only open()/wait()/close() below
 *        differ per platform; everything that calls them does not.
 *
 * XLhandle is a Windows kernel HANDLE (from CreateEvent(), signalled via
 * xlSetNotification()'s own internal plumbing) on Windows, and a raw
 * eventfd(2) file descriptor on Linux (see vxlapi_linux.h's header comment,
 * point 4) — same type NAME on both platforms (XL-API's own vxlapi.h vs.
 * vxlapi_linux.h both typedef it), different underlying type, different
 * wait primitive (WaitForSingleObject() vs. poll()+read()). This class is
 * the only place that distinction is visible.
 *
 * Cancellation: wait() accepts a stop_token and returns promptly once it's
 * requested, by forcing the same wait primitive to wake up early:
 *  - Windows: SetEvent() on the handle.
 *  - Linux: a 1 written to the eventfd via write() (safe — eventfd is a
 *    counting semaphore explicitly designed for exactly this producer/
 *    consumer signalling pattern; XL-API and this caller are just two of
 *    potentially many writers, same as SetEvent() being safe to call from
 *    anywhere on Windows).
 * Either way this makes wait() return "as if" a real notification fired;
 * the caller (recvFrame()) always re-checks stop_tok.stop_requested()
 * itself immediately after to tell a genuine signal from this synthetic
 * one apart, rather than trusting a successful wait alone — see
 * uVector.hpp's recvFrame() doc comment for the full rationale, which
 * applies identically on both platforms.
 */
class VectorNotifyWaiter {
    public:
        enum class WaitResult {
            SIGNALLED,
            TIMEOUT,
            ERROR
        };

        VectorNotifyWaiter() = default;

        ~VectorNotifyWaiter()
        {
            close();
        }

        VectorNotifyWaiter(const VectorNotifyWaiter &)            = delete;
        VectorNotifyWaiter &operator=(const VectorNotifyWaiter &) = delete;

        /** Calls xlSetNotification(port, &handle, 1) and stores the resulting handle. */
        ICommDriver::Status open(XLportHandle port);

        /**
         * @brief Adopt a handle already obtained through some OTHER "set notification"
         *        call — e.g. xlNetSetNotification() for the Ethernet Network API
         *        (VectorEth's Linux path), which has its own function name and first
         *        argument type (XLnetworkHandle, not XLportHandle) but produces the
         *        exact same kind of XLhandle wait()/forceWake()/close() already knows
         *        how to block on. Callers own making that call themselves; this just
         *        takes over lifecycle bookkeeping for the result, same as open() does
         *        for xlSetNotification()'s.
         */
        void adopt(XLhandle handle);

        /** True once open() has succeeded and close() hasn't been called since. */
        bool is_open() const
        {
            return m_bOpen;
        }

        /**
         * @brief Block for up to u32TimeoutMs milliseconds (0 = wait forever) for either
         *        a genuine notification or a stop_tok cancellation, whichever comes first.
         */
        WaitResult wait(uint32_t u32TimeoutMs, std::stop_token stop_tok) const;

        /** Force wait() to return WaitResult::SIGNALLED at the next opportunity - see class comment. */
        void forceWake() const;

        void close();

    private:
        XLhandle m_handle = {};
        bool m_bOpen      = false;
};

#endif // U_VECTOR_NOTIFY_WAITER_H
