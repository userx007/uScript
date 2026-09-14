#ifndef U_VECTOR_DRIVER_HANDLE_H
#define U_VECTOR_DRIVER_HANDLE_H

#include "ICommDriver.hpp"
#include "vxlapi_platform.hpp"

#include <cstdint>
#include <mutex>

/**
 * @brief Process-wide XL-API driver handle, shared by every XL-API-backed
 *        driver in this codebase (uVector/CAN, uVectorEth/Ethernet, and any
 *        future bus-specific wrapper - LIN/FlexRay/... - built the same way).
 *
 * xlOpenDriver()/xlCloseDriver() are global to the whole process, not
 * per-channel and not per-bus-type: one xlOpenDriver() call is enough for a
 * process to talk to CAN channels through one Vector instance AND Ethernet
 * channels through a VectorEth instance at the same time. XL-API itself
 * reference-counts repeated xlOpenDriver() calls internally, but this class
 * still keeps an explicit, codebase-wide refcount (rather than relying on
 * that undocumented internal behaviour, and rather than each bus-specific
 * driver class keeping its own independent counter) so the very first
 * XL-API channel opened anywhere in the process calls xlOpenDriver() and the
 * very last one closed calls xlCloseDriver().
 */
class VectorDriverHandle
{
public:
    /** xlOpenDriver() if this is the first live user in the whole process; always increments the refcount. */
    static ICommDriver::Status Acquire();

    /** Decrements the refcount; xlCloseDriver() if it reaches zero. */
    static void Release();

private:
    static std::mutex s_mutex;
    static uint32_t s_u32RefCount;
};

#endif // U_VECTOR_DRIVER_HANDLE_H
