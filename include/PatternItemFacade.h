#pragma once

#include "LogEvent.h"
#include <iostream>
#include <print>


/*=================================================PatternItemFacade==========================================*/

class PatternItemFacade{
protected:
    auto operator=(const PatternItemFacade&) -> PatternItemFacade& = delete;
    auto operator=(PatternItemFacade&&) -> PatternItemFacade& = delete;

public:
    PatternItemFacade() = default;
    PatternItemFacade(const PatternItemFacade&) = default;
    PatternItemFacade(PatternItemFacade&&) = default;

    vitual auto format(std:;ostream& os, const LogEvent& event) -> size_t = 0;

    virtual ~PatternItemFacade() = default;
};
