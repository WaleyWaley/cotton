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


/** @brief 它继承自Logger。在同步Logger的路由能力前面加了一层异步缓冲 
*   Logger 收到日志后立刻分发给 appender 输出；AsyncLogger 收到日志后先放进内存缓冲区，由后台线程再批量调用 Logger::log 输出。
*/

/** @details 
*   前台线程快速写入缓冲
*   后台线程批量取缓冲
*   定时flush
*   过载时丢弃日志
*   记录运行指标
*/
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

    /** @todo 记录丢弃的事件数量现在只是记录了超出buffers_to_write_而丢弃的事件数量，其余的也应统计 */
    auto append(LogEvent event) -> void
    {
        bool should_notify = false;

        {
            auto _ = std::lock_guard<std::mutex>{mutex_};
            if (current_buffer_->available() > 0) {
                // 缓冲区未满
                current_buffer_->append(std::move(event));
                accepted_events_.fetch_add(1, std::memory_order_relaxed);
            } else {
                // 缓冲区已满
                if (buffers_to_write_.size() >= config_.max_pending_buffers) {
                    dropped_events_.fetch_add(1, std::memory_order_relaxed);    // 记录因为超出写入缓存量而丢弃的事件数量
                    return;
                }

                /*
                                    前台业务线程 append()
                                        |
                                        | current_buffer_ 满了
                                        v
                                    buffers_to_write_
                */
                buffers_to_write_.push_back(std::move(current_buffer_));

                // 多缓冲机制（换弹夹）
                if (next_buffer_) {
                    current_buffer_ = std::move(next_buffer_);
                } else {
                    current_buffer_ = std::make_unique<EventBuffer>(config_.event_count);
                }

                current_buffer_->append(std::move(event));
                accepted_events_.fetch_add(1, std::memory_order_relaxed);

                // 缓冲区已满通知后台进程threadFunc_可以处理了
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
        // 后台线程启动时latch_.count_down();latch_变为0表示后台线程已经成功启动
        latch_.wait();  // 启动同步
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

    // 获取Json配置文件信息
    [[nodiscard]] auto getConfig() const -> const LoggerConfig& { return config_; }

    // 用于MCP Tools: logger_get_metrics 的 logger 运行指标。
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

    // 后台写入线程函数
    auto threadFunc_() -> void
    {
        /*这里的 new_buffer1 和 new_buffer2 是后台线程自己的备用空缓冲。
        作用是：当后台线程把 current_buffer_ 切走时，可以马上给前台线程换一个空 buffer，减少临时申请内存。
        buffers_to_process 是后台线程局部处理队列，用来接收从 buffers_to_write_ swap 出来的待写缓冲。*/
        auto new_buffer1 = std::make_unique<EventBuffer>(config_.event_count);
        auto new_buffer2 = std::make_unique<EventBuffer>(config_.event_count);
        auto buffers_to_process = std::vector<EventBufferPtr>{};

        // 后台线程自己的局部“处理队列”，用来把共享队列的缓冲区一次性搬出来，在锁外慢慢写
        // 保存的是：已经写满，或者等待后台线程写出的缓冲区
        buffers_to_process.reserve(config_.max_pending_buffers);

        latch_.count_down();

        while (running_) {
            {
                auto lock = std::unique_lock<std::mutex>(mutex_);

                // 等待日志到来和后台刷新
                if (buffers_to_write_.empty()) {
                    // 如果写入队列为空，且当前缓冲区没有数据，则等待 前台线程 notify_one()、或者等 flush_interval 秒超时
                    cond_.wait_for(lock, std::chrono::seconds(config_.flush_interval));
                }

                if (buffers_to_write_.empty() && current_buffer_->count() == 0) {
                    continue;
                }

                buffers_to_write_.push_back(std::move(current_buffer_));

                current_buffer_ = std::move(new_buffer1);

                buffers_to_process.swap(buffers_to_write_);

                // 如果前台备用缓存被用掉了，后台线程会把 new_buffer2 补给它
                if (next_buffer_ == nullptr) {
                    next_buffer_ = std::move(new_buffer2); 
                }
            }   // 锁结束

            // 过载裁剪(采取自muduo库)
            if (buffers_to_process.size() > config_.max_pending_buffers) {
                buffers_to_process.erase(
                    buffers_to_process.begin() + 2,
                    buffers_to_process.end());
            }

            for (const auto& buf : buffers_to_process) {
                for (const auto& event : buf->getEventSpan()) {
                    // 通过Logger（同步日志）来输出日志。
                    this->log(event);
                    written_events_.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // 过载保护，不让后台无限处理积压，避免内存和延迟持续扩大。
            if (buffers_to_process.size() > 2) {
                buffers_to_process.resize(2);
            }

            // 回收 buffer，处理完的 buffer 不直接释放，而是 reset 后保存到 new_buffer1/new_buffer2，下次继续用。这就是减少内存分配和内存碎片的地方。
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
    LoggerConfig config_;                              // 日志配置

    std::thread thread_;
    std::atomic<bool> running_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::latch latch_{1};

    EventBufferPtr current_buffer_;                     // 前台当前写入的缓冲
    EventBufferPtr next_buffer_;                        // 备用空缓冲，减少临时分配
    std::vector<EventBufferPtr> buffers_to_write_;      // 已经满了、等待后台线程写出的缓冲队列
    std::atomic<uint64_t> accepted_events_{0};          // 成功进入异步系统的事件数
    std::atomic<uint64_t> dropped_events_{0};           // 过载丢弃的事件数
    std::atomic<uint64_t> written_events_{0};           // 后台实际写出的事件数
};
