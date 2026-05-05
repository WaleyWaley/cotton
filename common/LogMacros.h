#pragma once

#include "logger/AsyncLogger.h"
#include "logger/Logger.h"
#include "logger/LogLevel.h"

#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace logger_macro_detail {

template <typename T>
struct remove_cvref {
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

inline auto to_ref(Logger& logger) noexcept -> Logger& { return logger; }
inline auto to_ref(AsyncLogger& logger) noexcept -> AsyncLogger& { return logger; }
inline auto to_ref(Logger* logger) noexcept -> Logger& { return *logger; }
inline auto to_ref(AsyncLogger* logger) noexcept -> AsyncLogger& { return *logger; }
inline auto to_ref(const std::shared_ptr<Logger>& logger) noexcept -> Logger& { return *logger; }
inline auto to_ref(const std::shared_ptr<AsyncLogger>& logger) noexcept -> AsyncLogger& { return *logger; }

inline auto is_valid(const Logger&) noexcept -> bool { return true; }
inline auto is_valid(const AsyncLogger&) noexcept -> bool { return true; }
inline auto is_valid(const Logger* logger) noexcept -> bool { return logger != nullptr; }
inline auto is_valid(const AsyncLogger* logger) noexcept -> bool { return logger != nullptr; }
inline auto is_valid(const std::shared_ptr<Logger>& logger) noexcept -> bool { return static_cast<bool>(logger); }
inline auto is_valid(const std::shared_ptr<AsyncLogger>& logger) noexcept -> bool { return static_cast<bool>(logger); }

template <typename L>
inline auto make_event(L& logger, LogLevel level, std::source_location loc) -> LogEvent {
    const auto tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const auto now = SystemClock::to_time_t(SystemClock::now());
    return LogEvent(
        std::string{logger.getLoggerName()},
        level,
        0U,
        tid,
        std::string{"MainThread"},
        now,
        0U,
        loc
    );
}

template <typename L>
inline auto submit_log(L& logger, LogEvent&& event) -> void {
    if constexpr (std::is_same_v<remove_cvref_t<L>, AsyncLogger>) {
        logger.append(std::move(event));
    } else {
        logger.log(event);
    }
}

} // namespace logger_macro_detail

#define LOG_LEVEL(logger, level, fmt, ...)                                                       \
    do {                                                                                          \
        if (::logger_macro_detail::is_valid((logger))) {                                         \
            auto& _cotton_logger = ::logger_macro_detail::to_ref((logger));                      \
            if (_cotton_logger.isLevelEnable((level))) {                                         \
                auto _cotton_event =                                                             \
                    ::logger_macro_detail::make_event(_cotton_logger, (level),                   \
                                                     std::source_location::current());            \
                _cotton_event.print((fmt) __VA_OPT__(, ) __VA_ARGS__);                           \
                ::logger_macro_detail::submit_log(_cotton_logger, std::move(_cotton_event));     \
            }                                                                                     \
        }                                                                                         \
    } while (false)

#define LOG_DEBUG(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::DEBUG, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INFO(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::INFO, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_WARN(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::WARN, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_ERROR(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::ERROR, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_FATAL(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::FATAL, (fmt) __VA_OPT__(, ) __VA_ARGS__)
