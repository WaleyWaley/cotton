#include "logger/Logger.h"
#include "logger/AsyncLogger.h"
#include "logger/LoggerAppender.h"
#include "logger/AppenderProxy.hpp"
#include "logger/LogFormatter.h"
#include "common/alias.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

int main() {
    std::cout << "========== 异步日志系统性能与并发测试 ==========\n";

    // 1. 创建异步 Logger (假设你的构造函数接收刷新间隔，比如 1 秒)
    // 如果你的 AsyncLogger 不需要传参，改成 make_shared<AsyncLogger>() 即可
    auto async_logger = std::make_shared<AsyncLogger>(5); 

    // 2. 添加输出端：RollingFileAppender (限制每个文件大小为 1_kb，方便测试滚动分片)
    async_logger->addAppender(std::make_shared<AppenderProxy<RollingFileAppender>>(
        LogFormatter{"%d{%Y-%m-%d %H:%M:%S} [%p] Thread-%t: %m\n"}, "async_test_log.txt", 10_mb
    ));

    // 3. 启动后台刷盘线程
    async_logger->start();
    std::cout << "后台异步线程已启动，开始高并发写入测试...\n";

    // 4. 模拟多线程轰炸
    const int THREAD_COUNT = 5;       // 5个线程同时写
    const int LOGS_PER_THREAD = 1000; // 每个线程狂写 1000 条
    std::vector<std::thread> threads;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&async_logger, i, LOGS_PER_THREAD]() {
            uint32_t tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            std::string thread_name = "Worker-" + std::to_string(i);

            for (int j = 0; j < LOGS_PER_THREAD; ++j) {
                auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                
                // 构造一条日志事件
                LogEvent event(
                    "AsyncLogger", LogLevel::INFO, 0, tid, thread_name, now_t, 0, std::source_location::current()
                );
                
                // 格式化日志内容
                event.print("异步性能测试 -> 线程ID: {}, 这是第 {} 条日志信息。", i, j);
                
                // 将日志推入前端缓冲区 (不阻塞主业务)
                async_logger->append(std::move(event)); 
            }
        });
    }

    // 等待所有业务线程完成写入
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "主线程: 所有 " << (THREAD_COUNT * LOGS_PER_THREAD) 
              << " 条日志已成功投递到前端缓冲区！\n";
    std::cout << "前端接口耗时: " << duration << " ms\n";

    // 5. 给后台线程一点点时间刷盘（或者你的 stop() 方法里已经实现了等待缓冲区写完的逻辑）
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 6. 安全停止异步后台线程
    async_logger->stop();

    std::cout << "========== 异步日志测试圆满结束 ==========\n";
    std::cout << "请检查当前目录下是否生成了 async_test_log.txt 及其滚动分片文件！\n";

    return 0;
}