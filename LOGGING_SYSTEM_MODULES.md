# Cotton 日志系统模块讲解

> 面试定位：这是一个用现代 C++ 实现的配置化、异步化、多端路由日志系统。整体目标不是只把日志打印出来，而是解决真实服务中的几个核心问题：业务线程不能被慢 I/O 拖垮，日志输出目标要可扩展，格式要可配置，运行时要能观测和调节。

## 1. 总体架构

### 1.1 核心调用链路

日志从业务代码到最终输出，大致经过下面的链路：

```text
LOG_INFO / LOG_ERROR 等宏
        |
        v
LogEvent 日志事件
        |
        v
Logger 或 AsyncLogger
        |
        v
AppenderFacade 抽象接口
        |
        v
AppenderProxy<具体 Appender>
        |
        v
LogFormatter 格式化
        |
        v
Stdout / RollingFile / Socket / SQL
```

对应代码位置：

- 宏入口：`common/LogMacros.h`
- 日志事件：`include/logger/LogEvent.h`、`src/LogEvent.cpp`
- 同步日志器：`include/logger/Logger.h`、`src/Logger.cpp`
- 异步日志器：`include/logger/AsyncLogger.h`
- 输出端抽象：`include/logger/AppenderFacade.h`、`include/logger/AppenderProxy.hpp`
- 输出端实现：`include/logger/LoggerAppender.h`、`src/LoggerAppender.cpp`
- 格式化：`include/logger/LogFormatter.h`、`src/LogFormatter.cpp`、`src/PatternItemImpl.cpp`
- 配置管理：`include/logger/LoggerConfig.h`、`src/LoggerConfig.cpp`
- 日志器管理：`include/logger/LogManager.h`、`src/LogManager.cpp`
- MCP 工具：`include/logger/LoggerMcpTools.h`、`src/LoggerMcpTools.cpp`、`src/logger_mcp_server.cpp`

### 1.2 设计思路

这个系统采用的是“前端采集 + 后端输出”的设计。

前端负责：

- 判断日志级别，尽早过滤低优先级日志。
- 构造 `LogEvent`，补齐线程、时间、源码位置等上下文。
- 对异步日志器来说，只把事件写入内存缓冲，不直接做慢 I/O。

后端负责：

- 从缓冲区批量取日志。
- 根据 appender 列表路由到不同输出端。
- 每个输出端使用自己的格式化模板。
- 慢速目标比如文件、网络、SQL 都尽量放到后台线程或批处理逻辑里。

这个拆分解决了一个很典型的问题：日志系统本身不能成为业务路径上的性能瓶颈。业务线程只做轻量工作，耗时的写文件、发网络、写数据库交给后端处理。

### 1.3 优点

- 职责清晰：事件、日志器、格式化器、输出端、配置管理互相解耦。
- 输出端可扩展：新增一个 appender 时，只要满足 `AppenderProxy` 要求的接口即可。
- 格式可配置：不同 appender 可以使用不同 pattern。
- 支持同步和异步两种模式：普通 `Logger` 直接输出，`AsyncLogger` 先入缓冲再后台输出。
- 支持运行期治理：通过 MCP 工具调整级别、重载配置、查看指标和日志尾部。

## 2. 日志级别模块

### 2.1 代码位置

- `include/logger/LogLevel.h`
- `src/LogLevel.cpp`

### 2.2 模块职责

`LogLevel` 负责定义日志级别，并提供字符串和枚举之间的转换。

级别定义大致是：

```cpp
enum class LogLevel {
    SYSFATAL = 9,
    SYSERR = 8,
    FATAL = 7,
    ERROR = 6,
    WARN = 5,
    TRACE = 4,
    INFO = 3,
    DEBUG = 2,
    ALL = 1,
    UNKNOW = -1
};
```

这里数值越大，代表日志级别越高。`Logger::isLevelEnable` 中使用 `level >= level_` 判断日志是否应该输出。

### 2.3 如何写的

`LevelToString` 使用 `switch` 把枚举转成字符串。实现里用了宏 `XX` 减少重复代码。

`StringToLogLevel` 使用 FNV-1a 哈希做字符串匹配，哈希函数定义在 `common/util.hpp` 中的 `UtilT::cHashString`。它支持大写和小写，例如 `INFO` 和 `info` 都能识别。

同时，代码重载了 `<=`、`>=`、`<`、`>`、`==` 操作符，让日志级别可以像普通数值一样比较。

### 2.4 解决的问题

日志系统必须快速判断一条日志是否需要输出。如果每次都做复杂字符串比较，成本不稳定。这里用枚举值比较做运行期过滤，字符串只在配置加载、MCP 调级别等场景转换一次。

### 2.5 优点

- 类型安全：使用 `enum class`，避免普通枚举污染命名空间。
- 判断简单：级别比较最终是底层整数比较。
- 配置友好：支持从 JSON 字符串转换成日志级别。
- 运行期友好：MCP 工具也可以传字符串修改日志级别。

### 2.6 可以继续优化的点

- `UNKNOW` 拼写可以改成 `UNKNOWN`。
- 可以把字符串转换改成 `std::unordered_map<std::string_view, LogLevel>`，可读性更高。
- 如果保留 hash-switch，需要注意哈希碰撞虽然概率很低，但理论上存在。

## 3. LogEvent 日志事件模块

### 3.1 代码位置

- `include/logger/LogEvent.h`
- `src/LogEvent.cpp`

### 3.2 模块职责

`LogEvent` 是一条日志的结构化载体。它不是简单的一段字符串，而是保存了日志上下文：

- logger 名称
- 日志级别
- 程序运行耗时
- 线程 id
- 线程名称
- 时间戳
- 协程 id
- 源码位置
- 用户自定义日志内容

其中源码位置使用 `std::source_location` 保存，可以拿到文件名、函数名和行号。

### 3.3 如何写的

构造函数在 `src/LogEvent.cpp` 中实现，使用 `std::move` 移动字符串字段，减少不必要拷贝。

