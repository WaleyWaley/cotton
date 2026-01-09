#pragma once
#include "AppenderProxy.hpp"
#include "LogFormatter.h"
#include <cstddef>
#include <fstream>


/*=================================================LogAppender==========================================*/
class StdoutAppender{
public:
    static void log(const LogFormatter& fmter, const LogEvent& event);
};

/**
 * @brief 滚动zhi文件日志输出器。日志器如果大于64mb或时间超过了24小时，了就会自动新建一个日志文件，继续写入日志
 */
class RollingFileAppender{
private:
    static constexper size_t c_default_max_file_size = 64_mb;
    static constexper Seconds c_default_max_time_interval = Seconds(24*60*60);  // 24小时

    // Flush 策略相关常量
    0


};

