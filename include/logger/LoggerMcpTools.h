#pragma once

#include "logger/LoggerConfig.h"
#include "logger/LogLevel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace LoggerMcpTools {

struct LoggerMetrics {
    std::string logger_name;
    std::string level;
    bool exists = false;
    bool is_async = false;
    bool running = false;
    size_t event_count = 0;
    int flush_interval = 0;
    size_t max_pending_buffers = 0;
    size_t current_buffer_count = 0;
    size_t current_buffer_available = 0;
    size_t pending_buffers = 0;
    uint64_t accepted_events = 0;
    uint64_t dropped_events = 0;
    uint64_t written_events = 0;
};

// 把字符串level转成LogLevel。查找logger。设置logger级别。返回bool表示成功
auto logger_set_level(const std::string& logger_name, const std::string& level) -> bool;
// 重新加载配置文件。返回bool表示成功
auto logger_reload_config(const std::string& config_file) -> bool;
// 打开日志文件，使用std::queue 保存最后N行
auto logger_tail_file(const std::string& file_path, size_t lines = 100) -> std::vector<std::string>;
// 查找如果是普通logger，获取是否存在和当前级别。如果是AsyncLogger。进一步获取Async metrics。
auto logger_get_metrics(const std::string& logger_name) -> LoggerMetrics;

auto to_json(const LoggerMetrics& metrics) -> std::string;

} // namespace LoggerMcpTools
