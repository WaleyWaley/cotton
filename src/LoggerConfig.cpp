#include "logger/LoggerConfig.h"

#include "json-rpc/json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace {

using json = nlohmann::json;

auto toLower(std::string s) -> std::string {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

auto parseAppenderType(const std::string& s) -> LoggerConfig::AppenderConfig::Type {
    const auto v = toLower(s);
    if (v == "stdout") return LoggerConfig::AppenderConfig::Type::Stdout;
    if (v == "rolling_file" || v == "rollingfile") return LoggerConfig::AppenderConfig::Type::RollingFile;
    if (v == "socket") return LoggerConfig::AppenderConfig::Type::Socket;
    if (v == "sql" || v == "mysql") return LoggerConfig::AppenderConfig::Type::Sql;
    return LoggerConfig::AppenderConfig::Type::Stdout;
}

auto parseSocketProtocol(const std::string& s) -> LoggerConfig::AppenderConfig::SocketProtocol {
    const auto v = toLower(s);
    if (v == "udp") return LoggerConfig::AppenderConfig::SocketProtocol::UDP;
    return LoggerConfig::AppenderConfig::SocketProtocol::TCP;
}

auto normalizeConfig(LoggerConfig cfg) -> LoggerConfig {
    if (cfg.event_count == 0) cfg.event_count = 64;
    if (cfg.flush_interval <= 0) cfg.flush_interval = 3;
    if (cfg.max_pending_buffers < 2) cfg.max_pending_buffers = 2;
    if (cfg.logger_name.empty()) cfg.logger_name = "root";

    if (cfg.appenders.empty()) {
        cfg.appenders.push_back(LoggerConfig::AppenderConfig{});
    }

    for (auto& app : cfg.appenders) {
        if (app.pattern.empty()) {
            app.pattern = "%d{%Y-%m-%d %H:%M:%S} [%rms] %t%T%N%T%F%T[%p]%T[%c]%T[%f:%l]%T[%v]%T%m%n";
        }

        if (app.type == LoggerConfig::AppenderConfig::Type::RollingFile) {
            if (app.file.empty()) app.file = "app.log";
            if (app.max_file_size == 0) app.max_file_size = 64 * 1024 * 1024;
            if (app.roll_interval_seconds == 0) app.roll_interval_seconds = 24 * 60 * 60;
        }

        if (app.type == LoggerConfig::AppenderConfig::Type::Socket) {
            if (app.host.empty()) app.host = "127.0.0.1";
            if (app.port == 0) app.port = 9000;
            if (app.max_queue == 0) app.max_queue = 4096;
            if (app.reconnect_interval_ms == 0) app.reconnect_interval_ms = 3000;
        }

        if (app.type == LoggerConfig::AppenderConfig::Type::Sql) {
            if (app.sql_host.empty()) app.sql_host = "127.0.0.1";
            if (app.sql_port == 0) app.sql_port = 3306;
            if (app.sql_user.empty()) app.sql_user = "root";
            if (app.sql_database.empty()) app.sql_database = "test";
            if (app.sql_table.empty()) app.sql_table = "logs";
            if (app.sql_batch_size == 0) app.sql_batch_size = 64;
            if (app.sql_flush_interval_ms == 0) app.sql_flush_interval_ms = 1000;
        }
    }

    return cfg;
}

} // namespace

auto LoggerConfig::loadFromJsonFile(const std::string& file_path) -> LoggerConfig {
    std::ifstream in(file_path);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open logger config file: " + file_path);
    }

    json j;
    in >> j;

    LoggerConfig cfg;

    if (j.contains("event_count") && j.at("event_count").is_number_unsigned()) {
        cfg.event_count = j.at("event_count").get<size_t>();
    }
    if (j.contains("flush_interval") && j.at("flush_interval").is_number_integer()) {
        cfg.flush_interval = j.at("flush_interval").get<int>();
    }
    if (j.contains("max_pending_buffers") && j.at("max_pending_buffers").is_number_unsigned()) {
        cfg.max_pending_buffers = j.at("max_pending_buffers").get<size_t>();
    }
    if (j.contains("level") && j.at("level").is_string()) {
        cfg.level = StringToLogLevel(j.at("level").get<std::string>());
    }
    if (j.contains("logger_name") && j.at("logger_name").is_string()) {
        cfg.logger_name = j.at("logger_name").get<std::string>();
    }

    if (j.contains("appenders") && j.at("appenders").is_array()) {
        for (const auto& a : j.at("appenders")) {
            if (!a.is_object()) {
                continue;
            }

            AppenderConfig app;
            if (a.contains("type") && a.at("type").is_string()) {
                app.type = parseAppenderType(a.at("type").get<std::string>());
            }
            if (a.contains("pattern") && a.at("pattern").is_string()) {
                app.pattern = a.at("pattern").get<std::string>();
            }

            if (a.contains("file") && a.at("file").is_string()) {
                app.file = a.at("file").get<std::string>();
            }
            if (a.contains("max_file_size") && a.at("max_file_size").is_number_unsigned()) {
                app.max_file_size = a.at("max_file_size").get<size_t>();
            }
            if (a.contains("roll_interval_seconds") && a.at("roll_interval_seconds").is_number_unsigned()) {
                app.roll_interval_seconds = a.at("roll_interval_seconds").get<uint32_t>();
            }

            if (a.contains("host") && a.at("host").is_string()) {
                app.host = a.at("host").get<std::string>();
            }
            if (a.contains("port") && a.at("port").is_number_unsigned()) {
                app.port = a.at("port").get<uint16_t>();
            }
            if (a.contains("protocol") && a.at("protocol").is_string()) {
                app.protocol = parseSocketProtocol(a.at("protocol").get<std::string>());
            }
            if (a.contains("max_queue") && a.at("max_queue").is_number_unsigned()) {
                app.max_queue = a.at("max_queue").get<size_t>();
            }
            if (a.contains("reconnect_interval_ms") && a.at("reconnect_interval_ms").is_number_unsigned()) {
                app.reconnect_interval_ms = a.at("reconnect_interval_ms").get<uint32_t>();
            }

            if (a.contains("sql_host") && a.at("sql_host").is_string()) {
                app.sql_host = a.at("sql_host").get<std::string>();
            }
            if (a.contains("sql_port") && a.at("sql_port").is_number_unsigned()) {
                app.sql_port = a.at("sql_port").get<uint16_t>();
            }
            if (a.contains("sql_user") && a.at("sql_user").is_string()) {
                app.sql_user = a.at("sql_user").get<std::string>();
            }
            if (a.contains("sql_password") && a.at("sql_password").is_string()) {
                app.sql_password = a.at("sql_password").get<std::string>();
            }
            if (a.contains("sql_database") && a.at("sql_database").is_string()) {
                app.sql_database = a.at("sql_database").get<std::string>();
            }
            if (a.contains("sql_table") && a.at("sql_table").is_string()) {
                app.sql_table = a.at("sql_table").get<std::string>();
            }
            if (a.contains("sql_batch_size") && a.at("sql_batch_size").is_number_unsigned()) {
                app.sql_batch_size = a.at("sql_batch_size").get<size_t>();
            }
            if (a.contains("sql_flush_interval_ms") && a.at("sql_flush_interval_ms").is_number_unsigned()) {
                app.sql_flush_interval_ms = a.at("sql_flush_interval_ms").get<uint32_t>();
            }

            cfg.appenders.push_back(std::move(app));
        }
    }

    return normalizeConfig(std::move(cfg));
}
