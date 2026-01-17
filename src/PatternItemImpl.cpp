#include "common/alias.h"
#include "logger/LogEvent.h"
#include "logger/PatternItemFacade.h"
#include "logger/PatternItemProxy.h"

#include  <cstddef>
#include <functional>
#include <sys/types.h>
#include <string.h>
#include <chrono>
#include <format>
/* ======================================FormatterItem==============================*/
class FunctionNameFormatItem {
public:
    static auto format(std::ostream&os, const LogEvent& event) -> size_t 
    {
        std::streampos start = os.tellp();
        os << event.getLoggerName();
        return static_cast<size_t>(os.tellp() - start);
    }
};

/**
 * @brief 消息format
 */

class MessageFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t
    {
        std::streampos start = os.tellp();
        os << event.getLoggerName();
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief 日志级别format */
class LevelFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        std::streampos start = os.tellp();
        os << LevelToString(event.getLevel());
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief 耗时format */
class ElapseFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        std::streampos start = os.tellp();
        os << std::to_string(event.getElapse());
        return cast_static<size_t>(os.tellp() - start);
    }
};

/** @brief 日志器名字format */
class NameFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        std::streampos start = os.tellp();
        os << event.getLoggerName();
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief 线程ID format */
class ThreadIdFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        // 记录当前写指针的位置，计算写入的长度
        std::streampos start = os.tellp();
        // 写入线程ID
        os << event.getThreadId();
        // 计算并返回写入的长度
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief 协程ID format */
class FiberIdFormatItem{
public:
    static auto format(std::ostream&os, const LogEvent& event) -> size_t {
        std::streampos start = os.tellp();
        os << event.getFiberId();
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief 线程名称 format */
class ThreadNameFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        std::streampos start = os.tellp();
        os << event.getThreadName();
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief 换行符 format */
class NewLineFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        os.put('\n');
        return 1;
    }
};

/** @brief 文件名 format */
class FilenameFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        std::streampos start = os.tellp();
        os << event.getFileName();
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief 行号 format */
class LineFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        std::streampos start = os.tellp();
        os << event.getLineNumber();
        return static_cast<size_t>(os.tellp() - start);
    }
};

/** @brief tab format */
class TabFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        os.put('\t');
        return 1;
    }
};

/** @brief % format */
class PercentSignFormatItem{
public:
    static auto format(std::ostream& os, const LogEvent& event) -> size_t {
        os.put('%');
        return 1;
    }
};

/*===================================FormatItem with Status======================= */
/** @brief 时间 format*/
class DateTimeFormatItem{
public:
    explicit DataTimeFormatItem(std::string data_format) : date_format_(move(data_format)){}

    auto format(std::ostream& os, const LogEvent& event)-> size_t
    {
        // 1. 讲 time_t 转换为 chorno 的 time_point
        auto tp = SystemClock::from_time_t(event.getTime());
        
        // 2. 创建带时区的时间对象
        auto zt = ZoneTime(std::chorno::current_zone(), tp);

        // 3. 准备栈缓冲区（避免 new/malloc ）
        char buf[64];

        // 4. 格式化
        // 注意：C++20 format 的语法是 "{:%Y-%m-%d %H:%M:%S}"
        // 如果 date_format_ 是 "%Y-%m-%d" 这种旧格式，可能需要调整，或者手动拼装 string_view
        // 这里假设 date_format_ 已经适配为 C++20 格式，或者我们直接硬编码标准格式
        // format_to_n 防止缓冲区溢出
        auto result = std::format_to_n(buf, sizeof(buf)-1, "{:%Y-%m-%d %H:%M:%S}", zt);

        // result.out 是指向结束位置的迭代器，result.size 是本该写入的长度
        size_t length = result.size;

        // 确保末尾有 \0 (虽然 os.write 不需要，但为了安全习惯)
        // 这里的 result.out 指向了写入内容的末尾
        // 比如写入了 "2023-10-27"，它就指向 '7' 后面那个位置
        *result.out = '\0';

        os.write(buf, length);

        return length;
    }

    // 左值
    auto getSubpattern() & -> const std::string&
    {
        return date_format_;
    }

    // 右值
    auto getSubpattern() && -> std::string
    {
        return std::move(date_format_);
    }

private:
    std::string date_format_ =  "%Y-%m-%d %H:%M:%S";
};

class StringFormatItem{
public:
    explicit StringFormatItem(std::string str) : str_(std::move(str)){}

    auto format(std::ostream& os, const LogEvent& event) -> size_t
    {
        std::streampos start = os.tell();
        os << str;
        return static_cast<size_t>(os.tellp() - start);
    }
private:
    std::string str_;
}

using ItemFactoryFunc = std::function<Sptr<PatternItemFacade>()>;
auto RegisterItemFactoryFunc() -> std::unordered_map<std::string, ItemFactoryFunc>{
    auto func_map = std::unordered_map<std::string, ItemFactory>{
#define XX(str, ItemType) \
        { \
            #str,[]() -> Sptr<PatternItemFacade>{return Sptr<PatternItemFacade>{new PatternItemProxy<ItemType>{}}}; \
        }
        XX(m, MessageFormatItem),       // m:消息
        XX(p, levelFormatItem),         // p:日志级别
        XX(c, NameFormatItem),          // c:日志器名称
        XX(r, ElapseFormatItem),        // r:累计毫秒数
        XX(f, FilenameFormatItem),      // f:文件名
        XX(l, LineFormatItem),          // l:行号
        XX(t, ThreadIdFormatItem),      // t:线程号
        XX(F, FiberIdFormatItem),       // F:协程号
        XX(N, ThreadNameFormatItem),    // N:线程名称
        XX(T, TabFormatItem),           // T:制表符
        XX(n, NewLineFormatItem),       // n:换行符
        XX(%, PercentSignFormatItem),   // %:百分号
        XX(v, FunctionNameFormatItem),  //v:函数名XX()
#undef XX
    };
    return func_map;
}

using StatusItemFactoryFunc = std::function<Sptr<PatternItemFacade>(std::string)>;

auto RegisterStatusItemFactoryFunc() -> std::unordered_map<std::string, StatusItemFactoryFunc>{
    return std::unordered_map<std::string, StatusItemFactoryFunc>{
#define XX(str, ItemType) \
        { \
            #str, [](std::string sub_pattern) -> Sptr<PatternItemFacade>{ \
                // std::make_shared<...>(...)在堆内存（Heap）里分配一块空间，创建这个对象。返回一个智能指针。
                return std::static_pointer_cast<PatternItemFacade>(std::make_shared<PatternItemProxy<ItemType>>(sub_pattern)); \
            } \
        }
            XX(d, DateTimeFormatItem),
            XX(str, StringFormatItem)
#undef XX
    };
}

