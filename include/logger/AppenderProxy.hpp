#pragma once

#include "AppenderFacade.h"
#include "LogFormatter.h"

#include <concepts>
#include <utility>

class LogFormatter;
class LogEvent;

template <typename T>
concept IsAppenderImpl = requires(T x, const LogFormatter& fmter, const LogEvent& event) {
    { x.log(fmter, event) } -> std::same_as<void>;
};

template <IsAppenderImpl Impl>
class AppenderProxy : public AppenderFacade {
public:
    AppenderProxy() : formatter_{LogFormatter{}} {}

    AppenderProxy(const AppenderProxy&) = delete;
    AppenderProxy(AppenderProxy&&) = delete;
    auto operator=(const AppenderProxy&) -> AppenderProxy& = delete;
    auto operator=(AppenderProxy&&) -> AppenderProxy& = delete;

    template <typename... Ts>
    explicit AppenderProxy(LogFormatter fmter, Ts&&... ts)
        : formatter_(std::move(fmter)), impl_(std::forward<Ts>(ts)...) {}

    [[nodiscard]] auto GetFormater() const -> const LogFormatter& { return formatter_; }

    auto SetFormatterPattern(LogFormatter formatter) -> void { formatter_ = std::move(formatter); }

    // impl实现的日志输出，AppenderFacade的log是它的接口
    void log(const LogEvent& event) override { impl_.log(formatter_, event); }

    ~AppenderProxy() override = default;

private:
    LogFormatter formatter_;
    Impl impl_;
};
