#include "include/LoggerAppender.h"
#include "common/alias.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sys/select.h>
#include <system_error>
#include <time.h>
/*=====================================LogAppender======================================*/

/*===========================StdoutAppender==================*/
void StdoutAppender::log(const LogFormatter& fmter, const LogEvent& event){
    fmter.format(std::cout, event);
}

/*===========================RollingFileAppenderAppender==================*/

RollingFileAppender::RollingFileAppender(std::string filename,
                                         size_t max_bytes,
                                         Seconds roll_interval)         
    : filename_{std::move(filename)},
    , basename_{std::filesystem::path{filename_}.filename().string()}
    , max_bytes_{max_bytes_}
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
        std::cerr << "错误代码(value): " << ec.value() << std::endl
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

auto RollingFileAppender::rollFile_() -> void{
    if(not filestream_.isopen())
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
    if(std::rename(filename_.c_str(), new_filename.c_str()) != 0)
    {
        throw std::system_error{std::error_code{errno, std::system_category()}, "重命名日志文件失败", + filename_ + "->" + new_filename};
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
    auto now = std::chrono::system_clock::time_point_cast<Seconds>(std::chrono::system_clock::now());
    auto zone_time = std::chrono::zoned_time<Seconds>{std::chrono::current_zone(), now};
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

auto RollingFileAppender::log(const LogFormatter& fmt, const LogEvent& event) -> void {
    // 1.加锁，确保线程安全
    auto _ = std::lock_guard{mutex_};

    // 2.检查是否需要滚动日志文件
    if(shouldRoll_())
    {
        rollFile_();
    }
    // 3.格式化并写入日志,
    auto total_size = fmter.format(filestream_, event);

    // 4.更新写入偏移量
    offset_ += total_size;

    // 5.处理 Flush 策略
    flush_count_++;

    // 6. 检查是否需要 Flush
    auto now = Clock::now();

    // 检查时间间隔
    auto time_to_flush = (now - last_flush_time_) >= c_flush_seconds;

    // 检查写入次数
    bool count_to_flush = flush_count_ >= c_flush_max_appends;

    if(time_to_flush || count_to_flush)
    {
        // 调用 std::ostream::flush() 将数据从 C++ 缓冲区推送到操作系统/ 
        filestream_.flush();
        // 重置状态
        last_flush_time_ = now;
        flush_count_ = 0;
    }
}