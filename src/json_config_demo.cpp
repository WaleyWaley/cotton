#include "common/LogMacros.h"
#include "logger/LogManager.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    try {
        // auto logger = LoggerMgr::GetInstance().getOrCreateAsyncLoggerFromFile("logger_config.json");
        auto logger = LoggerMgr::GetInstance().getOrCreateAsyncLoggerFromFile("logger_socket_tcp.json");
        logger->start();

        for (int i = 0; i < 20; ++i) {
            LOG_INFO(logger, "json demo message index={}", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        LOG_WARN(logger, "this is a warning from json demo");
        LOG_ERROR(logger, "this is an error from json demo");

        std::this_thread::sleep_for(std::chrono::seconds(2));
        logger->stop();

        std::cout << "json logger demo done" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "failed to init logger from json: " << ex.what() << std::endl;
        return 1;
    }
}
