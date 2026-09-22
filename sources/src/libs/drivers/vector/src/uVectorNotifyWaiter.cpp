#include "uVectorNotifyWaiter.hpp"

#include "uLogger.hpp"

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#endif

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "VECTOR_WAIT |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            IMPLEMENTATION                                   //
/////////////////////////////////////////////////////////////////////////////////

ICommDriver::Status VectorNotifyWaiter::open(XLportHandle port)
{
    close();

    XLhandle handle = {};
    XLstatus sts    = xlSetNotification(port, &handle, 1);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlSetNotification failed:"); LOG_STRING(xlGetErrorString(sts)));
        return ICommDriver::Status::PORT_ACCESS;
    }

    m_handle = handle;
    m_bOpen  = true;
    return ICommDriver::Status::SUCCESS;
}

void VectorNotifyWaiter::adopt(XLhandle handle)
{
    close();
    m_handle = handle;
    m_bOpen  = true;
}

VectorNotifyWaiter::WaitResult VectorNotifyWaiter::wait(uint32_t u32TimeoutMs, std::stop_token stop_tok) const
{
    if (!m_bOpen) {
        return WaitResult::ERROR;
    }

#if defined(_WIN32)

    const DWORD dwTimeout = (u32TimeoutMs == 0) ? INFINITE : static_cast<DWORD>(u32TimeoutMs);
    DWORD dwResult        = WaitForSingleObject(m_handle, dwTimeout);

    if (stop_tok.stop_requested()) {
        // dwResult may be WAIT_OBJECT_0 because of our own forceWake() rather
        // than a genuine notification - the caller re-checks stop_tok itself
        // to tell the two apart (see class comment), so it doesn't matter
        // which WaitResult we return here as long as it isn't ERROR.
        return WaitResult::TIMEOUT;
    }
    if (dwResult == WAIT_OBJECT_0) {
        return WaitResult::SIGNALLED;
    }
    if (dwResult == WAIT_TIMEOUT) {
        return WaitResult::TIMEOUT;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WaitForSingleObject returned:"); LOG_UINT32(dwResult));
    return WaitResult::ERROR;

#elif defined(__linux__)

    struct pollfd pfd;
    pfd.fd             = m_handle;
    pfd.events         = POLLIN;
    pfd.revents        = 0;

    const int iTimeout = (u32TimeoutMs == 0) ? -1 : static_cast<int>(u32TimeoutMs);
    int iRet           = poll(&pfd, 1, iTimeout);

    if (stop_tok.stop_requested()) {
        return WaitResult::TIMEOUT; // see the Windows branch's comment above - applies identically here
    }

    if (iRet == 0) {
        return WaitResult::TIMEOUT;
    }
    if (iRet < 0) {
        if (errno == EINTR) {
            // A signal interrupted us (not a stop_tok cancellation, that's
            // handled above) - treat exactly like a timeout so the caller's
            // drain-then-wait loop just tries again with whatever budget is
            // left, rather than surfacing a spurious error.
            return WaitResult::TIMEOUT;
        }
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed:"); LOG_STRING(std::strerror(errno)));
        return WaitResult::ERROR;
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("eventfd reported an error/hangup, revents:"); LOG_UINT32(static_cast<uint32_t>(pfd.revents)));
        return WaitResult::ERROR;
    }

    // Readable: clear/consume the counter (default eventfd semantics: read()
    // returns and resets the whole accumulated count to 0) before reporting
    // SIGNALLED, so a later wait() doesn't see stale readiness left over from
    // a count this call is about to fully account for.
    eventfd_t val = 0;
    if (eventfd_read(m_handle, &val) != 0 && errno != EAGAIN) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("eventfd_read failed:"); LOG_STRING(std::strerror(errno)));
    }

    return WaitResult::SIGNALLED;

#endif
}

void VectorNotifyWaiter::forceWake() const
{
    if (!m_bOpen) {
        return;
    }

#if defined(_WIN32)
    SetEvent(m_handle);
#elif defined(__linux__)
    if (eventfd_write(m_handle, 1) != 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("eventfd_write (forceWake) failed:"); LOG_STRING(std::strerror(errno)));
    }
#endif
}

void VectorNotifyWaiter::close()
{
    // Nothing to explicitly release here on either platform: the handle/fd
    // is owned by XL-API and torn down when xlClosePort()/xlCloseDriver()
    // run, exactly like the pre-refactor Windows-only code already assumed
    // (it never called CloseHandle() on m_hRxEvent either).
    //
    // Unverified on Linux specifically: whether libXlApi.so.26.20.14 also
    // close()s the eventfd itself when the owning port closes, the same way
    // it's assumed to release the Windows HANDLE. If long-running sessions
    // that repeatedly open/close channels show growing fd counts (check
    // with `ls /proc/<pid>/fd | wc -l` or `lsof -p <pid>`), that would mean
    // it doesn't, and this function should explicitly `::close(m_handle)`
    // here on the Linux branch — left as strictly ownership-neutral for now
    // rather than risking a double-close crash on an unverified assumption.
    m_handle = {};
    m_bOpen  = false;
}