`LogEvent` 内部使用 `std::stringstream custom_msg_` 保存用户日志内容。用户可以通过：

```cpp
event.print("user id={}, name={}", id, name);
```

写入格式化后的消息。`print` 使用 `std::format_string<Args...>` 和 `std::format`，这属于 C++20 风格的类型安全格式化。

### 3.4 设计思路

日志事件和最终输出字符串分离。

也就是说，在构造日志时并不立即决定它要长什么样。`LogEvent` 保存结构化字段，后续不同 `LogFormatter` 可以按自己的 pattern 输出不同格式。

例如同一条事件：

- 控制台可以输出简短格式：`[INFO] message`
- 文件可以输出详细格式：`time level logger file:line message`
- SQL 可以拆成字段写入数据库

### 3.5 解决的问题

如果日志系统一开始就把所有内容拼成字符串，后面想做多端输出、SQL 字段化、不同格式模板都会很麻烦。`LogEvent` 把数据保持为结构化字段，让后续路由和格式化更灵活。

### 3.6 优点

- 支持源码定位：通过 `std::source_location` 自动记录文件、行号、函数。
- 支持类型安全格式化：`std::format_string` 可以在编译期约束格式参数。
- 支持结构化扩展：SQL appender 可以直接读取 level、logger、thread_id、file、line、message 等字段。
- 支持移动语义：适合异步缓冲中移动事件，减少拷贝。

### 3.7 可以继续优化的点

- `std::stringstream` 不可拷贝，目前 `LogEvent` 对拷贝构造是删除的，但拷贝赋值写成了 default，后续可以统一禁用拷贝，只保留移动语义。
- `thread_name` 当前固定为 `"MainThread"`，可以接入线程局部变量保存真实线程名。
- `elapse` 和 `co_id` 当前多数场景是 0，后续可以接入启动时间和协程框架。

## 4. 日志宏模块

### 4.1 代码位置

- `common/LogMacros.h`

### 4.2 模块职责

日志宏提供业务侧使用入口：

```cpp
LOG_INFO(logger, "hello {}", name);
LOG_ERROR(logger, "failed code={}", code);
```

它屏蔽了事件构造、源码位置采集、同步/异步提交差异。

### 4.3 如何写的

核心宏是 `LOG_LEVEL`。它做了几件事：

1. 判断 logger 是否有效。
2. 把指针、引用、`shared_ptr` 形式的 logger 统一转成引用。
3. 调用 `isLevelEnable` 做前置过滤。
4. 使用 `std::source_location::current()` 构造事件。
5. 调用 `event.print` 写入用户消息。
6. 根据 logger 类型选择同步 `log` 或异步 `append`。

其中 `submit_log` 使用 `if constexpr` 判断模板参数是否是 `AsyncLogger`：

```cpp
if constexpr (std::is_same_v<remove_cvref_t<L>, AsyncLogger>) {
    logger.append(std::move(event));
} else {
    logger.log(event);
}
```

### 4.4 设计思路

宏只作为薄入口，不把业务逻辑写死。真正的事件构造放在 `logger_macro_detail::make_event`，真正的提交放在 `submit_log`。这样代码比纯宏堆叠更可维护。

使用宏的主要原因是需要准确捕获调用点的 `source_location`。如果封装成普通函数，很容易捕获到封装函数内部的位置，而不是业务调用位置。

### 4.5 解决的问题

- 统一日志调用方式。
- 避免业务代码手动构造 `LogEvent`。
- 避免低级别日志仍然做格式化开销。
- 让同步和异步日志器对业务侧保持一致 API。

### 4.6 优点

- 调用方式简单。
- 支持可变参数和 `std::format`。
- 支持多种 logger 传入形式：引用、指针、`shared_ptr`。
- 前置级别过滤降低无效日志开销。

### 4.7 可以继续优化的点

- 宏里的线程名目前固定，可以扩展为 `thread_local std::string`。
- 如果想完全避免被过滤日志的格式参数求值成本，需要提醒业务不要在参数里写昂贵表达式。
- 可以增加 `LOG_SYSERR`、`LOG_TRACE` 等宏。

## 5. Logger 同步日志器模块

### 5.1 代码位置

- `include/logger/Logger.h`
- `src/Logger.cpp`

### 5.2 模块职责

`Logger` 是最基础的日志器，负责：

- 保存 logger 名称。
- 保存当前日志级别。
- 维护 appender 列表。
- 判断日志级别。
- 把日志事件路由给所有 appender。

### 5.3 如何写的

`Logger` 继承 `std::enable_shared_from_this<Logger>`，说明设计上希望 logger 可以被智能指针统一管理。

内部有：

```cpp
std::string name_;
LogLevel level_;
std::vector<Sptr<AppenderFacade>> appenders_;
inline static std::atomic<uint32_t> auto_logger_id_ = 0;
```

默认构造函数通过 `auto_logger_id_.fetch_add(1)` 生成自动名称。带参构造函数则使用外部传入名称。

`log` 函数逻辑非常简洁：

1. 如果事件级别低于当前 logger 级别，直接 return。
2. 遍历所有 appender。
3. 对每个 appender 调用 `appender->log(event)`。

### 5.4 设计思路

`Logger` 不关心输出到哪里，也不关心具体格式。它只负责“过滤 + 路由”。这让 logger 和 appender 解耦。

例如：

- 同一个 logger 可以挂多个 appender。
- 同一个 appender 类型可以被不同 logger 使用。
- 新增输出端不需要改 `Logger::log`。

### 5.5 解决的问题

如果 logger 直接写文件或写网络，那么输出目标会和日志器强耦合。现在通过 `AppenderFacade` 抽象，`Logger` 只面向接口编程。

### 5.6 优点

- 简单稳定：核心 log 路径很短。
- 可扩展：appender 类型独立演进。
- 支持多端路由：一个事件可以被多个 appender 同时消费。
- 支持级别过滤：减少不必要输出。

### 5.7 可以继续优化的点

