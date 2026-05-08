#pragma once

#include "common/alias.h"
#include "common/singleton.hpp"
#include "logger/LoggerConfig.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#define GET_ROOT_LOGGER() LoggerMgr::GetInstance().getRoot()
#define GET_LOGGER_BY_NAME(name) LoggerMgr::GetInstance().getLogger(name)

class Logger;
class AsyncLogger;
class LoggerManager;

using LoggerMgr = Cot::Singleton<LoggerManager>;

class LoggerManager {
public:
    LoggerManager();
    void init_();

    auto getLogger(std::string_view logger_name) -> Sptr<Logger>;
    auto findLogger(std::string_view logger_name) const -> Sptr<Logger>;
    auto findAsyncLogger(std::string_view logger_name) const -> Sptr<AsyncLogger>;
    auto getRoot() -> Sptr<Logger> { return root_; }

    // 对外API：根据配置创建/获取异步日志器
    auto getOrCreateAsyncLogger(const LoggerConfig& config) -> Sptr<AsyncLogger>;
    // 对外API：从 JSON 配置文件创建/获取异步日志器
    auto getOrCreateAsyncLoggerFromFile(const std::string& config_file) -> Sptr<AsyncLogger>;
    auto reloadAsyncLoggerFromFile(const std::string& config_file) -> Sptr<AsyncLogger>;

private:
    auto configureAsyncLogger_(const Sptr<AsyncLogger>& logger, const LoggerConfig& config) -> void;
    mutable std::mutex mtx_;
    Sptr<Logger> root_;
    std::unordered_map<std::string, Sptr<Logger>> loggers_;
    std::unordered_map<std::string, Sptr<AsyncLogger>> async_loggers_;
};
