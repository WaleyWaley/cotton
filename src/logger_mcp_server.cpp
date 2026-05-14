#include "json-rpc/json.hpp"
#include "logger/LoggerMcpTools.h"
#include "logger/LogManager.h"

#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr auto c_protocol_version = "2024-11-05";
constexpr auto c_server_name = "cotton-logger-mcp-server";
constexpr auto c_server_version = "0.1.0";

auto makeJsonRpcResponse(const json& id, json result) -> json
{
    return json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)}
    };
}

auto makeJsonRpcError(const json& id, int code, const std::string& message) -> json
{
    return json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
}

auto makeTextContent(const std::string& text) -> json
{
    return json::array({json{{"type", "text"}, {"text", text}}});
}

auto makeToolResult(json payload) -> json
{
    return json{{"content", makeTextContent(payload.dump(2))}};
}

auto makeBoolToolResult(bool ok, const std::string& success_message, const std::string& failure_message) -> json
{
    return makeToolResult(json{
        {"ok", ok},
        {"message", ok ? success_message : failure_message}
    });
}

auto metricsToJson(const LoggerMcpTools::LoggerMetrics& metrics) -> json
{
    return json{
        {"logger_name", metrics.logger_name},
        {"level", metrics.level},
        {"exists", metrics.exists},
        {"is_async", metrics.is_async},
        {"running", metrics.running},
        {"event_count", metrics.event_count},
        {"flush_interval", metrics.flush_interval},
        {"max_pending_buffers", metrics.max_pending_buffers},
        {"current_buffer_count", metrics.current_buffer_count},
        {"current_buffer_available", metrics.current_buffer_available},
        {"pending_buffers", metrics.pending_buffers},
        {"accepted_events", metrics.accepted_events},
        {"dropped_events", metrics.dropped_events},
        {"written_events", metrics.written_events}
    };
}

auto getStringArg(const json& args, const std::string& key, std::string default_value = {}) -> std::string
{
    if (!args.contains(key) || args.at(key).is_null()) {
        return default_value;
    }
    if (!args.at(key).is_string()) {
        throw std::invalid_argument{"argument '" + key + "' must be a string"};
    }
    return args.at(key).get<std::string>();
}

auto getSizeArg(const json& args, const std::string& key, size_t default_value) -> size_t
{
    if (!args.contains(key) || args.at(key).is_null()) {
        return default_value;
    }
    if (!args.at(key).is_number_unsigned()) {
        throw std::invalid_argument{"argument '" + key + "' must be an unsigned integer"};
    }
    return args.at(key).get<size_t>();
}