- `level_` 在构造时最好显式初始化为 `LogLevel::INFO` 或 `LogLevel::ALL`。
- `appenders_` 的增删和 `log` 遍历目前没有加锁。如果运行时频繁热更新 appender，建议使用互斥锁、读写锁或 copy-on-write 方案。
- `Logger::log` 中某个 appender 抛异常时，可能影响后续 appender，后续可以做异常隔离。

## 6. Appender 抽象与类型擦除模块

### 6.1 代码位置

- `include/logger/AppenderFacade.h`
- `include/logger/AppenderProxy.hpp`

### 6.2 模块职责

这部分负责把不同类型的输出端统一成同一种接口。

`AppenderFacade` 是运行期接口：

```cpp
virtual void log(const LogEvent& event) = 0;
```

`AppenderProxy<Impl>` 是模板适配器。它内部持有：

- 一个 `LogFormatter`
- 一个具体 appender 实现对象，比如 `StdoutAppender`、`RollingFileAppender`、`SocketAppender`、`SqlAppender`

### 6.3 如何写的

`AppenderProxy` 使用 concept 约束实现类型：

```cpp
template <typename T>
concept IsAppenderImpl = requires(T x, const LogFormatter& fmter, const LogEvent& event) {
    { x.log(fmter, event) } -> std::same_as<void>;
};
```

这意味着所有具体 appender 都必须提供：

```cpp
void log(const LogFormatter& fmter, const LogEvent& event);
```

`AppenderProxy::log` 中把 facade 的调用转发给具体实现：

```cpp
impl_.log(formatter_, event);
```

### 6.4 设计思路

这里是“编译期约束 + 运行期多态”的组合。

编译期：

- concept 确保具体 appender 的接口符合要求。
- 如果某个 appender 没有正确实现 `log`，编译期就会报错。

运行期：

- `Logger` 只保存 `Sptr<AppenderFacade>`。
- 不同 appender 类型可以放进同一个 `vector`。

### 6.5 解决的问题

C++ 容器不能直接存放不同具体类型的对象。如果没有抽象层，`Logger` 就只能写死某几个 appender 类型。`AppenderFacade + AppenderProxy` 解决了这个问题。

### 6.6 优点

- 新增 appender 成本低。
- `Logger` 不需要知道具体输出端类型。
- 每个 appender 可以拥有自己的 formatter。
- concept 提升了模板错误的可读性。

### 6.7 可以继续优化的点

- 可以给 `AppenderFacade` 增加 `flush`、`name`、`metrics` 等可选能力。
- 可以让 appender 返回写入结果或错误码，方便监控失败率。

## 7. PatternItem 与 LogFormatter 格式化模块

### 7.1 代码位置

- `include/logger/LogFormatter.h`
- `src/LogFormatter.cpp`
- `include/logger/PatternItemFacade.h`
- `include/logger/PatternItemProxy.hpp`
- `src/PatternItemImpl.cpp`

### 7.2 模块职责

格式化模块负责把结构化的 `LogEvent` 转换成最终字符串。

支持的 pattern 包括：

- `%m`：消息
- `%p`：日志级别
- `%c`：logger 名称
- `%d{...}`：日期时间
- `%r`：累计运行毫秒数
- `%f`：文件名
- `%l`：行号
- `%v`：函数名
- `%t`：线程 id
- `%F`：协程 id
- `%N`：线程名称
- `%T`：制表符
- `%n`：换行符
- `%%`：百分号

### 7.3 如何写的

`LogFormatter` 构造时接收 pattern，并调用 `startParse_()` 解析。

解析器使用三种状态：

- `NORMAL`：普通字符串状态。
- `PATTERN`：读到了 `%` 后，解析一个格式符。
- `SUBPATTERN`：解析 `%d{...}` 这种带参数的格式符。

普通文本会被包装成 `StringFormatItem`，占位符会通过工厂函数创建对应的 format item。

例如：

- `%m` 对应 `MessageFormatItem`
- `%p` 对应 `LevelFormatItem`
- `%f` 对应 `FilenameFormatItem`
- `%l` 对应 `LineFormatItem`
- `%v` 对应 `FunctionNameFormatItem`
- `%d{...}` 对应 `DateTimeFormatItem`

每个 item 都实现：

```cpp
size_t format(std::ostream& os, const LogEvent& event);
```

最后 `LogFormatter::format` 遍历 `pattern_items_`，依次把每个字段写入输出流。

### 7.4 设计思路

pattern 只在 formatter 构造时解析一次，后续每条日志只需要遍历已经构建好的 item 列表。

这比每次输出日志都重新解析 pattern 更高效。

同时，每个格式项被拆成独立小类，有利于扩展。例如要新增 `%P` 输出进程 id，只需要：

1. 新增 `ProcessIdFormatItem`。
2. 在注册表里加一项。
3. pattern 中支持 `%P`。

### 7.5 解决的问题

日志格式如果写死在 appender 里，会导致每个输出端都重复拼字符串。现在通过 formatter 统一格式化逻辑，让 appender 只关心输出目标。

### 7.6 优点

- pattern 可配置。
- 解析和执行分离，运行时效率更好。
- 支持不同 appender 使用不同格式。
- pattern item 可扩展。
- 输出到 stream 和输出成 string 两种形式都支持。

### 7.7 可以继续优化的点

- `SUBPATTERN` 状态里目前默认通过 map 取工厂函数，如果未知格式符可能产生默认行为，后续可以显式检查。
- 解析错误现在使用 `assert` 和 `std::cerr`，生产环境可以改成异常或错误状态。
- `tellp()` 在某些 stream 上可能不可用或返回异常位置，统计 size 可以用 formatter item 自己计算或改成只返回字符串。

## 8. EventFixedBuffer 固定缓冲模块

### 8.1 代码位置

- `include/logger/EventFixedBuffer.hpp`

### 8.2 模块职责

`EventFixedBuffer` 是异步日志器使用的固定容量事件缓冲区。

它内部使用：

```cpp
std::vector<LogEvent> data_;
size_t count_;
```

