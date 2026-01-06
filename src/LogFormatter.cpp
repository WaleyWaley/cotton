#include "include/LogFormatter.h"
#include <string_view>

/*默认格式：%%d{%%Y-%%m-%%d %%H:%%M:%%S}%%T%%t%%T%%N%%T%%F%%T[%%p]%%T[%%c]%%T%%f:%%l%%T%%m%%n */

void LogFormatter::LogFormatter_init() {
    // 定义结果结构
    struct ParseItem {
        std::string str;
        std::string fmt;
        int type;               // 0:普通字符          1: 格式化项
    }

    std::vector<ParseItem> parse_items;

    // 避用试图免拷贝
    std::string_view pattern = pattern_; 

    // 记录解析到哪里
    size_t cursor = 0;

    while(cursor < pattern.length()) {

        // 从 cursor后开始找
        size_t percent_pos = pattern.find('%', cursor);

        // --- 情况A：没找到 '%' ---
        // 说明剩下的全都是普通字符串
        if(percent_pos == std::string_view::npos){
            parse_items.emplace_back(ParseItem(std::string(pattern.substr(cursor)),"", 0));
            break;  // 解析结束
        }

        // --- 情况B：找到了 '%' ---


        // 找到了'%'
        // 先把'%'之前的普通字符串存起来。
        if(percent_pos > cursor){
            parse_items.emplace_back(ParseItem(std::string(pattern.substr(cursor, percent_pos - cursor)),"", 0));   
        }

        // 移动cursor到%位置
        cursor = percent_pos;

        // 检查是不是%%
        if(cursor+1<pattern.length() and pattern[percent_pos+1] == '%'){
            parse_items.emplace_back(ParseItem("%","", 0));
            cursor += 2; //跳过两个%
            continue;
        } 

        // --- 情况C：解析格式符 (例如 %d 或 %d{...}) ---
        
        // 既然不是 %%，那这个 % 肯定是格式引导符
        // 只有 % 后面没有字符了 (例如字符串结尾是 %)
        if(cursor+1 >= pattern.length()) {
            parse_items.emplace_back(ParseItem("<error_percent>","", 0));
            break;
        }

        // 1. 提取格式化标识符 (通常是 % 后面的字母，如 d, m, n)
        // 我们假设格式符紧跟在 % 后面，直到遇到 { 或者非字母
        size_t fmt_id_start = cursor+1;
        size_t fmt_id_end = fmt_id_start;

        while(fmt_id_end < pattern.length() and isalpha(pattern[fmt_id_end])){
            fmt_id_end++;
        }

        // 拿到标识符，例如 "d"
        std::string str_val = std::string(pattern.substr(fmt_id_start, fmt_id_end - fmt_id_start));
        std::string fmt_val = "";

        // 更新到cursor到标识符后面
        cursor = fmt_id_end;

        if(cursor < pattern.length() and pattern[cursor] == '{') {
            // 查找闭合的}
            size_t brace_end = pattern.find('}', cursor);
            if(brace_end != std::string_view::npos) {
                fmt_val = std::string(pattern.substr(cursor+1, brace_end - cursor - 1));
                cursor = brace_end + 1;
            }else{
                // 未找到闭合的 }，视为错误
                std::cout << "Pattern parse error: missing closing brace '}'" << std::endl;
            }
        }
    }

}