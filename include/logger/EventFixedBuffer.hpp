#pragma once
#include <span>
#include <vector>

#include "LogEvent.h"

// 默认缓冲区容量（事件条数）
constexpr size_t c_k_event_count = 64;

/** @brief 异步日志器使用的固定容量事件缓冲区 
*   @details 固定缓存的优点：
*       容量已知、写入只是移动赋值和计数加一、重用缓存时不释放内存
*/
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

    // 用于获取事件的范围的视图。不拥有内存、不拷贝数据。只接受内存连续的容器/数组。像list这种就不行
    [[nodiscard]] auto getEventSpan() const -> std::span<const LogEvent>
    {
        return std::span<const LogEvent>(data_.data(), count_);
    }

    // 只把count_置0，不释放内存。
    void reset() { count_ = 0; }

private:
    std::vector<LogEvent> data_;
    size_t count_;
};
