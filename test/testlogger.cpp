#include "logger/Logger.h"
#include "logger/AsyncLogger.h"
#include "logger/LoggerAppender.h"
#include "logger/AppenderProxy.hpp"
#include "logger/LogFormatter.h"
#include "common/alias.h"
#include <iostream>

int main() {
    std::cout << "========== 日志系统极简测试 ==========\n";
    auto sync_logger = std::make_shared<Logger>();

    sync_logger->addAppender(std::make_shared<AppenderProxy<RollingFileAppender>>(LogFormatter{},"sync_log.txt", 1_kb));

    uint32_t tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    auto now_t = SystemClock::to_time_t(SystemClock::now());
    
    // 构造事件并打印
    LogEvent event{"TestLogger", LogLevel::INFO, 0, tid, "Main", now_t, 0};
    event.print("Hello Cotton Log System! Random: {}", 42);
    
    sync_logger->log(event);

    std::cout << "测试完成，请查看 sync_log.txt 是否生成！\n";
    return 0;
}