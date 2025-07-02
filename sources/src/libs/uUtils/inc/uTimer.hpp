#ifndef UTIMER_H
#define UTIMER_H

#include "uLogger.hpp"

#include <chrono>
#include <string>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////


#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "TIMER      :"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////


class Timer
{
public:
    explicit Timer(const std::string& context = "")
        : context_(context)
        , start_time(std::chrono::high_resolution_clock::now())
    {}

    ~Timer()
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(context_.empty() ? nullptr : "[" + context_ + "]"); LOG_STRING("Elapsed Time (sec):"); LOG_DOUBLE(elapsed.count()));
    }

private:
    std::string context_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
};


#endif // UTIMER_H
