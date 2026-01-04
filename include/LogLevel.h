#pragma once

#include <string_view>

class LogLevel {
public:
    enum Level{
        UNKNOWN = -1,
        ALL = 1,
        DEBUG = 2,
        INFO = 3,
        TRACE = 4,
        WARN = 5,
        ERROR = 6,
        FATAL = 7
    };

    std::string_view LevelToString(LogLevel::Level level);

    LogLevel::Level StringToLogLevel(std::string_view str);
};


