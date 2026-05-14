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

// 真正的事件构造逻辑
namespace logger_macro_detail {

// 去引用模板
template <typename T>
struct remove_cvref {
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

// 把logger统一转成引用
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

// 构造日志事件，
template <typename L>
inline auto make_event(L& logger, LogLevel level, std::source_location loc) -> LogEvent {
    // this_thread::get_id() 返回的是thread::id,所有要进行一个整数的hash值，然后进行一个uint32_t的转换
    /** @todo 这里用哈希转换可能会哈希冲突的问题，后期可以用 atomic<int>来替换 */
    const auto tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const auto now = SystemClock::to_time_t(SystemClock::now());
    return LogEvent(
        std::string{logger.getLoggerName()},    // 日志名
        level,                                  // 日志级别
        0U,                                     // 程序启动依赖的耗时(毫秒)
        tid,                                    // 线程id
        std::string{"MainThread"},              // 线程名
        now,                                    // 日志事件(UTC秒)
        0U,                                     // 协程id
        loc                                     // 源码位置信息
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


// 真正的事件构造放在 logger_macro_detail::make_event，真正的提交放在 submit_log。这样代码比纯宏堆叠更可维护
// 使用宏的主要原因是需要准确捕获调用点的 source_location。如果封装成普通函数，很容易捕获到封装函数内部的位置，而不是业务调用位置
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

// __VA_OPT__(,) 是 C++20 新增的宏语法，它只判断可变参数 __VA_ARGS__ 是否为空，如果为空则不打印可变参数。这样可以避免在调用时忘记添加可变参数。
#define LOG_DEBUG(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::DEBUG, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INFO(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::INFO, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_WARN(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::WARN, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_ERROR(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::ERROR, (fmt) __VA_OPT__(, ) __VA_ARGS__)
#define LOG_FATAL(logger, fmt, ...) LOG_LEVEL((logger), LogLevel::FATAL, (fmt) __VA_OPT__(, ) __VA_ARGS__)