构造时一次性分配指定容量，后续通过 `append` 把事件移动进数组。

### 8.3 如何写的

`append(LogEvent event)` 会判断 `count_ < data_.size()`：

- 如果有空间，把事件移动到 `data_[count_]`，然后 `++count_`。
- 如果满了，返回 false。

`getEventSpan()` 返回 `std::span<const LogEvent>`，让后台线程可以遍历当前有效事件，而不是暴露整个 vector。

`reset()` 只把 `count_` 置 0，不释放内存。

### 8.4 设计思路

异步日志追求的是前台写入快、内存分配少。

固定缓冲的好处是：

- 容量已知。
- 写入只是移动赋值和计数加一。
- 重用缓冲时不释放内存。

### 8.5 解决的问题

如果每条日志都单独 `new` 或 push 到无限增长的队列，会造成大量内存分配和不可控内存增长。固定缓冲把内存使用限制住，也方便后续做过载保护。

### 8.6 优点

- 内存复用。
- 结构简单。
- 与 `AsyncLogger` 的双缓冲模型匹配。
- `std::span` 提供只读视图，避免拷贝。

### 8.7 可以继续优化的点

- 当前 `data_(capacity)` 会默认构造 capacity 个 `LogEvent`，如果 `LogEvent` 未来变重，可以考虑原始存储或 ring buffer。
- 可以增加 `full()`、`empty()` 等语义函数，提升可读性。

## 9. AsyncLogger 异步日志器模块

### 9.1 代码位置

- `include/logger/AsyncLogger.h`

### 9.2 模块职责

`AsyncLogger` 是系统的性能核心。它继承 `Logger`，在同步 logger 的路由能力前面加了一层异步缓冲。

它负责：

- 前台线程快速写入缓冲。
- 后台线程批量取缓冲。
- 定时 flush。
- 过载时丢弃日志。
- 记录运行指标。

### 9.3 关键数据结构

主要成员包括：

```cpp
LoggerConfig config_;
std::thread thread_;
std::atomic<bool> running_;
std::mutex mutex_;
std::condition_variable cond_;
std::latch latch_{1};

EventBufferPtr current_buffer_;
EventBufferPtr next_buffer_;
std::vector<EventBufferPtr> buffers_to_write_;

std::atomic<uint64_t> accepted_events_{0};
std::atomic<uint64_t> dropped_events_{0};
std::atomic<uint64_t> written_events_{0};
```

其中：

- `current_buffer_`：前台当前写入的缓冲。
- `next_buffer_`：备用空缓冲，减少临时分配。
- `buffers_to_write_`：已经满了、等待后台线程写出的缓冲队列。
- `accepted_events_`：成功进入异步系统的事件数。
- `dropped_events_`：过载丢弃的事件数。
- `written_events_`：后台实际写出的事件数。

### 9.4 前台 append 思路

`append(LogEvent event)` 是业务线程调用的核心函数。

逻辑是：

1. 加锁保护缓冲区状态。
2. 如果 `current_buffer_` 还有空间，直接 append。
3. 如果当前缓冲满了，检查 `buffers_to_write_` 是否超过 `max_pending_buffers`。
4. 如果积压过多，增加 `dropped_events_` 并返回。
5. 如果还能接收，把满缓冲放入 `buffers_to_write_`。
6. 切换到 `next_buffer_` 或新建缓冲。
7. 写入当前事件。
8. 释放锁后唤醒后台线程。

### 9.5 后台线程思路

`threadFunc_()` 是后台线程。

它做的事情是：

1. 使用 `wait_for` 等待唤醒或超时。
2. 如果有待写缓冲，或当前缓冲中有数据，就把当前缓冲也切出去。
3. 用 `swap` 把 `buffers_to_write_` 转移到局部 `buffers_to_process`。
4. 释放锁。
5. 遍历所有事件，调用 `this->log(event)`，复用 `Logger` 的多端路由能力。
6. 尽量回收两个空缓冲，供后续复用。

这个设计的关键点是：后台线程做 I/O 时不持有锁。锁只保护缓冲队列切换，真正慢的输出在锁外执行。

### 9.6 过载保护

`AsyncLogger` 的过载保护主要有两层。

第一层在前台 `append`：

当 `buffers_to_write_.size() >= config_.max_pending_buffers` 时，直接丢弃事件，并累加 `dropped_events_`。

第二层在后台处理：

如果 `buffers_to_process.size() > config_.max_pending_buffers`，会擦除一部分积压缓冲，只保留前面部分。

这体现了一个工程取舍：日志是辅助数据，不能为了保证每条日志都写出而拖垮业务进程。高压情况下丢日志比内存无限增长更安全。

### 9.7 解决的问题

- 文件、网络、数据库 I/O 可能很慢，不能阻塞业务线程。
- 日志突增时不能无限占用内存。
- 后台写出需要批量化，降低频繁 I/O 开销。
- 需要能观测当前系统是否有积压、是否丢日志。

### 9.8 优点

- 前台路径短：多数情况下只是加锁、移动事件、计数。
- 双缓冲减少分配。
- `condition_variable` 支持满缓冲唤醒。
- `wait_for` 支持定时 flush，避免低流量时日志一直留在内存。
- metrics 完整，适合接入 MCP 或监控。

### 9.9 可以继续优化的点

- `running_` 是 atomic，但 `threadFunc_` 的循环退出后可以考虑处理最后残留在 `current_buffer_` 的事件，确保 stop 时尽量不丢尾部日志。
- 后台过载删除缓冲时目前没有同步增加 `dropped_events_`，指标可以进一步精确。
- 可以把 mutex 队列改成无锁 MPSC 队列，但复杂度会明显上升。
- 如果 appender 本身很慢，可以考虑每个 appender 独立线程，避免一个慢输出端拖慢所有输出端。

## 10. StdoutAppender 模块

### 10.1 代码位置

- `include/logger/LoggerAppender.h`
- `src/LoggerAppender.cpp`

### 10.2 模块职责

`StdoutAppender` 负责把日志写到标准输出。

