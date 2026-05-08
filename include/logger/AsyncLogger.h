#pragma once

#include "EventFixedBuffer.hpp"
#include "logger/Logger.h"
#include "logger/LoggerConfig.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

class AsyncLogger : public Logger {
public:
    using EventBuffer = EventFixedBuffer;
    using EventBufferPtr = std::unique_ptr<EventBuffer>;

    explicit AsyncLogger(const LoggerConfig& config = LoggerConfig{})
        : Logger(config.logger_name)
        , config_(normalizeConfig_(config))
        , running_(false)
        , current_buffer_(std::make_unique<EventBuffer>(config_.event_count))
        , next_buffer_(std::make_unique<EventBuffer>(config_.event_count))
    {
        buffers_to_write_.reserve(config_.max_pending_buffers);
        setLogLevel(config_.level);
    }

    explicit AsyncLogger(int flush_interval)
        : AsyncLogger(LoggerConfig{.flush_interval = flush_interval}) {}

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger(AsyncLogger&&) = delete;
    auto operator=(const AsyncLogger&) -> AsyncLogger& = delete;
    auto operator=(AsyncLogger&&) -> AsyncLogger& = delete;

    ~AsyncLogger()
    {
        if (running_) {
            stop();
        }
    }

    auto append(LogEvent event) -> void
    {
        bool should_notify = false;

        {
            auto _ = std::lock_guard<std::mutex>{mutex_};
            if (current_buffer_->available() > 0) {
                current_buffer_->append(std::move(event));
                accepted_events_.fetch_add(1, std::memory_order_relaxed);
            } else {
                if (buffers_to_write_.size() >= config_.max_pending_buffers) {
                    dropped_events_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                buffers_to_write_.push_back(std::move(current_buffer_));
                if (next_buffer_) {
                    current_buffer_ = std::move(next_buffer_);
                } else {
                    current_buffer_ = std::make_unique<EventBuffer>(config_.event_count);
                }
                current_buffer_->append(std::move(event));
                accepted_events_.fetch_add(1, std::memory_order_relaxed);
                should_notify = true;
            }
        }

        if (should_notify) {
            cond_.notify_one();
        }
    }

    auto start() -> void
    {
        running_ = true;
        thread_ = std::thread(&AsyncLogger::threadFunc_, this);
        latch_.wait();
    }

    auto stop() -> void
    {
        running_ = false;
        cond_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // MCP Tools: logger_get_metrics 的 logger 运行指标。
    struct Metrics {
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

    [[nodiscard]] auto getConfig() const -> const LoggerConfig& { return config_; }
    [[nodiscard]] auto getMetrics() const -> Metrics
    {
        auto metrics = Metrics{};
        auto _ = std::lock_guard<std::mutex>{mutex_};
        metrics.running = running_.load(std::memory_order_acquire);
        metrics.event_count = config_.event_count;
        metrics.flush_interval = config_.flush_interval;
        metrics.max_pending_buffers = config_.max_pending_buffers;
        metrics.current_buffer_count = current_buffer_ ? current_buffer_->count() : 0;
        metrics.current_buffer_available = current_buffer_ ? current_buffer_->available() : 0;
        metrics.pending_buffers = buffers_to_write_.size();
        metrics.accepted_events = accepted_events_.load(std::memory_order_relaxed);
        metrics.dropped_events = dropped_events_.load(std::memory_order_relaxed);
        metrics.written_events = written_events_.load(std::memory_order_relaxed);
        return metrics;
    }

private:
    static auto normalizeConfig_(LoggerConfig cfg) -> LoggerConfig
    {
        if (cfg.event_count == 0) {
            cfg.event_count = c_k_event_count;
        }
        if (cfg.flush_interval <= 0) {
            cfg.flush_interval = 3;
        }
        if (cfg.max_pending_buffers < 2) {
            cfg.max_pending_buffers = 2;
        }
        return cfg;
    }

    auto threadFunc_() -> void
    {
        latch_.count_down();

        auto new_buffer1 = std::make_unique<EventBuffer>(config_.event_count);
        auto new_buffer2 = std::make_unique<EventBuffer>(config_.event_count);
        auto buffers_to_process = std::vector<EventBufferPtr>{};
        buffers_to_process.reserve(config_.max_pending_buffers);

        while (running_) {
            {
                auto lock = std::unique_lock<std::mutex>(mutex_);

                if (buffers_to_write_.empty()) {
                    cond_.wait_for(lock, std::chrono::seconds(config_.flush_interval));
                }

                if (buffers_to_write_.empty() && current_buffer_->count() == 0) {
                    continue;
                }

                buffers_to_write_.push_back(std::move(current_buffer_));
                current_buffer_ = std::move(new_buffer1);
                buffers_to_process.swap(buffers_to_write_);

                if (next_buffer_ == nullptr) {
                    next_buffer_ = std::move(new_buffer2);
                }
            }

            if (buffers_to_process.size() > config_.max_pending_buffers) {
                buffers_to_process.erase(
                    buffers_to_process.begin() + 2,
                    buffers_to_process.end());
            }

            for (const auto& buf : buffers_to_process) {
                for (const auto& event : buf->getEventSpan()) {
                    this->log(event);
                    written_events_.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (buffers_to_process.size() > 2) {
                buffers_to_process.resize(2);
            }

            if (new_buffer1 == nullptr && !buffers_to_process.empty()) {
                new_buffer1 = std::move(buffers_to_process.back());
                buffers_to_process.pop_back();
                new_buffer1->reset();
            }

            if (new_buffer2 == nullptr && !buffers_to_process.empty()) {
                new_buffer2 = std::move(buffers_to_process.back());
                buffers_to_process.pop_back();
                new_buffer2->reset();
            }

            buffers_to_process.clear();
        }
    }

private:
    LoggerConfig config_;

    std::thread thread_;
    std::atomic<bool> running_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::latch latch_{1};

    EventBufferPtr current_buffer_;
    EventBufferPtr next_buffer_;
    std::vector<EventBufferPtr> buffers_to_write_;
    std::atomic<uint64_t> accepted_events_{0};
    std::atomic<uint64_t> dropped_events_{0};
    std::atomic<uint64_t> written_events_{0};
};
