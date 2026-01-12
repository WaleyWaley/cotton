#pragma once

class LogEvent;

/* Abstract base class for appenders */

class AppenderFacade{
protected:
    AppenderFacade() = default;
    auto operator=(const AppenderFacade&) -> AppenderFacade& = delete;
    auto operator=(AppenderFacade&&) -> AppenderFacade& = delete;
public:
    AppenderFacade(const AppenderFacade&) = default;
    AppenderFacade(AppenderFacade&&) = default;
    virtual void log(const LogEvent& event) = 0;
    virtual ~AppenderFacade() = default;
};