#include "logger/LoggerAppender.h"
#include "logger/LogEvent.h"
#include "logger/LogFormatter.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// Test-only lightweight LogFormatter implementation:
// isolate SocketAppender behavior from the unfinished parser implementation.
void LogFormatter::startParse_() {
    error_ = false;
    pattern_items_.clear();
}

auto LogFormatter::format(std::ostream& os, const LogEvent& event) const -> size_t {
    const std::string msg = event.getContent();
    os << msg << '\n';
    return msg.size() + 1;
}

auto LogFormatter::format(const LogEvent& event) const -> std::string {
    return event.getContent() + "\n";
}

class TcpCountingServer {
public:
    explicit TcpCountingServer(uint32_t read_delay_ms = 0) : read_delay_ms_(read_delay_ms) {}

    auto start() -> uint16_t {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(listen_fd_ >= 0);

        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;

        assert(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        assert(::listen(listen_fd_, 8) == 0);

        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        running_.store(true, std::memory_order_release);

        worker_ = std::thread([this] { this->loop_(); });
        return ntohs(addr.sin_port);
    }

    auto stop() -> void {
        running_.store(false, std::memory_order_release);

        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (conn_fd_ >= 0) {
            ::close(conn_fd_);
            conn_fd_ = -1;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    ~TcpCountingServer() { stop(); }

    [[nodiscard]] auto messageCount() const -> size_t { return message_count_.load(std::memory_order_acquire); }
    [[nodiscard]] auto bytesReceived() const -> size_t { return bytes_received_.load(std::memory_order_acquire); }

private:
    auto loop_() -> void {
        constexpr int k_buf_size = 8192;
        char buf[k_buf_size];

        while (running_.load(std::memory_order_acquire)) {
            if (conn_fd_ < 0) {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(listen_fd_, &readfds);
                timeval tv {0, 200000};

                const int ready = ::select(listen_fd_ + 1, &readfds, nullptr, nullptr, &tv);
                if (ready <= 0) {
                    continue;
                }

                sockaddr_in cli {};
                socklen_t cl = sizeof(cli);
                const int accepted = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&cli), &cl);
                if (accepted >= 0) {
                    conn_fd_ = accepted;
                }
                continue;
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(conn_fd_, &readfds);
            timeval tv {0, 200000};

            const int ready = ::select(conn_fd_ + 1, &readfds, nullptr, nullptr, &tv);
            if (ready <= 0) {
                continue;
            }

            const ssize_t n = ::recv(conn_fd_, buf, sizeof(buf), 0);
            if (n <= 0) {
                ::close(conn_fd_);
                conn_fd_ = -1;
                continue;
            }

            bytes_received_.fetch_add(static_cast<size_t>(n), std::memory_order_relaxed);
            message_count_.fetch_add(
                static_cast<size_t>(std::count(buf, buf + n, '\n')),
                std::memory_order_relaxed
            );

            if (read_delay_ms_ > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(read_delay_ms_));
            }
        }
    }

private:
    uint32_t read_delay_ms_ {0};
    std::atomic<bool> running_ {false};
    std::atomic<size_t> message_count_ {0};
    std::atomic<size_t> bytes_received_ {0};
    int listen_fd_ {-1};
    int conn_fd_ {-1};
    std::thread worker_;
};

struct PerfMetrics {
    size_t total_logs {0};
    double seconds {0.0};
    double logs_per_sec {0.0};
    double avg_us_per_log {0.0};
};

static auto makeEvent(int producer_id, int seq) -> LogEvent {
    const uint32_t tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    LogEvent event {
        "SocketPerf",
        LogLevel::INFO,
        0U,
        tid,
        "BenchThread",
        std::time(nullptr),
        0U
    };
    event.print("producer={} seq={}", producer_id, seq);
    return event;
}

static auto runProducerBenchmark(
    SocketAppender& appender,
    const LogFormatter& formatter,
    int thread_count,
    int logs_per_thread
) -> PerfMetrics {
    const auto begin = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(thread_count));

    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&, t] {
            for (int i = 0; i < logs_per_thread; ++i) {
                auto event = makeEvent(t, i);
                appender.log(formatter, event);
            }
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    const auto end = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration_cast<std::chrono::duration<double>>(end - begin).count();
    const size_t total = static_cast<size_t>(thread_count) * static_cast<size_t>(logs_per_thread);

    PerfMetrics metrics;
    metrics.total_logs = total;
    metrics.seconds = sec;
    metrics.logs_per_sec = static_cast<double>(total) / sec;
    metrics.avg_us_per_log = (sec * 1'000'000.0) / static_cast<double>(total);
    return metrics;
}

