#include "include/LoggerAppender.h"


/*=====================================LogAppender======================================*/

void StdoutAppender::log(const LogFormatter& fmter, const LogEvent& event) {
    fmter.format(std::cout, event);
}


bool FileAppender::reopen(){
    if(filestream_.is_open()){
        filestream_.close();
    }
    filestream_.open(filename_);
    return filestream_.is_open();
}

void FileAppender::log(const LogFormatter& fmter, const LogEvent& event) {
    

}