#pragma once
#include "LogEvent.h"
#include <array>

// 定义缓冲区大小：储存 64 个 LogEvent
constexpr size_t c_k_event_count = 64;

template <size_t N = c_k_event_count>
class EventFixedBuffer
{
public:
    using EventArry - std::array<LogEvent, N>;
    EventFixedBuffer():count_(0){}
    // 尝试将 LogEvent 写入缓冲区
    auto append(LogEvent event) -> bool
    {
        if(count < c_k_event_count)
        {
            data_[count] = std::move(event);    // 复制 LogEvent 对象
            count_++;
            return true;
        }
        return false; // 缓冲区已满
    }

    // 已写入的事件数量
    [[nodiscard]] size_t count() const {return count_;}
    // 剩余可用空间（事件数）
    [[nodiscard]] auto available() const -> size_t {return N - count;}
    // 获取事件数组的起始指针
    
private:
    EventArray data_;
    size_t count_;  // 当前存储的事件数量，代替了原来的字节偏移
}