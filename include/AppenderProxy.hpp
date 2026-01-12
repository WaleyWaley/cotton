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
};