实现很直接：

```cpp
void StdoutAppender::log(const LogFormatter& fmter, const LogEvent& event) {
    fmter.format(std::cout, event);
}
```

### 10.3 设计思路

控制台输出是最基础的 appender，适合开发调试、demo、容器日志采集。

它没有额外线程，也没有队列，依赖外层 `AsyncLogger` 来决定是否异步。

### 10.4 解决的问题

提供默认输出能力，保证系统即使没有复杂配置，也能看到日志。

### 10.5 优点

- 实现简单。
- 依赖少。
- 适合作为默认 appender。

### 10.6 可以继续优化的点

- 可以给不同日志级别加颜色。
- 可以考虑 `std::cerr` 输出错误级别日志。
- 多线程同步输出时可以增加锁，避免不同日志交叉。

## 11. RollingFileAppender 滚动文件模块

### 11.1 代码位置

- `include/logger/LoggerAppender.h`
- `src/LoggerAppender.cpp`

### 11.2 模块职责

`RollingFileAppender` 负责把日志写入文件，并在满足条件时滚动文件。

滚动条件包括：

- 文件大小超过 `max_bytes_`
- 文件打开时间超过 `roll_interval_`

同时它还有 flush 策略：

- 每 3 秒 flush 一次
- 或每 1024 次写入强制 flush

### 11.3 如何写的

构造函数保存文件名、基础文件名、最大大小、滚动间隔，然后调用 `openFile_()`。

`openFile_()` 使用：

```cpp
std::ios::out | std::ios::app | std::ios::binary
```

以追加和二进制模式打开文件，并通过 `tellp()` 初始化当前 offset。

`shouldRoll_()` 负责判断是否需要滚动：

- `offset_ > max_bytes_` 时滚动。
- 当前时间减去 `last_open_time_` 大于滚动间隔时滚动。

`rollFile_()` 会：

1. 关闭当前文件。
2. 调用 `getNewLogFileName_()` 生成带时间戳的文件名。
3. 使用 `std::rename` 重命名旧文件。
4. 打开新的同名文件继续写。

`log()` 中加锁保护文件流，先判断滚动，再格式化写入，最后按时间或次数 flush。

### 11.4 设计思路

文件日志不能无限增长，否则线上服务长期运行后会占满磁盘。滚动文件 appender 通过“大小 + 时间”双条件控制单个文件规模。

flush 也做了折中：

- 每条日志都 flush，可靠性高但性能差。
- 从不 flush，性能好但崩溃时容易丢更多日志。
- 当前采用定时和计数结合，是常见工程折中。

### 11.5 解决的问题

- 防止单个日志文件无限增长。
- 避免每条日志都 flush 导致性能下降。
- 文件写入加锁保证多线程安全。
- 文件打开失败、重命名失败会抛出系统错误，便于发现问题。

### 11.6 优点

- 支持大小滚动和时间滚动。
- 支持追加写，进程重启后不会覆盖已有日志。
- flush 策略减少系统调用开销。
- 使用 `std::filesystem` 和 `std::chrono`，代码表达现代化。

### 11.7 可以继续优化的点

- 滚动时如果目标文件名重复，可能覆盖或重命名失败，可以追加进程 id 或序号。
- 当前 `basename_` 成员保存了但使用较少，可以清理或用于命名。
- 可以增加保留文件数量和磁盘清理策略。
- 可以增加异步文件 appender，让文件写入单独成队列，不过目前外层 `AsyncLogger` 已经承担了主要异步能力。

## 12. SocketAppender 网络输出模块

### 12.1 代码位置

- `include/logger/LoggerAppender.h`
- `src/LoggerAppender.cpp`
- `test/socket_test.cpp`
- `logger_socket_tcp.json`
- `logger_socket_udp.json`

### 12.2 模块职责

`SocketAppender` 负责把日志发送到远端日志服务，支持：

- TCP
- UDP
- 域名或 IP 解析
- 内部消息队列
- 后台发送线程
- TCP 断线重连
- 队列满时丢弃旧消息

### 12.3 如何写的

构造函数接收：

```cpp
host
port
protocol
max_queue
reconnect_interval_ms
```

初始化时会：

1. 填充 `sockaddr_in remote_addr_`。
2. 优先使用 `inet_pton` 解析 IP。
3. 如果不是纯 IP，使用 `getaddrinfo` 做 DNS 解析。
4. 如果是 UDP，提前创建 socket。
5. 启动后台发送线程。

`log()` 的前台逻辑是：

1. 使用 formatter 把事件转成字符串。
2. 加锁。
3. 如果队列长度超过 `max_queue_`，不断弹出旧消息。
4. 把新消息入队。
5. 唤醒后台线程。

后台 `workerLoop_()` 做：

1. 等待队列非空或 stop。
2. 从队列取一条消息。
3. 释放锁。
4. 根据协议调用 `sendTcp_` 或 `sendUdp_`。

TCP 发送使用 `sendTcp_()`，其中会循环发送直到所有字节发完。如果发送失败，关闭 fd，下次重新连接。

UDP 发送使用 `sendto`，不维护连接。

### 12.4 设计思路

网络 I/O 是非常容易阻塞或失败的输出端，所以 appender 内部又加了一层后台线程和有界队列。

这和 `AsyncLogger` 的异步缓冲不是完全重复：

- `AsyncLogger` 解决日志系统整体前后端解耦。
- `SocketAppender` 内部队列解决网络发送端独立背压。

如果网络接收端很慢，SocketAppender 不会让调用方无限阻塞，而是丢弃旧消息保证新消息有机会进入队列。

### 12.5 解决的问题

- 网络抖动时业务线程不被拖住。
- TCP 断线后支持重连。
- UDP 场景下可以低成本发送。
- 队列有上限，避免内存无限增长。

### 12.6 优点

- 支持 TCP 和 UDP 两种模式。
- 内部队列有界，具备背压保护。
- 网络发送在锁外执行，降低锁占用。
- 测试中有慢接收端压测场景，验证前台 log 调用保持低延迟。

