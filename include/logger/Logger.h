#pragma once
#include "common/alias.h"
#include <iostream>
#include <string_view>
#include <source_location>
#include <chrono>
/**
 * @brief 日志器，用于输出日志，log用于输出日事件。Logger包含日志级别，日志器名称，创建时间，以及一个LogAppender数组。
    日志事件由 log方法输出， log方法首先判断日志级别是否达到本 Logger 的级别要求，
    如果满足则将日志事件传递给所有LogAppender进行输出，否则丢弃该条日志
 */

class Appender;
class LogEvent;
enum class LogLevel;

class Logger : public std::enable_shared_from_this<Logger>{
public:

    // 带参构造
    explicit Logger(std::string name) : name_(std::move(name)) {}

    // 无参构造，自动生成名字   
    Logger() : name_(std::to_string(auto_logger_id_.fetch_add(1))) {}        // fetch_add 是 atomic 的标准写法，等价于后置 ++

    void log(const LogEvent& event) const;
    // void log(const LogEvent& event, std::error_code &ec) const;

    void addAppender(Sptr<Appender> appender);

    void delAppender(Sptr<Appender> appender);

    void clearAppender();

    std::string_view getLoggerName() const {return name_;}

    void setLogLevel(LogLevel::Level level) {level_ = level;}

    LogLevel::Level getLogLevel() const {return level_;}

    bool isLevelEnable(LogLevel::Level level) const {return level >= level_;}

private:    
    // 日志名称
    std::string name_;
    // 日志级别
    LogLevel::Level level_;
    // Appender集合
    std::vector<Sptr<Appender>> appenders_;
    // 自动日志器ID, inline static 可以在类内初始化
    inline static std::atomic<uint32_t> auto_logger_id_ = 0;
};


// 在测试时调用的就是封装好的这个log函数，Logger的log是不对外暴露的
inline void log(const Logger& logger, LogLevel::Level loglevel, std::source_location source_info){
    logger.log(LogEvent {
        logger.getLoggerName(),
        loglevel,
        0,
        std::this_thread::get_id(),
        Curthr::GetName(),
        0,
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
        source_info});
}