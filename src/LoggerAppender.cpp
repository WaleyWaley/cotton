#include "logger/LoggerAppender.h"
#include "common/alias.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <sys/select.h>
#include <system_error>
#include <time.h>
#include <sstream>
#include <cstdio>
#include <netinet/in.h>
#include <cerrno>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>

/*=====================================LogAppender======================================*/

/*===========================StdoutAppender==================*/
void StdoutAppender::log(const LogFormatter& fmter, const LogEvent& event){
    fmter.format(std::cout, event);
}

/*===========================RollingFileAppenderAppender==================*/

RollingFileAppender::RollingFileAppender(std::string filename,
                                         size_t max_bytes,          // 文件极限大小
                                         Seconds roll_interval)     // 滚动的事件间隔    
    : filename_{std::move(filename)}
    , basename_{std::filesystem::path{filename_}.filename().string()}
    , max_bytes_{max_bytes}
    , roll_interval_{roll_interval} { openFile_(); }


RollingFileAppender::~RollingFileAppender(){
    auto _ = std::lock_guard{mutex_};
    if(filestream_.is_open())
    {
        filestream_.close();
    }
}

auto RollingFileAppender::openFile_() -> void{
    // 清楚错误标志，准备重新打开文件
    reopen_error_ = false;
    // 设置流，在failbit 或 badbit 时抛出异常
    // failbit: 逻辑错误，格式错误、文件未找到
    // badbit: 严重错误，如磁盘满、硬件故障、内存不足
    filestream_.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try{
        // 使用 std::ios::app 模式打开文件，追加写入, std::ios::binary 确保以二进制模式写入，避免某些平台上的换行符转换问题
        filestream_.open(filename_, std::ios::out | std::ios::app | std::ios::binary);
    } catch (const std::ios_base::failure& e){
        const auto& ec = e.code();
        // cerr 是标准错误输出流，一般用于输出错误信息，当你往 cerr 里写东西时，它会强制立刻、马上输出到屏幕。即使程序下一行代码就崩溃了，cerr 输出的报错信息也能保证让你看到。这就是为什么报错要用 cerr。
        std::cerr << "---文件操作失败---" << std::endl;
        // e.what() 会输出你在 throw 时写的错误描述信息
        std::cerr << "错误描述(what()): " << e.what() << std::endl;
        std::cerr << "错误代码(value): " << ec.value() << std::endl;
        std::cerr << "错误类别(category): " << ec.category().name() << std::endl;
        throw std::system_error{ec};
    }
    last_open_time_ = Clock::now();
    // 获取当前文件大小，更新 offset_。tellp() 的作用就是：返回当前光标距离文件开头有多少个字节（Byte）。
    /* 写入数据
        filestream_ << "hello"; 
        问问流：现在写到第几个字节了？
        如果文件是空的，写了5个字，tellp() 就返回 5
        offset_ = filestream_.tellp();
    */
    offset_ = filestream_.tellp();
}

void RollingFileAppender::rollFile_(){
    if(not filestream_.is_open())
    {
        // 文件未打开，无法滚动,直接尝试打开新的文件
        openFile_();
    }
    // 1.关闭当前文件
    filestream_.close();

    // 2.生成带时间戳的新文件名
    std::string new_filename = getNewLogFileName_();

    // 3. 重命名文件 (旧文件名 -> 新文件名)
    // 使用 std::rename 进行原子操作
    int ret = std::rename(filename_.c_str(), new_filename.c_str());
    if(ret != 0)
    {
        // 修正 system_error 调用
        throw std::system_error(
            std::error_code(errno, std::system_category()), " 重命名日志文件失败: " + filename_ + "->" + new_filename
        );
    }
    // 4.打开一个新的日志文件
    return openFile_();
}

// 判断是否需要滚动日志文件
auto RollingFileAppender::shouldRoll_() const -> bool
{
    // 1.大小检测
    if(offset_ > max_bytes_)
    {
        return true;
    }

    // 2.时间检测
    auto now = Clock::now();
    auto elpased = std::chrono::duration_cast<Seconds>(now - last_open_time_);
    return elpased >= roll_interval_;
}

