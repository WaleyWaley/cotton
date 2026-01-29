#pragma once

#include "AppenderProxy.hpp"
#include "LogFormatter.h"
#include <cstddef>
#include <fstream>


/*=================================================LogAppender==========================================*/
class StdoutAppender{
public:
    static void log(const LogFormatter& fmter, const LogEvent& event);
};

/**
 * @brief 滚动文件日志输出器。日志器如果大于64mb或时间超过了24小时，了就会自动新建一个日志文件，继续写入日志
 */
class RollingFileAppender{
private:
    static constexper size_t c_default_max_file_size = 64_mb;
    static constexper Seconds c_default_max_time_interval = Seconds(24*60*60);  // 24小时

    // Flush 策略相关常量
    static constexpr Seconds c_flush_seconds = Seconds(3);  // 每3秒flush一次
    static constexpr uint64_t c_flush_max_appends = 1024; // 每1024次写入强制刷新

    std::mutex mutex_;

    // 文件路径和名称
    std::string filename_;
    std::string basename_;  // 用于重命名时构建新文件名
    std::ofstream filestream_;

    // 滚动机制配置
    const size_t max_bytes_;      // 单个日志文件的最大字节数，超过则滚动
    const Seconds roll_interval_;   // 滚动时间间隔

    // 状态追踪
    TimePoint last_open_time_ = TimePoint::min();  // 上次打开文件的时间点
    bool reopen_error_ = false;              // 重新打开文件时是否出错
    size_t offset_ = 0;                     // 当前文件的写入的字节数

    // Flush 相关状态
    TimePoint last_flush_time_ = TimePoint::min(); // 上次flush的时间
    uint64_t flush_count_ = 0;               // 自上次flush以来的写入次数

    auto openFile_() -> void;

    /**
     * @brief  **滚动日志文件**：关闭当前文件，重命名它，并打开一个新的同名文件。
     */
    auto rollFile_() -> void;
    auto shouldRoll_() const -> bool;
    auto getNewLogFileName_() const -> std::string;

public:
    explicit RollingFileAppender(std::string filename,
                                 size_t max_file_size = c_default_max_file_size,
                                 Seconds roll_interval = c_default_max_time_interval);
    RollingFileAppender(const RollingFileAppender&) = delete;
    RollingFileAppender(RollingFileAppender&&) = delete;
    auto operator=(const RollingFileAppender&) -> RollingFileAppender& = delete;
    auto operator=(RollingFileAppender&&) -> RollingFileAppender& = delete;
    ~RollingFileAppender();
    void log(const LogFormatter& fmter, const LogEvent& event);
};

/**
 * @brief SQL日志输出器
 * @todo Implement SqlAppender
 */
class SqlAppender;
