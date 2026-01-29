#!/bin/bash

# 解析：
# 1. testlogger.cpp       : 你的测试主程序
# 2. ../src/*.cpp         : 你的所有源代码 (Logger.cpp 等)
# 3. -I../include         : 让编译器能找到 "logger/Logger.h"
# 4. -I..                 : 让编译器能找到 "common/alias.h" (关键修复！)
# 5. -std=c++23 -lpthread : 标准库和线程库支持

g++ testlogger.cpp ../src/*.cpp -I../include -I.. -std=c++23 -lpthread -o testlogger && ./testlogger
./run testlogger