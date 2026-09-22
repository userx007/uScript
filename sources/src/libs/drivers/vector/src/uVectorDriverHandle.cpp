#include "uVectorDriverHandle.hpp"

#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "VECTOR_DRV  |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            IMPLEMENTATION                                   //
/////////////////////////////////////////////////////////////////////////////////

std::mutex VectorDriverHandle::s_mutex;
uint32_t VectorDriverHandle::s_u32RefCount = 0;

ICommDriver::Status VectorDriverHandle::Acquire()
{
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_u32RefCount == 0) {
        XLstatus sts = xlOpenDriver();
        if (sts != XL_SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("xlOpenDriver failed:"); LOG_STRING(xlGetErrorString(sts)));
            return ICommDriver::Status::PORT_ACCESS;
        }
    }
    ++s_u32RefCount;
    return ICommDriver::Status::SUCCESS;
}

void VectorDriverHandle::Release()
{
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_u32RefCount == 0) {
        return;
    }
    if (--s_u32RefCount == 0) {
        xlCloseDriver();
    }
}
