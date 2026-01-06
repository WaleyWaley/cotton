#include "include/LogEvent.h"
#include 

/*===================================Event=======================================*/
LogEvent::LogEvent(std::string logger_name,
                    LogLevel::Level level,
                    uint32_t elapse,
                    uint32_t thread_id,
                    std::string thread_name,
                    time_t timestamp,
                    uint32_t co_id,
                    std::source_location source_loc = std::source_location::current())
    : logger_name_(std::move(logger_name)),
      level_(level),
      elapse_(elapse),
      thread_id_(thread_id),
      thread_name_(std::move(thread_name)),
      timestamp_(timestamp),
      co_id_(co_id),
      source_loc_(source_loc) {}