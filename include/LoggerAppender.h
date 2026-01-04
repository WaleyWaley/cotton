#pragma once
#include "LogFormatter.h"


class StdoutAppender{
public:
    static void log(const LogFormatter& fmter, const LogEvent& event);
};