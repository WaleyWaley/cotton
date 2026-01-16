#include "logger/LogEvent.h"
#include "logger/LogFormatter.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <unordered_map>
#include <functional>

namespace{

enum class ParseState{
    NORMAL,
    PATTERN,
    SUBPATTERN
};

}   //namespace

class PatternItemFacade;

// 1. 无参工厂：生产不需要参数的 Item (如 %m 消息, %n 换行)
using ItemFactoryFunc = std::function<Sptr<PatternItemFacade>()>;

// 注册无参工厂函数, 如果这个函数被调用，就会返回一个映射表，表里存着各种格式符对应的工厂函数
// 比如 "%m" -> 生产 MessageItem 的工厂函数
auto RegisterItemFactoryFunc() -> std::unordered_map<std::string, ItemFactoryFunc>;

// 2. 有参工厂：生产需要字符串参数的 Item (如 %d{...} 日期, 普通字符串)
// 格式：std::function<返回值类型(参数类型1, 参数类型2, ...)> 变量名;
// 这个函数类型表示一个函数，它接受一个 std::string 参数，并返回一个智能指针，指向 PatternItemFacade 类型的对象。
using StatusItemFactoryFunc() = std::function<Sptr<PatternItemFacade>(std::string)>;

// 注册有参工厂函数
// 这个函数被调用时，会返回一个映射表，表里存着各种格式符对应的工厂函数
// 比如 "%d" -> 生产 DateItem 的工厂函数
auto RegisterStatusItemFactoryFunc() -> std::unordered_map<std::string, StatusItemFactoryFunc>;

auto LogFormatter::startParse_ -> void ()
{
    // 最后的小括号意思是立即调用这个lambda表达式，并将返回值赋值给变量, 用lambda表达式初始化静态变量
    // 静态局部变量初始化 只会执行一次，第一次调用时初始化，后续调用时直接使用已初始化的值
    static auto s_ProduceFuncMap1 = []() -> std::unordered_map<std::string, ItemFactoryFunc>{
        return RegisterItemFactoryFunc();
    }();

    assert(s_ProduceFuncMap1.size() != 0);

    auto normal_str = std::string{};
    auto state = ParseState::NORMAL;

    for(auto i = size_t {0}; i < pattern_.size();)
    {
        switch(state)
        {
            // 作用：把 % 之间的所有字符当作普通字符串处理。
            case ParseState::NORMAL: {
                // 读取直到遇到 '%'
                if(pattern_[i] != '%')
                {
                    // 遇到 %，先保存之前的普通字符串
                    if(!normal_str.empty())
                    {
                        auto produce_func = s_ProduceFuncMap2["str"]    // 查表找 StringItem
                        pattern_items_.push_back(produce_func(std::move(normal_str)));
                        normal_str.clear();
                    }
                }
                state = ParseState::PATTERN;
                i++; // 消耗掉 '%' 这个字符，进入下一个状态
                else
                {
                    // 普通字符，加入缓冲区
                    normal_str.push_back(pattern_[i]);
                    i++;    // 消耗掉普通字符
                }
                break;
            } 

            case ParseState::PATTERN: {
                // 获取 '%' 后面的那个字符
                auto c = pattern_[i];
                // 1. 检查是不是子模式 (例如 %d{...})
                if (i+1 < pattern_.size() and pattern_[i+1] == '{')
                {
                    state = ParseState::SUBPATTERN; // 切换状态去读取括号内的内容
                    i += 2; // 跳过格式符 (如'd') 和 '{' 两个字符
                    break;
                }
                
                // 2. 这是一个普通的格式符号 (例如 %m, %p)，查找 Map
                if(s_ProduceFuncMap1.find(std::string(1,c)) == s_ProduceFuncMap1.end())
                {
                    // 没找到，按普通字符串处理
                    normal_str.push_back('%');
                    normal_str.push_back(c);
                    state = ParseState::NORMAL;
                    i++; // 消耗掉这个无效的格式符
                    break;
                }
                // string(1,c)为什么要这么写？ 通常是为了把一个原本是单引号的字符（char），转换成双引号的字符串（string），以便和其他字符串进行拼接。
                // 查表生产，找到了，加入列表
                auto produce_func = s_ProduceFuncMap1[std::string(1,c)];
                pattern_items_.push_back(produce_func());
                state = ParseState::NORMAL; // 活干完了，切回普通模式
                i++; // 消耗掉这个格式符
                break;
            }

            case ParseState::SUBPATTERN: {
                // 回头找那个格式符。
                // i此时在 '{' 的后面。pattern_[i-2] 大概率是那个 'd'。
                // 此时 i 指向 '{' 后面的第一个字符。
                // 之前的结构是 "%d{", 所以格式符是 pattern_[i-2]
                // 注意：如果之前 i+=2 的逻辑变了，这里也要变。
                auto escape_c = pattern_[i-2];

                auto sub_pattern = std::string{};

                // 读取 {} 里面的内容
                while(i<pattern_.size() and pattern_[i] != '}')
                {
                    sub_pattern.push_back(pattern_[i]);
                    i++;
                }
                if(i >= pattern_.size())
                {
                    // @todo exception
                    std::cerr << "[ERROR] LogFormatter parse error: missing '}'" << std::endl;
                    error_ = true;
                    return;
                }
                
                // 此时 pattern_[i] 是 '}'
                // 查表生产
                auto produce_func = s_ProduceFuncMap2[std::string(1, escape_c)];
                pattern_items_.push_back(produce_func(sub_pattern));
                state = ParseState::NORMAL;
                i++;    // 消耗掉 '}'
                break;
            }
        }
    }
    // 结束后，检查状态
    assert(!error_);
    assert(state == ParseState::NORMAL);

    if(not normal_str.empty())
    {
        auto produce_func = s_ProduceFuncMap2["str"];
        pattern_items_.push_back(produce_func(std::move(normal_str)));
    }

}

auto LogFormatter::format(std::ostream& os, const LogEvent& event) const -> size_t {
    size_t total_size = 0;
    std::ranges::for_each(pattern_items_,[&os, &event, &total_size](const Sptr<PatternItemFacade> item) -> void 
    // item->format(...) 是多态调用！
    // 不同的 item 会把自己负责的内容写到 os 里，并返回写入的长度。
    {
        // 如果 item 是日期项，它就往 os 写入 "2023-10-27"。
        // 如果 item 是消息项，它就往 os 写入 "User login success"。
        // 这就体现了 多态 的威力：不管你是什么项，我统一调 format 接口。
        total_size += item->format(os, event);
    });
    return total_size;
}

auto LogFormatter::format(const LogEvent& event) const -> std::string 
{
    auto ss = std::ostringstream{};
    std::ranges::for_each(pattern_items_,[&ss, &event](const Sptr<PatternItemFacade> item) -> void 
    {
        item->format(ss, event);
    });
    return ss.str();
}