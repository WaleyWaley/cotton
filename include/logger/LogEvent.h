#pragma once
#include "LogLevel.h"
#include <source_location>
#include <cstdint>
#include <sstream>
#include <memory>
#include <format>
#include <ctime>



class LogEvent{
public:
    using Sptr = std::shared_ptr<LogEvent>;

    LogEvent() = default;
    LogEvent(const LogEvent&) = delete;
    LogEvent(LogEvent &&) noexcept = default;

    // LogEvent&&：这是 右值引用。意思是这个构造函数的参数是一个“临时对象”或者“即将销毁的对象”。
    LogEvent& operator=(const LogEvent& ) = default;
    LogEvent& operator=(LogEvent&&) = default;

    /**
     * @brief 构造函数
     * @param logger_name 日志器名称
     * @param level 日志级别
     * @param elapse 程序启动依赖的耗时(毫秒)
     * @param thread_id 线程id
     * @param thread_name 线程名称
     * @param time 日志事件(UTC秒)
     * @param co_id 协程id
     * @param source_loc 源码位置信息
     */
    LogEvent(std::string logger_name, LogLevel::Level level, uint32_t elapse, uint32_t thread_id, std::string thread_name, time_t timestamp, uint32_t co_id, std::source_location source_loc = std::source_location::current());

    ~LogEvent() = default;

    std::string_view getLoggerName() const & {return logger_name_;}

    LogLevel::Level getLevel() const {return level_;}

    uint32_t getElapse() const {return elapse_;}

    uint32_t getThreadId() const {return thread_id_;}

    std::string_view getThreadName() const & {return thread_name_;}

    std::time_t getTime() const {return timestamp_;}
    
    uint32_t getFiberId() const {return co_id_;}

    std::string getContent() const {return custom_msg_.str();}

    std::stringstream& getSS() {return custom_msg_;}

    std::string getFilename() const {return source_loc_.file_name();}

    std::string getFunctionName() const {return source_loc_.function_name();}

    template <typename... Args>
    void print(std::format_string<Args...> fmt, Args&&... args){
        custom_msg_ << std::format(fmt, std::forward<Args>(args)...);
    }

private:
    std::string logger_name_;
    LogLevel::Level level_;
    uint32_t elapse_;
    uint32_t thread_id_;
    std::string thread_name_;
    std::time_t timestamp_;
    uint32_t co_id_;
    std::source_location source_loc_;
    std::stringstream custom_msg_;

};