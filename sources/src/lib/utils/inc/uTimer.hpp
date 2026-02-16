#ifndef UTIMER_H
#define UTIMER_H

#include "uLogger.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <optional>

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

namespace utime
{

class Timer
{
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double>;

    // Constructor - optionally auto-start
    explicit Timer(const std::string& context = "", bool auto_start = true, bool auto_log = true)
        : context_(context)
        , auto_log_(auto_log)
        , is_running_(false)
        , accumulated_time_(0.0)
    {
        if (auto_start) {
            start();
        }
    }

    // Destructor - log if enabled
    ~Timer()
    {
        if (auto_log_ && has_started_) {
            stop();
            logElapsed();
        }
    }

    // Delete copy operations (timers shouldn't be copied)
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // Allow move operations
    Timer(Timer&& other) noexcept
        : context_(std::move(other.context_))
        , auto_log_(other.auto_log_)
        , is_running_(other.is_running_)
        , has_started_(other.has_started_)
        , start_time_(other.start_time_)
        , accumulated_time_(other.accumulated_time_)
        , laps_(std::move(other.laps_))
    {
        other.is_running_ = false;
        other.auto_log_ = false;  // Prevent moved-from object from logging
    }

    Timer& operator=(Timer&& other) noexcept
    {
        if (this != &other) {
            context_ = std::move(other.context_);
            auto_log_ = other.auto_log_;
            is_running_ = other.is_running_;
            has_started_ = other.has_started_;
            start_time_ = other.start_time_;
            accumulated_time_ = other.accumulated_time_;
            laps_ = std::move(other.laps_);
            
            other.is_running_ = false;
            other.auto_log_ = false;
        }
        return *this;
    }

    // Start/restart the timer
    void start()
    {
        if (!is_running_) {
            start_time_ = Clock::now();
            is_running_ = true;
            has_started_ = true;
        }
    }

    // Stop the timer (accumulates elapsed time)
    void stop()
    {
        if (is_running_) {
            accumulated_time_ += getCurrentElapsed();
            is_running_ = false;
        }
    }

    // Reset the timer to zero
    void reset()
    {
        accumulated_time_ = Duration::zero();
        laps_.clear();
        is_running_ = false;
        has_started_ = false;
    }

    // Restart (reset + start)
    void restart()
    {
        reset();
        start();
    }

    // Record a lap time (returns lap duration)
    double lap()
    {
        double total = elapsed_seconds();
        double lap_time = total - (laps_.empty() ? 0.0 : laps_.back());
        laps_.push_back(total);
        
        if (auto_log_) {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; 
                     LOG_STRING(getContextPrefix());
                     LOG_STRING("Lap #"); LOG_SIZET(laps_.size());
                     LOG_STRING(": "); LOG_DOUBLE(lap_time); 
                     LOG_STRING(" sec (total: "); LOG_DOUBLE(total); LOG_STRING(" sec)"));
        }
        
        return lap_time;
    }

    // Get elapsed time in various units
    double elapsed_seconds() const
    {
        return (accumulated_time_ + getCurrentElapsed()).count();
    }

    double elapsed_milliseconds() const
    {
        return elapsed_seconds() * 1000.0;
    }

    double elapsed_microseconds() const
    {
        return elapsed_seconds() * 1000000.0;
    }

    double elapsed_nanoseconds() const
    {
        return elapsed_seconds() * 1000000000.0;
    }

    // Get lap times
    const std::vector<double>& get_laps() const
    {
        return laps_;
    }

    size_t lap_count() const
    {
        return laps_.size();
    }

    // Query state
    bool is_running() const { return is_running_; }
    bool has_started() const { return has_started_; }
    
    const std::string& context() const { return context_; }
    void set_context(const std::string& ctx) { context_ = ctx; }

    // Manual logging
    void log() const
    {
        logElapsed();
    }

    // Format elapsed time as string
    std::string to_string(bool include_context = true) const
    {
        std::ostringstream oss;
        if (include_context && !context_.empty()) {
            oss << "[" << context_ << "] ";
        }
        oss << elapsed_seconds() << " sec";
        return oss.str();
    }

private:
    std::string context_;
    bool auto_log_;
    bool is_running_;
    bool has_started_ = false;
    TimePoint start_time_;
    Duration accumulated_time_;
    std::vector<double> laps_;

