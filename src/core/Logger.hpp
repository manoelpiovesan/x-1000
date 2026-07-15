#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>

namespace xpad::core {

class Logger {
public:
    static void info(std::string_view message) { log("INFO", message); }
    static void warn(std::string_view message) { log("WARN", message); }
    static void error(std::string_view message) { log("ERROR", message); }

private:
    static void log(std::string_view level, std::string_view message) {
        static std::mutex mutex;
        const auto now = std::chrono::system_clock::now();
        const auto nowTimeT = std::chrono::system_clock::to_time_t(now);
        std::tm calendarTime{};
#if defined(_WIN32)
        localtime_s(&calendarTime, &nowTimeT);
#else
        localtime_r(&nowTimeT, &calendarTime);
#endif
        std::ostringstream stream;
        stream << std::put_time(&calendarTime, "%H:%M:%S")
               << " [" << level << "] "
               << message;

        std::scoped_lock lock(mutex);
        std::cout << stream.str() << std::endl;
    }
};

} // namespace xpad::core

