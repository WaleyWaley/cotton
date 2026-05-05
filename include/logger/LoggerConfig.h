#pragma once

#include "logger/LogLevel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct LoggerConfig {
    struct AppenderConfig {
        enum class Type { Stdout, RollingFile, Socket };
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
    };

    size_t event_count = 64;
    int flush_interval = 3;
    size_t max_pending_buffers = 25;

    LogLevel level = LogLevel::INFO;
    std::string logger_name = "root";

    // 若未配置 appenders，则默认挂一个 stdout
    std::vector<AppenderConfig> appenders{};

    static auto loadFromJsonFile(const std::string& file_path) -> LoggerConfig;
};
