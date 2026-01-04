#pragma once
#include "LogLevel.h"
#include <source_location>
#include <cstdint>



class LogEvent{
public:
    LogEvent() = default;
    // 禁止复制构造
    LogEvent(const LogEvent&) = delete;
    // 禁止赋值构造
    LogEvent& operator=(const LogEvent&) = delete;
    // 允许移动构造

    // LogEvent&&：这是 右值引用。意思是这个构造函数的参数是一个“临时对象”或者“即将销毁的对象”。
    /*移动构造函数 */
    LogEvent& operator=(const LogEvent&&) noexcept = default;
    /*移动赋值运算符*/
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
    LogEvent(std::string logger_name, LogLevel::Level level, uint32_t elapse, uint32_t thread_id, std::string thread_name, time_t timestamp, std::string co_id, std::string source_loc);
    ~LogEvent() = default;

    std::string_view getLoggerName() const & {return logger_name_;}

    LogLevel::Level getLevel() const {return level_;}

    uint32_t getElapse() const & {return elapse_;}

    uint32_t getThreadId() const {return thread_id_;}

    uint32_t getFiberId() const {return fiber_id_;}

    std::string_view getThreadName() const & {return thread_name_;}


    std::string getFilename() const {return source_loc.file_name();}

    std::string getFunctionName() const {return source_loc.function_name();}

private:
    std::string logger_name_;
    LogLevel::Level level_;
    uint32_t elapse_;


};