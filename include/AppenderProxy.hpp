#pragma once

#include "AppenderFacade.h"
#include "LogFormatter.h"

class LogFormatter;
class LogEvent;

template<typename T>
concept IsAppenderImpl = requires(T x, LogFormatter y, LogEvent z) {
    x.log(y, z);
};

/**
 * @brief thread-safe, actually a proxy of the concrete appender
 */

// template<IsAppenderImpl Impl>
// requires IsAppenderImpl<Impl>
template<IsAppenderImpl Impl>
class AppenderProxy : public AppenderFacade {
public:
    AppenderProxy(): formatter_{LogFormatter{}}{}
    AppenderProxy(const AppenderProxy&) = delete;
    AppenderProxy(AppenderProxy&&) = delete;
    auto operator=(const AppenderProxy&) -> AppenderProxy& = delete;
    auto operator=(AppenderProxy&&) -> AppenderProxy& = delete; 

    // 完美转发
    template <typename... Ts>
    explicit AppenderProxy(LogFormatter fmter, Ts&&... ts) : formatter_(std::move(fmter)), impl_(std::forward<Ts>(ts)...){}

    // [[nodiscard]]意思是不要忽略我的返回值，如果调用者调用了这个函数，但是没有使用它的返回值。请务必给他一个警告。
    [[nodiscard]] auto GetFormater() const -> const LogFormater&
    {
        return formatter_;
    }

    auto SetFormatterPattern(LogFormatter formatter) -> void
    {
        formatter_ = std::move(formatter);
    }

    void log(const LogEvent& event) override
    {
        impl_.log(formatter_, event);
    }

    ~AppenderProxy() override = default;

private:
    LogFormatter formatter_;
    // 这个Impl类型是必须满足log(y,z)这样的类型
    Impl impl_;
};

