#pragma once

#include "logger/LogLevel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct LoggerConfig {

    // 输出端配置
    struct AppenderConfig { 
        enum class Type { Stdout, RollingFile, Socket, Sql };
        enum class SocketProtocol { TCP, UDP };

        Type type = Type::Stdout;
        std::string pattern{};

        // rolling_file
        std::string file = "app.log";
        size_t max_file_size = 64 * 1024 * 1024;
        uint32_t roll_interval_seconds = 24 * 60 * 60;

        // socket
        std::string host = "127.0.0.1";
        uint16_t port = 9000;
        SocketProtocol protocol = SocketProtocol::TCP;
        size_t max_queue = 4096;
        uint32_t reconnect_interval_ms = 3000;

        // sql (MySQL CLI)
        std::string sql_host = "127.0.0.1";
        uint16_t sql_port = 3306;
        std::string sql_user = "root";
        std::string sql_password{};
        std::string sql_database = "test";
        std::string sql_table = "logs";
        size_t sql_batch_size = 64;
        uint32_t sql_flush_interval_ms = 1000;
    };

    size_t event_count = 64;    // 每个异步缓冲区最多放多少条事件
    int flush_interval = 3;     // 后台线程定时flush间隔
    size_t max_pending_buffers = 25;    // 最大待写缓冲区数量, 防止无限积压

    LogLevel level = LogLevel::INFO;
    std::string logger_name = "root";           // logger名称，也可作为业务标识

    // 若未配置 appenders，则默认挂一个 stdout
    std::vector<AppenderConfig> appenders{};    // 输出端列表

    static auto loadFromJsonFile(const std::string& file_path) -> LoggerConfig;
};
