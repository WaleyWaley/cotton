#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#if __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#define COTTON_HAS_MYSQL_CLIENT 1
#elif __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#define COTTON_HAS_MYSQL_CLIENT 1
#else
#define COTTON_HAS_MYSQL_CLIENT 0
#endif

#include "logger/AppenderProxy.hpp"
#include "logger/AsyncLogger.h"
#include "logger/Logger.h"
#include "logger/LoggerAppender.h"
#include "logger/LogManager.h"


/**  @brief 管理 root logger。
*    按名称获取或创建普通 logger。
*    查找 logger。
*    查找 async logger。
*    根据 LoggerConfig 创建 async logger。
*    从 JSON 文件加载 logger。
*    运行时重载 async logger 配置。
*    根据 appender config 构造具体 appender。
*/

namespace {

auto toSocketProtocol(LoggerConfig::AppenderConfig::SocketProtocol p) -> SocketAppender::Protocol {
    if (p == LoggerConfig::AppenderConfig::SocketProtocol::UDP) {
        return SocketAppender::Protocol::UDP;
    }
    return SocketAppender::Protocol::TCP;
}

#if COTTON_HAS_MYSQL_CLIENT
class MysqlExecutorState {
public:
    explicit MysqlExecutorState(const LoggerConfig::AppenderConfig& cfg)
        : host_(cfg.sql_host)
        , port_(cfg.sql_port)
        , user_(cfg.sql_user)
        , password_(cfg.sql_password)
        , database_(cfg.sql_database) {}

    ~MysqlExecutorState() {
        close_();
    }

    void execute(const std::string& sql) {
        const auto lock = std::lock_guard<std::mutex>{mutex_};
        ensureConnected_();

        if (mysql_query(conn_, sql.c_str()) == 0) {
            return;
        }

        if (isConnectionError_(mysql_errno(conn_))) {
            reconnect_();
            if (mysql_query(conn_, sql.c_str()) == 0) {
                return;
            }
        }

        const auto err = std::string{mysql_error(conn_)};
        throw std::runtime_error("mysql query failed: " + err);
    }

private:
    static auto isConnectionError_(unsigned int err) -> bool {
        return err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST || err == CR_CONN_HOST_ERROR;
    }

    void ensureConnected_() {
        if (conn_ != nullptr) {
            return;
        }
        connect_();
    }

    void reconnect_() {
        close_();
        connect_();
    }

    void connect_() {
        conn_ = mysql_init(nullptr);
        if (conn_ == nullptr) {
            throw std::runtime_error("mysql_init failed");
        }

        bool reconnect = true;
        mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);

        if (mysql_real_connect(
                conn_,
                host_.c_str(),
                user_.c_str(),
                password_.c_str(),
                database_.c_str(),
                static_cast<unsigned int>(port_),
                nullptr,
                0) == nullptr) {
            const auto err = std::string{mysql_error(conn_)};
            close_();
            throw std::runtime_error("mysql_real_connect failed: " + err);
        }
    }

    void close_() {
        if (conn_ != nullptr) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }

private:
    std::string host_;
    uint16_t port_;
    std::string user_;
    std::string password_;
    std::string database_;

    MYSQL* conn_ = nullptr;
    std::mutex mutex_;
};

auto buildMysqlExecutor(const LoggerConfig::AppenderConfig& cfg) -> SqlAppender::SqlExecutor {
    auto state = std::make_shared<MysqlExecutorState>(cfg);
    return [state](const std::string& sql) { state->execute(sql); };
}
#else
auto buildMysqlExecutor(const LoggerConfig::AppenderConfig&) -> SqlAppender::SqlExecutor {
    return [](const std::string&) {
        throw std::runtime_error{"MySQL client headers not found. Install default-libmysqlclient-dev or libmariadb-dev."};
    };
}
#endif


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

        case LoggerConfig::AppenderConfig::Type::Sql:
            return std::make_shared<AppenderProxy<SqlAppender>>(
                formatter,
                cfg.sql_table,
                buildMysqlExecutor(cfg),
                cfg.sql_batch_size,
                cfg.sql_flush_interval_ms);
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

// MCP函数
auto LoggerManager::getLogger(std::string_view logger_name) -> Sptr<Logger> {
    auto _ = std::lock_guard<std::mutex>{mtx_};

    if (auto it = loggers_.find(std::string(logger_name)); it != loggers_.end()) {
        return it->second;
    }

    auto logger = std::make_shared<Logger>(std::string{logger_name});
    loggers_.emplace(std::string{logger_name}, logger);
    return logger;
}

auto LoggerManager::findLogger(std::string_view logger_name) const -> Sptr<Logger> {
    auto _ = std::lock_guard<std::mutex>{mtx_};
    if (auto it = loggers_.find(std::string{logger_name}); it != loggers_.end()) {
        return it->second;
    }
    return nullptr;
}

auto LoggerManager::findAsyncLogger(std::string_view logger_name) const -> Sptr<AsyncLogger> {
    auto _ = std::lock_guard<std::mutex>{mtx_};
    if (auto it = async_loggers_.find(std::string{logger_name}); it != async_loggers_.end()) {
        return it->second;
    }
    return nullptr;
}
// MCP函数结束

auto LoggerManager::configureAsyncLogger_(const Sptr<AsyncLogger>& logger, const LoggerConfig& config) -> void {
    logger->setLogLevel(config.level);
    logger->clearAppender();
    for (const auto& app_cfg : config.appenders) {
        logger->addAppender(buildAppender(app_cfg));
    }
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

    configureAsyncLogger_(logger, cfg);

    async_loggers_.emplace(key, logger);
    loggers_[key] = logger;
    return logger;
}

auto LoggerManager::getOrCreateAsyncLoggerFromFile(const std::string& config_file) -> Sptr<AsyncLogger> {
    auto cfg = LoggerConfig::loadFromJsonFile(config_file);
    return getOrCreateAsyncLogger(cfg);
}

// 配置热重载
auto LoggerManager::reloadAsyncLoggerFromFile(const std::string& config_file) -> Sptr<AsyncLogger> {
    auto cfg = LoggerConfig::loadFromJsonFile(config_file);

    std::lock_guard<std::mutex> lock(mtx_);

    const auto key = cfg.logger_name.empty() ? std::string{"root"} : cfg.logger_name;
    cfg.logger_name = key;

    if (auto it = async_loggers_.find(key); it != async_loggers_.end()) {
        configureAsyncLogger_(it->second, cfg);
        loggers_[key] = it->second;
        return it->second;
    }

    auto logger = std::make_shared<AsyncLogger>(cfg);
    configureAsyncLogger_(logger, cfg);
    async_loggers_[key] = logger;
    loggers_[key] = logger;
    return logger;
}

