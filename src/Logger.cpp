#include "logger/Logger.h"
#include "logger/AppenderFacade.h"

void Logger::addAppender(Sptr<AppenderFacade> appender) {
    if (appender) {
        appenders_.push_back(std::move(appender));
    }
}

void Logger::delAppender(Sptr<AppenderFacade> appender) {
    if (!appender) {
        return;
    }

    for (auto it = appenders_.begin(); it != appenders_.end();) {
        if (*it == appender) {
            it = appenders_.erase(it);
        } else {
            ++it;
        }
    }
}

void Logger::clearAppender() { appenders_.clear(); }

void Logger::log(const LogEvent& event) const {
    if (!isLevelEnable(event.getLevel())) {
        return;
    }

    // 让 logger 检查这条 event 的级别； 如果允许输出，就把 event 分发给 logger 挂载的所有输出端。
    /*
        这里的 appender 就是输出端，比如：
        控制台 StdoutAppender
        滚动文件 RollingFileAppender
        网络 SocketAppender
        数据库 SqlAppender
    */
    for (const auto& appender : appenders_) {
        if (appender) {
            // appender是AppenderFacade
            appender->log(event);
        }
    }
}