### 12.7 可以继续优化的点

- 当前队列满时丢弃旧消息，但没有暴露 dropped 指标，可以增加统计。
- TCP 重连是固定间隔，可以改成指数退避。
- 可以支持非阻塞 socket 和 epoll，适合更高吞吐场景。
- 可以增加 TLS 或日志协议封装。

## 13. SqlAppender 数据库输出模块

### 13.1 代码位置

- `include/logger/LoggerAppender.h`
- `src/LoggerAppender.cpp`
- `src/LogManager.cpp`
- `logger_sql.json`
- `src/json_config_demo.cpp`

### 13.2 模块职责

`SqlAppender` 负责把日志写入数据库。它不是每条日志同步执行 SQL，而是：

- 前台构造 SQL 并入队。
- 后台线程批量 flush。
- 支持 batch size。
- 支持定时 flush。
- 执行失败输出到 stderr，避免递归调用日志系统。

### 13.3 如何写的

构造函数接收：

```cpp
table_name
executor
batch_size
flush_interval_ms
```

其中 `executor` 是：

```cpp
using SqlExecutor = std::function<void(const std::string& sql)>;
```

这使 `SqlAppender` 不直接绑定 MySQL API。真正的 MySQL executor 在 `src/LogManager.cpp` 中根据配置构建。

`SqlAppender::log()` 负责：

1. 从 `LogEvent` 提取 level、logger、thread_id、timestamp、file、line、message。
2. 调用 `buildInsertSql_()` 构造 INSERT 语句。
3. 把 SQL 放入 `pending_sqls_`。
4. 如果达到 batch size，唤醒后台线程。

`workerLoop_()` 使用 `wait_for` 等待：

- 有数据
- 超时
- stop 信号

取数据时使用 `batch.swap(pending_sqls_)`，快速把队列交换到局部变量，然后释放锁，在锁外执行 SQL。

### 13.4 MySQL executor 思路

`src/LogManager.cpp` 中通过条件编译检测 MySQL 或 MariaDB 头文件。

如果存在客户端库，就定义 `MysqlExecutorState`：

- 保存 host、port、user、password、database。
- 懒连接或重连。
- `execute` 中调用 `mysql_query`。
- 如果遇到连接错误，重连后重试一次。

然后 `buildMysqlExecutor` 返回一个 lambda，捕获 `shared_ptr<MysqlExecutorState>`，保证 executor 状态生命周期安全。

如果没有 MySQL 客户端头文件，就返回一个会抛异常的 executor，提示安装依赖。

### 13.5 解决的问题

数据库写入比文件更慢、更容易失败。如果每条日志都同步写数据库，业务线程会被数据库性能拖住。`SqlAppender` 通过后台线程和批量提交降低影响。

### 13.6 优点

- `SqlAppender` 与具体数据库连接解耦，通过 `SqlExecutor` 注入。
- 后台批量提交，降低前台开销。
- SQL 字符串做了单引号转义。
- 数据库错误不会递归进入日志系统，而是写 stderr。
- MySQL 连接支持断线重连。

### 13.7 可以继续优化的点

- 当前构造的是单条 INSERT，可以改成 multi-values 批量 INSERT，性能会更高。
- 表名没有转义，配置来源不可信时需要白名单校验。
- 密码不应该写在 JSON 中，建议用环境变量或密钥管理。
- SQL 拼接可以改成 prepared statement。
- 可以增加失败重试队列或落盘补偿。

## 14. LoggerConfig 配置化管理模块

### 14.1 代码位置

- `include/logger/LoggerConfig.h`
- `src/LoggerConfig.cpp`
- `logger_config.json`
- `logger_socket_tcp.json`
- `logger_socket_udp.json`
- `logger_sql.json`

### 14.2 模块职责

`LoggerConfig` 把日志系统参数配置化，包括：

- 异步缓冲参数
- 日志级别
- logger 名称
- appender 列表
- 每个 appender 的具体参数

### 14.3 配置结构

顶层字段：

```json
{
  "logger_name": "json_async",
  "level": "INFO",
  "event_count": 128,
  "flush_interval": 2,
  "max_pending_buffers": 32,
  "appenders": []
}
```

appender 可以是：

- `stdout`
- `rolling_file`
- `socket`
- `sql`

每种 appender 有自己的字段。例如 rolling file 有：

```json
{
  "type": "rolling_file",
  "file": "json_async.log",
  "max_file_size": 1048576,
  "roll_interval_seconds": 3600,
  "pattern": "%d{%Y-%m-%d %H:%M:%S} [%p] [%c] %f:%l %m%n"
}
```

### 14.4 如何写的

`LoggerConfig::loadFromJsonFile` 使用 nlohmann json 读取文件。

解析时会检查字段存在和类型，例如：

- `event_count` 必须是无符号数字。
- `flush_interval` 必须是整数。
- `level` 必须是字符串。
- `appenders` 必须是数组。

解析完成后调用 `normalizeConfig` 做兜底：

- `event_count == 0` 时恢复默认值 64。
- `flush_interval <= 0` 时恢复默认值 3。
- `max_pending_buffers < 2` 时修正为 2。
- logger 名为空时使用 `root`。
- appender 列表为空时默认加一个 stdout。
- pattern 为空时使用默认 pattern。

### 14.5 设计思路

把配置解析和 logger 构建分开。

`LoggerConfig` 只负责把 JSON 转换成内存配置对象，不直接创建 logger。真正创建 logger 和 appender 的逻辑在 `LoggerManager` 中。

这种设计使配置模块更纯粹，也方便单独测试。

### 14.6 解决的问题

- 避免日志输出目标写死在代码中。
- 支持不同环境使用不同配置。
- 支持运行时 reload。
- 支持多 appender 同时配置。

### 14.7 优点

- 默认值完整，不容易因为少配字段导致崩溃。
- 类型检查相对严格。
- appender 配置集中，扩展新 appender 时有明确入口。
- JSON 易读，适合面试展示。

