#pragma once

#include "logger/PatternItemFacade.h"

#include <concepts>
#include <cstddef>
#include <ostream>
#include <utility>

template <typename T>
concept IsPatternImpl = requires(T x, std::ostream& os, const LogEvent& event) {
    { x.format(os, event) } -> std::same_as<size_t>;
};
template <IsPatternImpl PatternImpl>
class PatternItemProxy : public PatternItemFacade {
public:
    template <typename... Args>
    explicit PatternItemProxy(Args&&... args) : item_{std::forward<Args>(args)...} {}

    auto format(std::ostream& os, const LogEvent& event) -> size_t override {
        return item_.format(os, event);
    }

private:
    PatternImpl item_;
};