// 具体格式示例
/*{
  "name": "calculate_sum",
  "description": "Add two numbers together",
  "inputSchema": {
    "type": "object",
    "properties": {
      "a": { "type": "number", "description": "First number" },
      "b": { "type": "number", "description": "Second number" }
    },
    "required": ["a", "b"]
  }
} */
auto toolSchema(
    std::string name,
    std::string description,
    json properties,
    std::vector<std::string> required = {}) -> json
{
    return json{
        {"name", std::move(name)},
        {"description", std::move(description)},
        {"inputSchema", {
            {"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}
        }}
    };
}

auto handleInitialize() -> json
{
    return json{
        {"protocolVersion", c_protocol_version},
        {"capabilities", {
            {"tools", json::object()}
        }},
        {"serverInfo", {
            {"name", c_server_name},
            {"version", c_server_version}
        }}
    };
}

auto handleToolsList() -> json
{
    auto tools = json::array();

    tools.push_back(toolSchema(
        "logger_set_level",
        "Set runtime log level for an existing Cotton logger.",
        json{
            {"logger_name", {{"type", "string"}, {"description", "Logger name. Empty string means root."}}},
            {"level", {{"type", "string"}, {"description", "Log level: SYSFATAL, SYSERR, FATAL, ERROR, WARN, TRACE, INFO, DEBUG, ALL."}}}
        },
        {"logger_name", "level"}
    ));

    tools.push_back(toolSchema(
        "logger_reload_config",
        "Reload an async logger from a Cotton logger JSON config file.",
        json{
            {"config_file", {{"type", "string"}, {"description", "Path to logger JSON config file."}}}
        },
        {"config_file"}
    ));

    tools.push_back(toolSchema(
        "logger_tail_file",
        "Read the last N lines from a log file.",
        json{
            {"file_path", {{"type", "string"}, {"description", "Path to log file."}}},
            {"lines", {{"type", "integer"}, {"minimum", 0}, {"description", "Number of tail lines to read. Default: 100."}}}
        },
        {"file_path"}
    ));

    tools.push_back(toolSchema(
        "logger_get_metrics",
        "Get runtime metrics for a Cotton logger.",
        json{
            {"logger_name", {{"type", "string"}, {"description", "Logger name. Empty string means root."}}}
        },
        {"logger_name"}
    ));

    return json{{"tools", std::move(tools)}};
}

auto handleToolCall(const json& params) -> json
{
    const auto name = getStringArg(params, "name");
    const auto args = params.contains("arguments") && params.at("arguments").is_object()
        ? params.at("arguments")
        : json::object();

    if (name == "logger_set_level") {
        const auto logger_name = getStringArg(args, "logger_name");
        const auto level = getStringArg(args, "level");
        const auto ok = LoggerMcpTools::logger_set_level(logger_name, level);
        return makeBoolToolResult(ok, "logger level updated", "logger level update failed");
    }

    if (name == "logger_reload_config") {
        const auto config_file = getStringArg(args, "config_file");
        const auto ok = LoggerMcpTools::logger_reload_config(config_file);
        return makeBoolToolResult(ok, "logger config reloaded", "logger config reload failed");
    }

    if (name == "logger_tail_file") {
        const auto file_path = getStringArg(args, "file_path");
        const auto lines = getSizeArg(args, "lines", 100);
        const auto tail_lines = LoggerMcpTools::logger_tail_file(file_path, lines);
        return makeToolResult(json{
            {"file_path", file_path},
            {"lines", tail_lines}
        });
    }

    if (name == "logger_get_metrics") {
        const auto logger_name = getStringArg(args, "logger_name");
        return makeToolResult(metricsToJson(LoggerMcpTools::logger_get_metrics(logger_name)));
    }

    throw std::invalid_argument{"unknown tool: " + name};
}

auto dispatchRequest(const json& request) -> std::optional<json>
{
    const auto method = request.value("method", std::string{});
    const auto id = request.contains("id") ? request.at("id") : json{};
    const auto is_notification = !request.contains("id");

    try {
        if (method == "initialize") {
            return makeJsonRpcResponse(id, handleInitialize());
        }

        if (method == "notifications/initialized") {
            return std::nullopt;
        }

        if (method == "tools/list") {
            return makeJsonRpcResponse(id, handleToolsList());
        }

        if (method == "tools/call") {
            const auto params = request.contains("params") ? request.at("params") : json::object();
            return makeJsonRpcResponse(id, handleToolCall(params));
        }

        if (is_notification) {
            return std::nullopt;
        }

        return makeJsonRpcError(id, -32601, "method not found: " + method);
    } catch (const std::exception& ex) {
        if (is_notification) {
            return std::nullopt;
        }
        return makeJsonRpcError(id, -32602, ex.what());
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1) {
        try {
            LoggerMgr::GetInstance().reloadAsyncLoggerFromFile(argv[1]);
        } catch (const std::exception& ex) {
            std::cerr << "failed to preload logger config: " << ex.what() << '\n';
        }
    }

    std::ios::sync_with_stdio(false);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            const auto request = json::parse(line);
            auto response = dispatchRequest(request);
            if (response.has_value()) {
                std::cout << response->dump() << '\n';
                std::cout.flush();
            }
        } catch (const std::exception& ex) {
            auto error = makeJsonRpcError(nullptr, -32700, ex.what());
            std::cout << error.dump() << '\n';
            std::cout.flush();
        }
    }

    return 0;
}

/*
    g++ -std=c++20 -pthread \
        -I"/home/john/workspace/cotton" \
        -I"/home/john/workspace/cotton/include" \
        "/home/john/workspace/cotton/src/logger_mcp_server.cpp" \
        "/home/john/workspace/cotton/src/LoggerMcpTools.cpp" \
        "/home/john/workspace/cotton/src/LogManager.cpp" \
        "/home/john/workspace/cotton/src/Logger.cpp" \
        "/home/john/workspace/cotton/src/LoggerConfig.cpp" \
        "/home/john/workspace/cotton/src/LoggerAppender.cpp" \
        "/home/john/workspace/cotton/src/LogFormatter.cpp" \
        "/home/john/workspace/cotton/src/LogEvent.cpp" \
        "/home/john/workspace/cotton/src/LogLevel.cpp" \
        "/home/john/workspace/cotton/src/PatternItemImpl.cpp" \
        -o "/home/john/workspace/cotton/logger_mcp_server"
*/