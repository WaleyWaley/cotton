#include "include/LogLevel.h"



std::string_view LogLevel::LevelToString(LogLevel::Level level) {
    switch (level) {
    #define value2name(levelvalue, levelname) \
        case(levelvalue): return #levelname;
        
        value2name(ALL, ALL);
        value2name(DEBUG, DEBUG);
        value2name(INFO, INFO);
        value2name(TRACE, TRACE);
        value2name(WARN, WARN);
        value2name(ERROR, ERROR);
        value2name(FATAL, FATAL);
        value2name(UNKNOWN, UNKNOWN);
    #undef value2name

    return "";
    }
}

LogLevel::Level LogLevel::StringToLogLevel(std::string_view str) {

    #define name2value(levelvalue, str) \
        if(str == #levelvalue){ \
            return LogLevel levelvalue \
        }

        name2value(ALL, "ALL");
        name2value(DEBUG, "DEBUG");
        name2value(INFO, "INFO");
        name2value(TRACE, "TRACE");
        name2value(WARN, "WARN");
        name2value(ERROR, "ERROR");
        name2value(FATAL, "FATAL");
        name2value(UNKNOWN, "UNKNOWN");

    #undef name2value

    return LogLevel UNKNOWN; // Default to UNKNOWN for unrecognized strings
}