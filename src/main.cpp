#include "common/LogMacros.h"
#include "logger/LoggerMcpTools.h"
#include "logger/LogManager.h"
#include <chrono>
#include <iostream>
#include <thread>
int main() {
    auto logger = LoggerMgr::GetInstance().getOrCreateAsyncLoggerFromFile("logger_config.json");
    logger->start();
    LOG_INFO(logger, "before set level: info message");
    LOG_DEBUG(logger, "before set level: debug message should not appear if level is INFO");
    LoggerMcpTools::logger_set_level("app_async", "DEBUG");
    LOG_DEBUG(logger, "after set level: debug message should appear");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto metrics = LoggerMcpTools::logger_get_metrics("app_async");
    std::cout << LoggerMcpTools::to_json(metrics) << std::endl;
    try {
        auto lines = LoggerMcpTools::logger_tail_file("json_async.log", 10);
        for (const auto& line : lines) {
            std::cout << line << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "tail failed: " << ex.what() << '\n';
    }
    LoggerMcpTools::logger_reload_config("logger_config.json");
    logger->stop();
    return 0;
}

g++ -std=c++20 \
  -I. \
  -Iinclude \
  src/main.cpp \
  src/LoggerMcpTools.cpp \
  src/LogManager.cpp \
  src/Logger.cpp \
  src/LoggerConfig.cpp \
  src/LoggerAppender.cpp \
  src/LogFormatter.cpp \
  src/LogEvent.cpp \
  src/LogLevel.cpp \
  src/PatternItemImpl.cpp \
  -o cotton_demo