auto RollingFileAppender::getNewLogFileName_() const -> std::string{

    auto p = std::filesystem::path{filename_};

    // 1.获取文件名主体(不包含扩展名)和扩展名
    auto stem = p.stem().string();         // 文件名主体,对于 "app.log"，得到 "app"
    auto extension = p.extension().string(); // 扩展名,对于 "app.log"，得到 ".log"

    // 2. 获取时间戳字符串
    auto now = std::chrono::time_point_cast<Seconds>(SystemClock::now());
    auto zone_time = ZoneTime<Seconds>{std::chrono::current_zone(), now};
    auto time_point_str = std::format("{:%Y-%m-%d_%H-%M-%S}", zone_time.get_local_time());

    // 3. 构建新的文件名: stem.YYYYMMDD-HHMMSS.extension
    // 注意：如果原文件名没有扩展名，extension会是空字符串
    std::string new_filename = stem;
    new_filename += "." + std::string{time_point_str};
    new_filename += extension;

    // 4. 组合路径：使用 parent_path() 确保新文件仍在原目录
    // 原始文件名包含路径，所以我们需要父路径,这里的/是路径连接符
    return (p.parent_path() / new_filename).string();
}

auto RollingFileAppender::log(const LogFormatter& fmter, const LogEvent& event) -> void {
    // 1.加锁，确保线程安全
    auto _ = std::lock_guard{mutex_};

    // 2.检查是否需要滚动日志文件
    if(shouldRoll_())
    {
        rollFile_();
    }
    // 3.格式化并写入日志,
    auto total_size = fmter.format(filestream_, event); // 返回写了多少字节

    // 4.更新写入偏移量
    offset_ += total_size;

    // 5.处理 Flush 策略
    flush_count_++;

    // 6. 检查是否需要 Flush
    auto now = Clock::now();

    // 检查时间间隔
    auto time_to_flush = ((now - last_flush_time_) >= c_flush_seconds);

    // 检查写入次数
    bool count_to_flush = (flush_count_ >= c_flush_max_appends);

    if(time_to_flush || count_to_flush)
    {
        // 调用 std::ostream::flush() 将数据从 C++ 缓冲区推送到操作系统/ 
        filestream_.flush();
        // 重置状态
        last_flush_time_ = now;
        flush_count_ = 0;
    }
}


// SqlAppender实现

SqlAppender::SqlAppender(std::string        table_name,
                         SqlExecutor        executor,   
                         size_t             batch_size,
                         uint32_t           flush_interval_ms)
    : table_name_{std::move(table_name)}
    , executor_{std::move(executor)}
    , batch_size_{batch_size}
    , flush_interval_ms_{flush_interval_ms}
{
    // 预分配, 避免频繁rehash
    pending_sqls_.reserve(batch_size_ * 2);

    // 启动后台写入线程
    worker_thread_ = std::thread{[this]{workerLoop_();}};
} 

SqlAppender::~SqlAppender()
{
    // 1.通知后台线程退出
    {
        std::unique_lock<std::mutex> lock(mutex_);
        stop_.store(true, std::memory_order_release);
    }

    cv_.notify_one();

    if(worker_thread_.joinable())
        worker_thread_.join();
}

// log() - 仅入队，不阻塞调用方
void SqlAppender::log(const LogFormatter& fmter, const LogEvent& event)
{
    bool should_notify = false;
    // 从event中提取结构化字段，构建 INSERT 语句
    auto sql = buildInsertSql_(
        table_name_,
        std::string{LevelToString(event.getLevel())},
        std::string{event.getLoggerName()},
        event.getThreadId(),
        event.getTime(),
        event.getFilename(),
        event.getLine(),
        event.getContent()
    );

    {
        std::unique_lock<std::mutex> lock{mutex_};
        pending_sqls_.emplace_back(std::move(sql));
        should_notify = pending_sqls_.size() >= batch_size_;
    }

    // 达到batch_size_时主动唤醒后台线程提前提交
    if(should_notify)
        cv_.notify_one();
}

