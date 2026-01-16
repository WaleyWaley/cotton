#pragma once
#include "PatternItemFacade.h"
#include <cstddef>
#include <iostream>
#include <print>

template <typename ItemImpl>
class PatternItemProxy : public PatternItemFacade{
public:

    template<typename... Args>
    explicit PatternItemProxy(Args&&... args) : item_{ std::forward<Args>(args)...} {}

    // 这个就是实现不同formatItem的基类？
    auto format(std::ostream& os, const LogEvent& event) -> size_t override{ return item_.format(os, event);}
    
private:
    ItemImpl item_;
};