static auto waitForMessages(const TcpCountingServer& server, size_t target, std::chrono::seconds timeout) -> size_t {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const size_t current = server.messageCount();
        if (current >= target) {
            return current;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return server.messageCount();
}

static auto benchmarkBackpressure() -> void {
    std::cout << "[CASE 1] backpressure (slow receiver), check producer non-blocking\n";

    TcpCountingServer slow_server(4);
    const uint16_t port = slow_server.start();

    constexpr int k_threads = 8;
    constexpr int k_logs_per_thread = 30'000;
    constexpr size_t k_total_logs = static_cast<size_t>(k_threads) * static_cast<size_t>(k_logs_per_thread);

    const LogFormatter formatter {"%m%n"};
    {
        SocketAppender appender {
            "127.0.0.1",
            port,
            SocketAppender::Protocol::TCP,
            4096,
            20
        };

        const auto perf = runProducerBenchmark(appender, formatter, k_threads, k_logs_per_thread);
        std::cout << "  produced logs   : " << perf.total_logs << "\n";
        std::cout << "  producer time   : " << perf.seconds << " s\n";
        std::cout << "  producer QPS    : " << static_cast<uint64_t>(perf.logs_per_sec) << " logs/s\n";
        std::cout << "  avg log() cost  : " << perf.avg_us_per_log << " us\n";

        // This threshold is intentionally loose to avoid flaky failures across machines.
        assert(perf.avg_us_per_log < 1500.0 && "front-end log() should stay low-latency under backpressure");
    }

    const size_t received = waitForMessages(slow_server, k_total_logs, std::chrono::seconds(3));
    const double delivered_ratio = static_cast<double>(received) / static_cast<double>(k_total_logs);

    std::cout << "  server received : " << received << "/" << k_total_logs << "\n";
    std::cout << "  delivery ratio  : " << delivered_ratio * 100.0 << "%\n";
    std::cout << "  note            : lower ratio is expected here due to bounded queue + drop-on-overflow design.\n";
    std::cout << "  PASS\n\n";

    slow_server.stop();
}

static auto benchmarkFastPath() -> void {
    std::cout << "[CASE 2] fast receiver, check throughput and delivery\n";

    TcpCountingServer fast_server(0);
    const uint16_t port = fast_server.start();

    constexpr int k_threads = 4;
    constexpr int k_logs_per_thread = 20'000;
    constexpr size_t k_total_logs = static_cast<size_t>(k_threads) * static_cast<size_t>(k_logs_per_thread);

    const LogFormatter formatter {"%m%n"};
    {
        SocketAppender appender {
            "127.0.0.1",
            port,
            SocketAppender::Protocol::TCP,
            131072,
            20
        };

        const auto perf = runProducerBenchmark(appender, formatter, k_threads, k_logs_per_thread);
        std::cout << "  produced logs   : " << perf.total_logs << "\n";
        std::cout << "  producer time   : " << perf.seconds << " s\n";
        std::cout << "  producer QPS    : " << static_cast<uint64_t>(perf.logs_per_sec) << " logs/s\n";
        std::cout << "  avg log() cost  : " << perf.avg_us_per_log << " us\n";

        const size_t received = waitForMessages(fast_server, k_total_logs, std::chrono::seconds(5));
        const double delivered_ratio = static_cast<double>(received) / static_cast<double>(k_total_logs);
        std::cout << "  server received : " << received << "/" << k_total_logs << "\n";
        std::cout << "  delivery ratio  : " << delivered_ratio * 100.0 << "%\n";

        assert(received > 0 && "server should receive some messages");
    }

    std::cout << "  PASS\n\n";
    fast_server.stop();
}

int main() {
    std::cout << "========== SocketAppender performance test ==========\n\n";
    benchmarkBackpressure();
    benchmarkFastPath();
    std::cout << "========== done ==========\n";
    return 0;
}

/* 
g++ -std=c++23 -pthread \
  test/socket_test.cpp \
  src/LoggerAppender.cpp src/LogEvent.cpp src/LogLevel.cpp \
  -Iinclude -I. -o /tmp/socket_test

/tmp/socket_test
*/