### 14.8 可以继续优化的点

- 对未知字段可以给出 warning。
- 对非法 appender type 当前默认 stdout，后续可以改成报错，避免配置写错却静默降级。
- 密码等敏感字段建议支持环境变量引用。
- 可以支持多 logger 配置，而不是单文件单 logger。

## 15. LoggerManager 日志器管理模块

### 15.1 代码位置

- `include/logger/LogManager.h`
- `src/LogManager.cpp`
- `common/singleton.hpp`

### 15.2 模块职责

`LoggerManager` 是日志系统的注册中心和工厂。

它负责：

- 管理 root logger。
- 按名称获取或创建普通 logger。
- 查找 logger。
- 查找 async logger。
- 根据 `LoggerConfig` 创建 async logger。
- 从 JSON 文件加载 logger。
- 运行时重载 async logger 配置。
- 根据 appender config 构造具体 appender。

### 15.3 如何写的

`LoggerMgr` 是：

```cpp
using LoggerMgr = Cot::Singleton<LoggerManager>;
```

底层是 Meyers Singleton，`common/singleton.hpp` 中使用函数内 static 对象保证 C++11 之后的线程安全初始化。

`LoggerManager` 内部维护：

```cpp
Sptr<Logger> root_;
std::unordered_map<std::string, Sptr<Logger>> loggers_;
std::unordered_map<std::string, Sptr<AsyncLogger>> async_loggers_;
std::mutex mtx_;
```

`getLogger` 按名称查找，不存在就创建普通 `Logger`。

`getOrCreateAsyncLogger` 根据配置创建 `AsyncLogger`，然后调用 `configureAsyncLogger_` 挂 appender。

`reloadAsyncLoggerFromFile` 重新读取 JSON，如果 logger 已存在，就清空旧 appender 并按新配置重新挂载。

### 15.4 Appender 构建逻辑

`src/LogManager.cpp` 中的 `buildAppender` 是配置和实现之间的桥。

它根据 `AppenderConfig::Type` 创建：

- `AppenderProxy<StdoutAppender>`
- `AppenderProxy<RollingFileAppender>`
- `AppenderProxy<SocketAppender>`
- `AppenderProxy<SqlAppender>`

每个 appender 都传入独立的 `LogFormatter{cfg.pattern}`。

### 15.5 设计思路

`LoggerManager` 承担装配职责，把系统各模块连起来：

- 配置模块负责解析。
- manager 负责实例化。
- logger 负责路由。
- appender 负责输出。

这符合依赖集中装配的思路，避免业务代码到处 new appender。

### 15.6 解决的问题

- 全局日志器统一管理。
- 避免同名 logger 重复创建。
- 支持运行时查找和调级别。
- 支持配置驱动创建复杂 logger。
- 支持 MCP 工具访问 logger。

### 15.7 优点

- 对外 API 简单。
- 内部用 mutex 保护 map。
- root logger 默认挂 stdout，有保底能力。
- 异步 logger 同时放进 `async_loggers_` 和 `loggers_`，方便普通查找和异步指标查询。

### 15.8 可以继续优化的点

- `reload` 时如果 logger 正在输出，清空 appender 可能和 `Logger::log` 遍历并发冲突，需要进一步同步。
- 可以支持删除 logger。
- 可以支持多配置文件合并。
- 可以增加 logger 层面的 metrics。

## 16. MCP 工具模块

### 16.1 代码位置

- `include/logger/LoggerMcpTools.h`
- `src/LoggerMcpTools.cpp`
- `src/logger_mcp_server.cpp`

### 16.2 模块职责

MCP 模块把日志系统的运维能力暴露成工具，让外部客户端可以通过 JSON-RPC 调用。

当前提供四个工具：

- `logger_set_level`
- `logger_reload_config`
- `logger_tail_file`
- `logger_get_metrics`

### 16.3 LoggerMcpTools 如何写的

`LoggerMcpTools` 是工具函数层，不直接处理 JSON-RPC 协议。

`logger_set_level`：

1. 把字符串 level 转成 `LogLevel`。
2. 查找 logger。
3. 设置 logger 级别。
4. 返回 bool 表示是否成功。

`logger_reload_config`：

1. 调用 `LoggerMgr::GetInstance().reloadAsyncLoggerFromFile(config_file)`。
2. 捕获异常，返回 bool。

`logger_tail_file`：

1. 打开日志文件。
2. 使用 `std::deque` 保存最后 N 行。
3. 返回 vector。

这个实现是一个典型 ring buffer 思路：不用把整个文件都存起来，只保留最后 N 行。

`logger_get_metrics`：

1. 查找普通 logger，获取是否存在和当前级别。
2. 如果是 async logger，进一步获取 async metrics。
3. 返回结构化的 `LoggerMetrics`。

### 16.4 MCP Server 如何写的

`src/logger_mcp_server.cpp` 是 stdio JSON-RPC server。

启动后循环：

```text
读取 stdin 一行
解析 JSON
根据 method 分发
写 stdout 一行 JSON 响应
flush
```

支持的方法包括：

- `initialize`
- `notifications/initialized`
- `tools/list`
- `tools/call`

`tools/list` 返回工具定义，包括工具名、描述、输入 schema。

`tools/call` 根据工具名调用对应的 `LoggerMcpTools` 函数。

### 16.5 设计思路

MCP 层没有直接侵入日志系统核心，而是作为一层薄适配：

```text
MCP JSON-RPC
        |
        v
LoggerMcpTools
        |
        v
LoggerManager / AsyncLogger / 文件
```

这使得日志核心即使不启用 MCP，也能正常工作。MCP 只是额外的管理入口。

### 16.6 解决的问题

真实服务运行时经常需要：

- 临时把某个 logger 调成 DEBUG。
- 修改配置并热重载。
- 看某个日志文件最后几行。
- 观察异步队列是否积压、是否丢日志。

MCP 工具把这些能力标准化，让外部 agent 或调试工具可以直接调用。

### 16.7 优点

