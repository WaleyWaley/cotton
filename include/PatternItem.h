#include "include/LogFormatter.h"

// 抽象基类，所有具体的PatternItem都继承自它
class PatterItem{
public:
    using Sptr = std::shared_ptr<PatternItem>;
    virtual ~PatternItem() = default;
    // 核心接口：接收输出流和事件
    virtual void format(std::ostream& os, LogEvent::Sptr event) = 0;
};

// 普通字符串 Item 处理(ParseItem type=0的情况)
class StringFormatItem: public PatternItem{
public:
    StringFormatItem(std::string& str) : str_(move(str)){}
    void format(std::ostream& os, LogEvent::Sptr event) override {
        os << str_; // 直接原样输出
    }
private:
    std::string str_;
}:

// 具体的格式化 Item 比如消息内容
class MessageFormatItem: public PatternItem{
public:
    MessageFormatItem(const std::string& fmt = "") {}
    void format(std::ostream& os, LogEvent::Sptr event) override {
        os << event->getContent();
    }
};

class LevelFormatItem: public PatternItem{
public:
    LevelFormatItem(const std::string& fmt = "") {}
    void format(std::ostream& os, LogEvent::Sptr event) override {
        os << LogLevel::LevelToString(event->getLevel());
    }

};