// 后台工作线程
void SqlAppender::workerLoop_()
{
    while(true)
    {
        std::vector<std::string> batch;
        batch.reserve(batch_size_); // 预分配内存

        {
            std::unique_lock<std::mutex> lock{mutex_};
            // 等待：有数据 or 超时 or 停止信号
            cv_.wait_for(lock, std::chrono::milliseconds{flush_interval_ms_},[this]{return !pending_sqls_.empty() || stop_.load(std::memory_order_acquire);});

            // 将待处理 SQL swap 出来，尽快释放锁
            batch.swap(pending_sqls_);
        }

        // 完全【无锁】状态下，从容落盘
        if(!batch.empty())
            flushBatch_(batch);

        if(stop_.load(std::memory_order_acquire))
        {
            // 退出前最后一次打扫战场，防止在刚才 flushBatch_ 期间又有新数据进来
            std::vector<std::string> remaining;
            {
                // 此时外面的锁早就释放了，这里重新加锁是绝对安全的
                std::unique_lock<std::mutex> lock{mutex_};
                remaining.swap(pending_sqls_);
            } // 拿到最后的数据，立刻解锁

            if(!remaining.empty())
                flushBatch_(remaining);

            break;
        }
    }
}

// 批量落盘函数
void SqlAppender::flushBatch_(std::vector<std::string>& batch)
{
    for(auto& sql : batch)
    {
        try
        {
            executor_(sql);
        }
        catch(const std::exception& e)
        {
            // SQL 执行失败时输出到 stderr, 避免递归调用日志系统
            std::fprintf(stderr, "[SqlAppender] executor threw: %s SQL: %s", e.what(), sql.c_str());
        }
        catch(...)
        {
            std::fprintf(stderr, "[SqlAppender] executor threw unknown exception.");
        }
    }
}

// SQL字符串转义：将单引号替换为两个单引号
static auto escapeSqlString(const std::string& s) -> std::string
{
    std::string result;
    result.reserve(s.size());
    for(char c : s)
    {
        if(c=='\'') result += "''";
        else        result += c;
    }
    return result;
}

auto SqlAppender::buildInsertSql_(const std::string& table,
                                  const std::string& level,
                                  const std::string& logger,
                                  uint32_t           thread_id,
                                  std::time_t        timestamp,
                                  const std::string& file,
                                  uint32_t           line,
                                  const std::string& message) -> std::string
{
    return std::format(
        "INSERT INTO {} (level, logger, thread_id, timestamp, file, line, message) "
        "VALUES ('{}', '{}', {}, {}, '{}', {}, '{}');",
        table,
        escapeSqlString(level),
        escapeSqlString(logger),
        thread_id,
        static_cast<long long>(timestamp),
        escapeSqlString(file),
        line,
        escapeSqlString(message)
    );
}



// SocketAppender 实现
SocketAppender::SocketAppender(std::string host,
                               uint16_t port,
                               Protocol protocol,
                               size_t   max_queue,
                               uint32_t reconnect_interval_ms)
    : host_{std::move(host)}
    , port_{port}
    , protocol_{protocol}
    , max_queue_{max_queue}
    , reconnect_interval_ms_{reconnect_interval_ms}
{
    // 解析目标地址(host 可为域名或 IP)
    std::memset(&remote_addr_, 0, sizeof(remote_addr_));    //防止脏数据
    remote_addr_.sin_family = AF_INET;      // IPv4协议
    // htons把主机字节序转网络字节序
    remote_addr_.sin_port   = htons(port_); // 端口

    // 解析client IP
    // 先尝试 inet_pton(纯IP字符串)
    // 输入点分十进制，解析后写入 remote_addr_.sin_addr
    if(::inet_pton(AF_INET, host.c_str(), &remote_addr_.sin_addr) != 1)
    {
        // 非纯 IP， 尝试 DNS 解析
        struct addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = (protocol_ == Protocol::TCP) ? SOCK_STREAM : SOCK_DGRAM;

        // 存储DNS解析结果（链表）
        struct addrinfo* res = nullptr;

        if(::getaddrinfo(host_.c_str(), nullptr, &hints, &res) == 0 and res)
        {
            // 把解析结果转为IPv4地址结构体
            auto* addr4 = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
            remote_addr_.sin_addr = addr4->sin_addr;
            ::freeaddrinfo(res);    // 释放DNS解析的内存，避免内存泄漏
        }
        else
        {
            // 都解析失败
            // int fprintf(FILE* 流指针, 格式化字符串, 参数1, 参数2...);
            std::fprintf(stderr, "{SocketAppender} Failed to resolve host: %s\n", host_.c_str());
        }
    }

    // UDP:提前创建socket(无需connect)
    if(protocol == Protocol::UDP)
    {
        sock_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if(sock_fd<0)
            std::fprintf(stderr, "[SocketAppender] UDP socket() failed: %s\n", std::strerror(errno));
    }

    // 启动后台发送线程
    worker_thread_ = std::thread{[this]{workerLoop_();}};
}

