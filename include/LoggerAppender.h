#pragma once
#include "LogFormatter.h"
#include "LogEvent.h"
#include <iostream>
#include <memory>
#include <fstream>

class LogEvent;
class LogFormatter;

/*=================================================LogAppender==========================================*/

/**
 *   @brief Appender基类
 */
class LogAppender{
public:
    typedef std::shared_ptr<LogAppender> Sptr;
    // 输出函数为纯虚函数，因为具体实现各个子类不一样，由各个子类自己决定
    virtual void log(const LogFormatter& fmter, const LogEvent& event) = 0;
    // 子类如果像定义自己的析构就用子类的，否则用父类的
    virtual ~LogAppender() {}
};

/**
 *   @brief 输出到控制台的Appender
 */
class StdoutAppender : public LogAppender{
public:
    /**
    *   @brief 输出到控制台的Appender
    *   @todo  完善log函数
    */
    typedef std::shared_ptr<StdoutAppender> Sptr;

    void log(const LogFormatter& fmter, const LogEvent& event) override;
};

/**
 *   @brief 输出到文件的Appender
 */
class FileAppender : public LogAppender{
public:
    typedef std::shared_ptr<FileAppender> Sptr;

    FileAppender(std::string filename): filename_(move(filename)){ reopen();}   // 刚创建时尝试打开文件

    ~FileAppender(){
        if(filestream_.is_open()){
            filestream_.close();
        }
    }

    bool reopen();
    
    void log(const LogFormatter& fmter, const LogEvent& event) override;

private:
    std::string filename_;              // 文件名
    std::ofstream filestream_;          // 文件输出流
};

