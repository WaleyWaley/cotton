#include <memory>
#include <mutex>

#include "logger/AppenderProxy.hpp"
#include "logger/AsyncLogger.h"
#include "logger/Logger.h"
#include "logger/LoggerAppender.h"
#include "logger/LogManager.h"

namespace {

auto toSocketProtocol(LoggerConfig::AppenderConfig::SocketProtocol p) -> SocketAppender::Protocol {
    if (p == LoggerConfig::AppenderConfig::SocketProtocol::UDP) {
        return SocketAppender::Protocol::UDP;
    }
    return SocketAppender::Protocol::TCP;
}

auto buildAppender(const LoggerConfig::AppenderConfig& cfg) -> Sptr<AppenderFacade> {
    auto formatter = LogFormatter{cfg.pattern};

    switch (cfg.type) {
        case LoggerConfig::AppenderConfig::Type::Stdout:
            return std::make_shared<AppenderProxy<StdoutAppender>>(formatter);

        case LoggerConfig::AppenderConfig::Type::RollingFile:
            return std::make_shared<AppenderProxy<RollingFileAppender>>(
                formatter,
                cfg.file,
                cfg.max_file_size,
                Seconds{cfg.roll_interval_seconds});

        case LoggerConfig::AppenderConfig::Type::Socket:
            return std::make_shared<AppenderProxy<SocketAppender>>(
                formatter,
                cfg.host,
                cfg.port,
                toSocketProtocol(cfg.protocol),
                cfg.max_queue,
                cfg.reconnect_interval_ms);
    }

    return std::make_shared<AppenderProxy<StdoutAppender>>(formatter);
}

} // namespace

LoggerManager::LoggerManager()
    : root_{new Logger("root")}, loggers_{{"root", root_}} {
    root_->addAppender(std::make_shared<AppenderProxy<StdoutAppender>>());
    init_();
}

void LoggerManager::init_() {}

auto LoggerManager::getLogger(std::string_view logger_name) -> Sptr<Logger> {
    auto _ = std::lock_guard<std::mutex>{mtx_};

    if (auto it = loggers_.find(std::string(logger_name)); it != loggers_.end()) {
        return it->second;
    }

    auto logger = std::make_shared<Logger>(std::string{logger_name});
    loggers_.emplace(std::string{logger_name}, logger);
    return logger;
}

auto LoggerManager::getOrCreateAsyncLogger(const LoggerConfig& config) -> Sptr<AsyncLogger> {
    auto _ = std::lock_guard<std::mutex>{mtx_};

    const auto key = config.logger_name.empty() ? std::string{"root"} : config.logger_name;
    if (auto it = async_loggers_.find(key); it != async_loggers_.end()) {
        return it->second;
    }

    auto cfg = config;
    cfg.logger_name = key;
    auto logger = std::make_shared<AsyncLogger>(cfg);

    logger->clearAppender();
    for (const auto& app_cfg : cfg.appenders) {
        logger->addAppender(buildAppender(app_cfg));
    }

    async_loggers_.emplace(key, logger);
    loggers_[key] = logger;
    return logger;
}

auto LoggerManager::getOrCreateAsyncLoggerFromFile(const std::string& config_file) -> Sptr<AsyncLogger> {
    auto cfg = LoggerConfig::loadFromJsonFile(config_file);
    return getOrCreateAsyncLogger(cfg);
}