SocketAppender::~SocketAppender()
{
    {
        auto lock = std::unique_lock{mutex_};
        stop_.store(true, std::memory_order_release);
    }
    cv_.notify_one();

    if(worker_thread_.joinable())
        worker_thread_.join();

    if(sock_fd >= 0)
    {
        ::close(sock_fd);
        sock_fd = -1;
    }
}

// 前台写入消息队列
void SocketAppender::log(const LogFormatter& fmter, const LogEvent& event)
{
    if(max_queue_ == 0)
        return;
    // 调用format的单参数重载把事件转化为字符串
    std::string msg = fmter.format(event);
    {
        auto lock = std::unique_lock{mutex_};

        // 队列满时丢弃最旧的消息，保证调用方不阻塞, 严苛的防御性约束：不管队列现在多肿，强制瘦身到 max_queue_ 以下
        while(msg_queue_.size() >= max_queue_)
            msg_queue_.pop();

        msg_queue_.push(std::move(msg));
    } // 锁结束
    // 只是负责唤醒后台线程。后台线程醒来后，马上要重新拿 mutex_。如果你还没释放锁就通知它，它醒来也只能继续等锁。
    cv_.notify_one();
}

// 后台工作线程来sendTcp_或者sendUdp_
void SocketAppender::workerLoop_()
{
    while(true)
    {
        std::string msg;

        {
            auto lock = std::unique_lock{mutex_};
            cv_.wait(lock,[this]{return !msg_queue_.empty() || stop_.load(std::memory_order_acquire);});
            if(msg_queue_.empty())
            {
                // stop_ == true 且队列为空，正常退出
                break;
            }
            msg = std::move(msg_queue_.front());
            msg_queue_.pop();
        }   // 锁结束

        // 在锁外执行网络 I/O
        bool ok = false;

        if(protocol_ == Protocol::TCP)
            ok = sendTcp_(msg);
        else
            ok = sendUdp_(msg);

        if(!ok)
            std::fprintf(stderr, "[SocketAppender] Failed to send log message.\n");
    }
}

// TCP: 连接/重连/发送

auto SocketAppender::connectTcp_() -> int
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        std::fprintf(stderr, "[SocketAppender] socket() failed: %s\n", std::strerror(errno));
        return -1;
    }
    if(::connect(fd,
                 reinterpret_cast<struct sockaddr*>(&remote_addr_),
                 sizeof(remote_addr_)) != 0)
    {
        std::fprintf(stderr, "[SocketAppender] connect() to %s:%d failed: %s\n",
                     host_.c_str(), port_, std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

auto SocketAppender::ensureConnected_() -> int
{
    if(sock_fd >= 0)
        return sock_fd;

    // 尝试重连(简单退避：固定间隔)
    sock_fd = connectTcp_();
    if(sock_fd < 0)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds{reconnect_interval_ms_}
        );
    }
    return sock_fd;
}

auto SocketAppender::sendTcp_(const std::string& data) -> bool
{
    int fd = ensureConnected_();
    if(fd < 0)
        return false;

    // 当前发送游标/指针
    const char* ptr     = data.c_str();
    // 剩余代发字节数
    ssize_t     left    = static_cast<ssize_t>(data.size());

    while(left > 0)
    {
        ssize_t sent = ::send(fd, ptr, static_cast<size_t>(left), MSG_NOSIGNAL);
        // 发送失败
        if(sent <= 0)
        {
            // 连接断开,关闭fd,下次重连
            ::close(sock_fd);
            sock_fd = -1;
            return false;
        }
        // 发送成功，更新游标/指针
        ptr     += sent;
        left    -= sent;
    }
    return true;
}

// UDP: 直接 sendto
auto SocketAppender::sendUdp_(const std::string& data) -> bool
{
    if(sock_fd < 0)
        return false;

    ssize_t sent = ::sendto(
        sock_fd,
        data.c_str(),
        data.size(),
        0,
        reinterpret_cast<const struct sockaddr*> (&remote_addr_),
        sizeof(remote_addr_)
    );
    return sent == static_cast<ssize_t>(data.size());
}
