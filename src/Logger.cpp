#include "include/logger.h"


/*============================Logger==================================*/
Logger::Logger(std::string name) : name_(name){}

Logger::setLevel(LogLevel::Level level) {level_ = level;}

void Logger::addAppender(std::shared_ptr<Appender> appender){
    appenders_.push_back(appender);
}

void Logger::delAppender(std::shared_ptr<Appender> appender){
    for(auto it = appenders_.begin(); it != appenders_.end(); ++it){
        if(*it == appender){
            appenders_.erase(it);
            break;
        }
    }
}

void Logger::clearAppender(){
    appenders_.clear();
}


void Logger::log(const LogEvent& event) const {
    if(event.getLevel() >= level_){
        for(auto appender : appenders_){
            appender->append(event);
        }
    }
}



