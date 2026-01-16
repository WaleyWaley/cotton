#include "include/LogLevel.h"
#include "common/util.hpp"


/*===================================Level=======================================*/

auto LevelToString(LogLevel level) -> std::string_view
{
    switch(level)
    {
#define XX(levelvalue, levelname) \
    case (levelvalue) :{  \
        return #levelname; \
    }
        XX(LogLevel::SYSFATAL, SYSFATAL)
        XX(LogLevel::SYSERR, SYSERR)
        XX(LogLevel::FATAL, FATAL)
        XX(LogLevel::ERROR, ERROR)
        XX(LogLevel::WARN, WARN)
        XX(LogLevel::TRACE, TRACE)
        XX(LogLevel::INFO, INFO)
        XX(LogLevel::DEBUG, DEBUG)
        XX(LogLevel::ALL, ALL)
#undef XX
    }
}

auto StringToLevel(std::string_view str) -> LogLevel
{
    switch (UtilT::cHashString(str))
    {
#define XX(levelname, levelvalue) \
        case (UtilT::cHashString(#levelname)):\
        {   \
            return levelvalue; \
        }
    
        XX(SYSFATAL, LogLevel::SYSFATAL)
        XX(sysfatal, LogLevel::SYSFATAL)
        XX(SYSERR, LogLevel::SYSERR)
        XX(syserr, LogLevel::SYSERR)
        XX(FATAL, LogLevel::FATAL)
        XX(fatal, LogLevel::FATAL)
        XX(ERROR, LogLevel::ERROR)
        XX(error, LogLevel::ERROR)
        XX(WARN, LogLevel::WARN)
        XX(warn, LogLevel::WARN)
        XX(TRACE, LogLevel::TRACE)
        XX(trace, LogLevel::TRACE)
        XX(INFO, LogLevel::INFO)
        XX(info, LogLevel::INFO)
        XX(DEBUG, LogLevel::DEBUG)
        XX(debug, LogLevel::DEBUG)
        XX(ALL, LogLevel::ALL)
        XX(all, LogLevel::ALL)
#undef XX
    default:
        return LogLevel::ALL;
    }
}

auto operator<=(LogLevel lhs, LogLevel rhs) -> bool{
    return static_cast<std::underlying_type_t<LogLevel>>(lhs) <= static_cast<std::underlying_type_t<LogLevel>>(rhs);
}

auto operator>(LogLevel lhs, LogLevel rhs) -> bool{
    return not(lhs<=rhs);
}

auto operator==(LogLevel one, LogLevel two) -> bool{
    return static_cast<std::underlying_type_t<LogLevel>>(one) == static_cast<std::underlying_type_t<LogLevel>>(two);
}
auto operator>=(LogLevel lhs, LogLevel rhs) -> bool{
    return lhs > rhs or lhs == rhs;
}

auto operator<(LogLevel lhs, LogLevel rhs) -> bool{
    return not(lhs >= rhs);
}