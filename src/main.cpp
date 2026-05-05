#include "common/LogMacros.h"
#include "logger/AppenderFacade.h"
#include "logger/AppenderProxy.hpp"
#include "logger/LogEvent.h"
#include "logger/LoggerAppender.h"
#include "logger/LoggerConfig.h"
#include "logger/LogManager.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

class SimpleFileAppender final : public AppenderFacade {
public:
    explicit SimpleFileAppender(std::string file_name) : out_(std::move(file_name), std::ios::app) {}

    void log(const LogEvent& event) override {
        const auto lock = std::lock_guard<std::mutex>{mutex_};
        out_ << "[" << LevelToString(event.getLevel()) << "] "
             << event.getLoggerName() << " "
             << event.getFilename() << ":" << event.getLine() << " "
             << event.getContent() << '\n';
    }

private:
    std::mutex mutex_;
    std::ofstream out_;
};

int main() {
    // 方式1：代码内构造配置
    LoggerConfig code_cfg;
    code_cfg.logger_name = "app_async";
    code_cfg.level = LogLevel::INFO;
    code_cfg.event_count = 128;
    code_cfg.flush_interval = 2;
    code_cfg.max_pending_buffers = 32;

    auto async_logger = LoggerMgr::GetInstance().getOrCreateAsyncLogger(code_cfg);
    async_logger->addAppender(std::make_shared<SimpleFileAppender>("async_main.log"));
    async_logger->addAppender(std::make_shared<AppenderProxy<StdoutAppender>>());
    async_logger->start();

    // 统一宏：AsyncLogger + shared_ptr
    for (int i = 0; i < 10; ++i) {
        LOG_INFO(async_logger, "[async] index={}", i);
    }

    // 方式2：从 JSON 文件加载配置（如果文件存在）
    try {
        auto file_logger = LoggerMgr::GetInstance().getOrCreateAsyncLoggerFromFile("logger_config.json");
        file_logger->addAppender(std::make_shared<AppenderProxy<StdoutAppender>>());
        file_logger->start();
        LOG_WARN(file_logger, "logger loaded from logger_config.json");
        file_logger->stop();
    } catch (...) {
        // 没有配置文件时忽略，保持示例可直接运行
    }

    // 统一宏：同步 Logger 也可用
    auto sync_logger = std::make_shared<Logger>("sync_logger");
    sync_logger->addAppender(std::make_shared<AppenderProxy<StdoutAppender>>());
    sync_logger->setLogLevel(LogLevel::DEBUG);
    LOG_DEBUG(sync_logger, "[sync] debug message");
    LOG_ERROR(sync_logger.get(), "[sync/raw pointer] error message");

    std::this_thread::sleep_for(std::chrono::seconds{3});
    async_logger->stop();

    std::cout << "done. check async_main.log" << std::endl;
    return 0;
}
