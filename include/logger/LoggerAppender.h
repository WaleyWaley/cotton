#pragma once

#include "AppenderProxy.hpp"
#include "LogFormatter.h"
#include <cstddef>
#include <fstream>
#include "common/alias.h"
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <netinet/in.h>

class LogFormatter;
class LogEvent;


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
    static constexpr size_t c_default_max_file_size = 64_mb;
    static constexpr Seconds c_default_max_time_interval = Seconds(24*60*60);  // 24小时

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

// SqlAppender
class SqlAppender{
public:
    using SqlExecutor = std::function<void(const std::string& /*sql*/)>;
    explicit SqlAppender(std::string    table_name,
                         SqlExecutor    executor,
                         size_t         batch_size   = 64,
                         uint32_t       flush_interval_ms = 1000);

    // 禁止拷贝构造
    SqlAppender(const SqlAppender&)             = delete;
    // 禁止移动构造
    SqlAppender(SqlAppender&&)                  = delete;
    // 禁止拷贝赋值
    SqlAppender& operator=(const SqlAppender&)  = delete;
    // 禁止移动赋值
    SqlAppender& operator=(SqlAppender&&)       = delete;

    ~SqlAppender();

    // 满足 IsAppenderImpl concept 的接口
    void log(const LogFormatter& fmter, const LogEvent& event);

private:
    // 将一行日志数据打包成 INSERT INTO ... VALUES (...) 语句
    static auto buildInsertSql_(const std::string& table,
                                const std::string& level,
                                const std::string& logger,
                                uint32_t           thread_id,
                                std::time_t        timestamp,
                                const std::string& file,
                                uint32_t           line,
                                const std::string& message) -> std::string;
    
    // 后台工作线程：批量提交队列中的SQL
    void workerLoop_();

    // 将 pending_sqls_ 中的内容提交给 executor_, 需在持有锁或 swap 后调用
    void flushBatch_(std::vector<std::string>& batch);
    
    // ---------------数据成员-------------------
    std::string                 table_name_;
    SqlExecutor                 executor_;
    size_t                      batch_size_;
    uint32_t                    flush_interval_ms_;

    std::mutex                  mutex_;
    std::condition_variable     cv_;
    std::vector<std::string>     pending_sqls_; // 存储待提交的 SQL 语句

    std::atomic<bool>           stop_{false};
    std::thread                 worker_thread_;
};


// SocketAppender
/**
 * @param host               远端主机名或 IP 地址
 * @param port               远端端口
 * @param protocol           传输协议，默认 TCP
 * @param max_queue          内部队列最大消息数，超出则丢弃最旧消息，默认 4096
 * @param reconnect_interval_ms  TCP 断线重连间隔（毫秒），默认 3000
 */
class SocketAppender{
public:
    enum class Protocol {TCP, UDP};
    explicit SocketAppender(std::string host,
                            uint16_t    prot,
                            Protocol    protocol              = Protocol::TCP,
                            size_t      max_queue             = 4096,
                            uint32_t    reconnect_interval_ms = 3000);

    SocketAppender(const SocketAppender&)              = delete; 
    SocketAppender(SocketAppender&&)                   = delete; 
    SocketAppender& operator=(const SocketAppender&)   = delete; 
    SocketAppender& operator=(SocketAppender&&)        = delete;
    
    ~SocketAppender();


    void log(const LogFormatter& fmter, const LogEvent& event);
private:
    // TCP相关
    // 建立 TCP 连接，失败返回-1
    auto connectTcp_() -> int;
    // 确保 TCP 连接可用，必要时重连：返回有效 fd 或 -1
    auto ensureConnected_() ->int;
    // 通过 TCP 发送数据
    auto sendTcp_(const std::string& data) -> bool;
    // 通过 UDP 发送数据
    auto sendUdp_(const std::string& data) -> bool;
    // 后台工作线程
    void workerLoop_();

    // 数据成员
    std::string             host_;
    uint16_t                port_;
    Protocol                protocol_;
    size_t                  max_queue_;
    uint32_t                reconnect_interval_ms_;
    
    int sock_fd{-1};

    // 解析好的目标地址(UDP 复用)
    struct sockaddr_in      remote_addr_{};
    // 队列
    std::mutex              mutex_;
    std::condition_variable cv_;
    std::queue<std::string> msg_queue_;
    std::atomic<bool>       stop_{false};
    std::thread             worker_thread_;
};