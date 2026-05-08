#include "logger/LoggerMcpTools.h"

#include "logger/AsyncLogger.h"
#include "logger/Logger.h"
#include "logger/LogManager.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace LoggerMcpTools {
namespace {

auto jsonEscape(std::string_view text) -> std::string
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

auto normalizeLoggerName(const std::string& logger_name) -> std::string
{
    return logger_name.empty() ? std::string{"root"} : logger_name;
}

} // namespace

auto logger_set_level(const std::string& logger_name, const std::string& level) -> bool
{
    const auto parsed_level = StringToLogLevel(level);
    if (parsed_level == LogLevel::UNKNOW) {
        return false;
    }

    auto logger = LoggerMgr::GetInstance().findLogger(normalizeLoggerName(logger_name));
    if (!logger) {
        return false;
    }

    logger->setLogLevel(parsed_level);
    return true;
}

auto logger_reload_config(const std::string& config_file) -> bool
{
    try {
        LoggerMgr::GetInstance().reloadAsyncLoggerFromFile(config_file);
        return true;
    } catch (...) {
        return false;
    }
}

auto logger_tail_file(const std::string& file_path, size_t lines) -> std::vector<std::string>
{
    if (lines == 0) {
        return {};
    }

    auto in = std::ifstream{file_path};
    if (!in.is_open()) {
        throw std::runtime_error{"cannot open log file: " + file_path};
    }

    std::deque<std::string> ring;
    std::string line;
    while (std::getline(in, line)) {
        if (ring.size() == lines) {
            ring.pop_front();
        }
        ring.push_back(std::move(line));
    }

    return {std::make_move_iterator(ring.begin()), std::make_move_iterator(ring.end())};
}

auto logger_get_metrics(const std::string& logger_name) -> LoggerMetrics
{
    auto metrics = LoggerMetrics{};
    const auto name = normalizeLoggerName(logger_name);
    metrics.logger_name = name;

    auto logger = LoggerMgr::GetInstance().findLogger(name);
    if (!logger) {
        return metrics;
    }

    metrics.exists = true;
    metrics.level = std::string{LevelToString(logger->getLogLevel())};

    auto async_logger = LoggerMgr::GetInstance().findAsyncLogger(name);
    if (!async_logger) {
        return metrics;
    }

    const auto async_metrics = async_logger->getMetrics();
    metrics.is_async = true;
    metrics.running = async_metrics.running;
    metrics.event_count = async_metrics.event_count;
    metrics.flush_interval = async_metrics.flush_interval;
    metrics.max_pending_buffers = async_metrics.max_pending_buffers;
    metrics.current_buffer_count = async_metrics.current_buffer_count;
    metrics.current_buffer_available = async_metrics.current_buffer_available;
    metrics.pending_buffers = async_metrics.pending_buffers;
    metrics.accepted_events = async_metrics.accepted_events;
    metrics.dropped_events = async_metrics.dropped_events;
    metrics.written_events = async_metrics.written_events;
    return metrics;
}

auto to_json(const LoggerMetrics& metrics) -> std::string
{
    auto os = std::ostringstream{};
    os << '{'
       << "\"logger_name\":\"" << jsonEscape(metrics.logger_name) << "\","
       << "\"level\":\"" << jsonEscape(metrics.level) << "\","
       << "\"exists\":" << (metrics.exists ? "true" : "false") << ','
       << "\"is_async\":" << (metrics.is_async ? "true" : "false") << ','
       << "\"running\":" << (metrics.running ? "true" : "false") << ','
       << "\"event_count\":" << metrics.event_count << ','
       << "\"flush_interval\":" << metrics.flush_interval << ','
       << "\"max_pending_buffers\":" << metrics.max_pending_buffers << ','
       << "\"current_buffer_count\":" << metrics.current_buffer_count << ','
       << "\"current_buffer_available\":" << metrics.current_buffer_available << ','
       << "\"pending_buffers\":" << metrics.pending_buffers << ','
       << "\"accepted_events\":" << metrics.accepted_events << ','
       << "\"dropped_events\":" << metrics.dropped_events << ','
       << "\"written_events\":" << metrics.written_events
       << '}';
    return os.str();
}

} // namespace LoggerMcpTools