- 使用 JSON-RPC，协议简单。
- 工具 schema 清晰，易被客户端发现。
- 工具层和日志核心解耦。
- 可以观测异步指标，体现工程完整度。

### 16.8 可以继续优化的点

- `logger_reload_config` 当前只返回 bool，可以返回错误信息。
- `logger_tail_file` 可以限制可读路径，避免读取任意文件。
- MCP server 可以支持 resources，比如列出已有 logger。
- 可以增加工具：创建 logger、列出 logger、查看 appender 配置、清空指标等。

## 17. 测试与 Demo 模块

### 17.1 代码位置

- `src/main.cpp`
- `src/json_config_demo.cpp`
- `test/socket_test.cpp`

### 17.2 main demo

`src/main.cpp` 展示了：

1. 从 `logger_config.json` 创建 async logger。
2. 启动 logger。
3. 输出 INFO 和 DEBUG。
4. 使用 MCP 工具修改级别。
5. 获取 metrics。
6. tail 日志文件。
7. reload 配置。
8. stop logger。

这个 demo 展示的是“配置加载 + 日志输出 + MCP 工具调用”的主链路。

### 17.3 json_config_demo

`src/json_config_demo.cpp` 使用 `logger_sql.json` 创建 logger，并输出多条日志。这主要展示 SQL appender 的配置化使用方式。

### 17.4 socket_test

`test/socket_test.cpp` 是 SocketAppender 的压测和行为验证。

它自建一个 TCP server，统计收到的日志数量和字节数。测试分为两种场景：

- 慢接收端：验证网络背压下，前台 `log()` 仍然低延迟。
- 快接收端：验证正常情况下可以保持较高吞吐和基本投递。

这个测试能支撑你面试时讲“不是只写了功能，还考虑了高压情况下的行为”。

### 17.5 可以继续优化的点

- 可以把 demo 编译命令收敛到 CMake。
- 可以增加单元测试覆盖 formatter、config parser、LogLevel 转换。
- 可以增加 AsyncLogger stop 时尾部日志是否写出的测试。

## 18. 面试推荐讲法

### 18.1 一分钟版本

我做的是一个 C++ 日志系统，整体采用配置驱动和异步缓冲设计。业务侧通过宏创建 `LogEvent`，自动采集时间、线程、文件、行号和函数名。普通 `Logger` 负责级别过滤和多 appender 路由，`AsyncLogger` 在前面加了固定缓冲、双缓冲切换和后台线程，避免业务线程被文件、网络、数据库 I/O 阻塞。输出端通过 `AppenderFacade + AppenderProxy` 做类型擦除，具体支持 stdout、滚动文件、Socket、SQL。格式化通过 pattern 解析成多个 format item，支持不同 appender 使用不同格式。配置通过 JSON 管理，运行时还接了 MCP 工具，可以调级别、重载配置、tail 文件、查看异步队列指标。

### 18.2 重点亮点

1. 异步缓冲和过载保护。
2. `concept + facade/proxy` 实现可扩展 appender。
3. `std::source_location` 自动记录源码位置。
4. JSON 配置化和热重载。
5. 多端路由：stdout、文件、网络、SQL。
6. MCP 工具化运维能力。

### 18.3 面试官可能追问

#### 为什么需要异步日志？

因为日志输出目标可能很慢，尤其是文件 flush、网络发送、数据库写入。如果在业务线程同步执行，会增加请求延迟。异步日志把业务线程和 I/O 解耦，业务线程只写内存缓冲，后台线程批量输出。

#### 为什么要丢日志？

在过载情况下，日志不是主业务数据。为了保证进程稳定，不能无限堆积内存，也不能让业务线程一直阻塞。所以设置 `max_pending_buffers`，超过阈值时丢弃日志并记录 dropped 指标。

#### 为什么每个 appender 都有 formatter？

因为不同输出端需要不同格式。控制台希望简洁，文件希望详细，SQL 希望结构化，Socket 可能希望带固定协议字段。把 formatter 放在 appender proxy 内部，可以让每个输出端独立配置。

#### 为什么用 MCP？

MCP 让日志系统不只是内部库，还具备运行期管理能力。外部工具可以标准化调用 `logger_set_level`、`logger_reload_config`、`logger_tail_file`、`logger_get_metrics`，这对线上排查问题很有价值。

## 19. 后续优化路线

### 19.1 稳定性

- `Logger::level_` 显式默认初始化。
- `Logger::appenders_` 增删和遍历之间增加并发保护。
- `AsyncLogger::stop` 时确保刷完尾部日志。
- appender 异常隔离，避免一个输出端失败影响其他输出端。

### 19.2 性能

- SQL 改成批量 INSERT。
- Socket 改成非阻塞 I/O 或 epoll。
- AsyncLogger 可以减少锁粒度或使用 MPSC 队列。
- Formatter 可以进一步减少 `stringstream` 和 `tellp` 开销。

### 19.3 可观测性

- 每个 appender 增加独立 metrics。
- SocketAppender 增加 dropped、send_failed、reconnect_count。
- SqlAppender 增加 pending_count、failed_count、last_error。
- MCP 增加 list loggers 和 list appenders。

### 19.4 配置安全

- SQL 密码改用环境变量。
- MCP tail 限制文件路径。
- 配置非法字段返回明确错误。
- 支持 schema 校验。

## 20. 总结

这个日志系统已经具备一个工程化日志框架的主要骨架：

- 用 `LogEvent` 保存结构化日志。
- 用 `Logger` 做级别过滤和多端路由。
- 用 `AsyncLogger` 做异步缓冲和过载保护。
- 用 `LogFormatter` 做 pattern 格式化。
- 用 `AppenderProxy` 做输出端扩展。
- 用 JSON 做配置化管理。
- 用 MCP 做运行期控制和观测。

面试时不要只说“我实现了日志系统”，而要强调它解决的实际问题：降低业务线程延迟、控制内存增长、支持多目标输出、支持配置热更新、支持运行期观测。这些点比单纯打印日志更接近真实后端工程。