    // Get current elapsed time (only if running)
    Duration getCurrentElapsed() const
    {
        if (is_running_) {
            return Clock::now() - start_time_;
        }
        return Duration::zero();
    }

    // Get context prefix for logging
    std::string getContextPrefix() const
    {
        if (context_.empty()) {
            return "";
        }
        return "[" + context_ + "] ";
    }

    // Log the elapsed time
    void logElapsed() const
    {
        double seconds = elapsed_seconds();
        LOG_PRINT(LOG_DEBUG, LOG_HDR; 
                 LOG_STRING(getContextPrefix());
                 LOG_STRING("Elapsed Time: "); 
                 LOG_DOUBLE(seconds); 
                 LOG_STRING(" sec");
                 LOG_STRING(formatTime(seconds)));
    }

    // Format time in human-readable form
    std::string formatTime(double seconds) const
    {
        if (seconds < 0.000001) {
            return " (" + std::to_string(elapsed_nanoseconds()) + " ns)";
        } else if (seconds < 0.001) {
            return " (" + std::to_string(elapsed_microseconds()) + " μs)";
        } else if (seconds < 1.0) {
            return " (" + std::to_string(elapsed_milliseconds()) + " ms)";
        } else if (seconds < 60.0) {
            return "";
        } else if (seconds < 3600.0) {
            int mins = static_cast<int>(seconds / 60);
            double secs = seconds - (mins * 60);
            return " (" + std::to_string(mins) + " min " + std::to_string(secs) + " sec)";
        } else {
            int hours = static_cast<int>(seconds / 3600);
            int mins = static_cast<int>((seconds - hours * 3600) / 60);
            double secs = seconds - (hours * 3600) - (mins * 60);
            return " (" + std::to_string(hours) + " hr " + std::to_string(mins) + " min " + std::to_string(secs) + " sec)";
        }
    }
};

// RAII timer that auto-logs (original behavior)
class ScopedTimer : public Timer
{
public:
    explicit ScopedTimer(const std::string& context = "")
        : Timer(context, true, true)  // auto-start, auto-log
    {}
};

// Utility functions
inline void delay_ms(size_t milliseconds)
{
    if (milliseconds > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

inline void delay_us(size_t microseconds)
{
    if (microseconds > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    }
}

inline void delay_seconds(size_t seconds)
{
    if (seconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }
}

// Get current timestamp as string
inline std::string current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::string str = std::ctime(&time);
    str.pop_back();  // Remove trailing newline
    return str;
}

// Get high-precision timestamp in seconds since epoch
inline double timestamp_seconds()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

} // namespace utime

#endif // UTIMER_H


///////////////////////////////////////////////////////////////////////
// USAGE:
///////////////////////////////////////////////////////////////////////


/*
// 1. Simple RAII timing (like original)
{
    ScopedTimer timer("database query");
    database.execute(query);
}  // Auto-logs: "[database query] Elapsed Time: 0.523 sec (523.0 ms)"

// 2. Manual control with multiple measurements
Timer timer("processing", false, false);
timer.start();
process_part1();
timer.lap();  // Record lap 1

process_part2();
timer.lap();  // Record lap 2

timer.stop();
std::cout << "Total: " << timer.elapsed_seconds() << " sec\n";

// 3. Pause/resume timing
Timer timer("complex task");
timer.start();
do_work();
timer.stop();   // Pause

do_other_stuff();  // Not timed

timer.start();  // Resume
do_more_work();
timer.stop();

std::cout << "Work took: " << timer.elapsed_milliseconds() << " ms\n";

// 4. Conditional logging
Timer timer("operation", true, false);
// ... work ...
if (timer.elapsed_seconds() > 1.0) {
    LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Operation took too long!"));
    timer.log();
}

// 5. Query without stopping
Timer timer("server uptime");
while (server.running()) {
    if (timer.elapsed_seconds() > 3600) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Server uptime: 1 hour"));
        timer.restart();
    }
}

// 6. Race lap timing
Timer race("Formula 1 race");
for (int lap = 1; lap <= 10; ++lap) {
    drive_lap();
    double lap_time = race.lap();
    std::cout << "Lap " << lap << ": " << lap_time << " sec\n";
}

*/