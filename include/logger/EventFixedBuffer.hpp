#pragma once
#include <span>
#include <vector>

#include "LogEvent.h"

// 默认缓冲区容量（事件条数）
constexpr size_t c_k_event_count = 64;

class EventFixedBuffer
{
public:
    explicit EventFixedBuffer(size_t capacity = c_k_event_count)
        : data_(capacity), count_(0) {}

    auto append(LogEvent event) -> bool
    {
        if (count_ < data_.size())
        {
            data_[count_] = std::move(event);
            ++count_;
            return true;
        }
        return false;
    }

    [[nodiscard]] auto count() const -> size_t { return count_; }
    [[nodiscard]] auto capacity() const -> size_t { return data_.size(); }
    [[nodiscard]] auto available() const -> size_t { return data_.size() - count_; }

    [[nodiscard]] auto getEventSpan() const -> std::span<const LogEvent>
    {
        return std::span<const LogEvent>(data_.data(), count_);
    }

    void reset() { count_ = 0; }

private:
    std::vector<LogEvent> data_;
    size_t count_;
};
