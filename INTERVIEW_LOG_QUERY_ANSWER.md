# 面试题回答：高并发下如何查找某个业务的某个日志等级

## 1. 面试官问题

如果有多个前端业务同时写入日志，在高并发状态下，想要查找某一个业务的某一个日志等级对应的日志，我是怎么做的？我是否实现了这个功能？

我的回答要分两层：

1. **日志写入阶段是否支持区分业务和日志等级。**
2. **日志查询阶段是否已经实现按业务和等级检索。**

结论是：

> 我的日志系统在写入阶段已经保存了“业务标识”和“日志等级”这两个关键信息；如果使用 SQL Appender，这两个字段会结构化写入数据库，因此可以通过 SQL 查询实现“按业务 + 等级”查找。  
> 但如果是普通文件、stdout 或 socket 输出，目前系统本身没有实现一个内置的过滤查询 API。MCP 里只实现了 tail 文件和 metrics，没有实现 `logger_query_logs(logger_name, level)` 这种精确查询工具。  
> 所以严格来说：**结构化数据基础已经实现，SQL 场景下可以查询；通用查询功能还没有完整实现。**

## 2. 高并发写入时，业务和等级是怎么被记录的

### 2.1 业务标识来自 logger_name

在我的设计里，一个业务可以对应一个独立的 logger。比如：

```json
{
  "logger_name": "order_service",
  "level": "INFO"
}
```

或者：

```json
{
  "logger_name": "payment_service",
  "level": "WARN"
}
```

这个 `logger_name` 会进入 `LoggerConfig`：

- `include/logger/LoggerConfig.h`
  - `std::string logger_name = "root";`

配置加载时会读取 JSON 里的 `logger_name`：

- `src/LoggerConfig.cpp`
  - `cfg.logger_name = j.at("logger_name").get<std::string>();`

创建异步 logger 时，`AsyncLogger` 会把 `config.logger_name` 传给父类 `Logger`：

- `include/logger/AsyncLogger.h`
  - `: Logger(config.logger_name)`

所以，如果多个业务分别使用不同 logger，例如：

```cpp
auto order_logger = LoggerMgr::GetInstance().getOrCreateAsyncLoggerFromFile("order_logger.json");
auto pay_logger = LoggerMgr::GetInstance().getOrCreateAsyncLoggerFromFile("payment_logger.json");
```

那么日志事件里就可以通过 logger name 区分业务。

### 2.2 日志等级来自宏

业务代码调用不同宏时，日志等级会被固定下来：

- `LOG_DEBUG` 对应 `LogLevel::DEBUG`
- `LOG_INFO` 对应 `LogLevel::INFO`
- `LOG_WARN` 对应 `LogLevel::WARN`
- `LOG_ERROR` 对应 `LogLevel::ERROR`
- `LOG_FATAL` 对应 `LogLevel::FATAL`

代码位置：

- `common/LogMacros.h`

示意：

```cpp
#define LOG_INFO(logger, fmt, ...) \
    LOG_LEVEL((logger), LogLevel::INFO, (fmt) __VA_OPT__(, ) __VA_ARGS__)
```

也就是说：

```cpp
LOG_ERROR(order_logger, "create order failed, order_id={}", order_id);
```

会把这条日志的等级标记为 `ERROR`。

### 2.3 make_event 会把 logger_name 和 level 写进 LogEvent

核心事件构造函数是：

- `common/LogMacros.h`
  - `logger_macro_detail::make_event`

它会构造 `LogEvent`，其中前两个核心字段就是：

```cpp
std::string{logger.getLoggerName()}, // logger_name
level,                               // log level
```

因此，每条日志事件在进入系统时，已经携带了：

```text
业务标识：logger_name
日志等级：LogLevel
线程信息：thread_id
时间信息：timestamp
源码位置：file / line / function
用户消息：message
```

### 2.4 LogEvent 内部保存结构化字段

`LogEvent` 的定义在：

- `include/logger/LogEvent.h`

它保存了：

```cpp
std::string logger_name_;
LogLevel level_;
uint32_t thread_id_;
std::time_t timestamp_;
std::source_location source_loc_;
std::stringstream custom_msg_;
```

并通过接口暴露：

```cpp
std::string_view getLoggerName() const;
LogLevel getLevel() const;
std::string getContent() const;
```

所以这里不是只保存一段纯文本，而是保存了一条结构化事件。这一点很重要，因为只有结构化事件后面才方便按字段查询。

## 3. 高并发写入时，事件怎么进入日志系统

### 3.1 多个业务线程同时调用日志宏

假设多个业务同时写日志：

```cpp
LOG_INFO(order_logger, "create order id={}", id);
LOG_ERROR(payment_logger, "pay failed id={}", id);
LOG_WARN(user_logger, "login retry user={}", uid);
```

每一次宏调用都会生成自己的 `LogEvent`。

数据流是：

```text
业务线程
  -> LOG_INFO / LOG_ERROR
  -> LOG_LEVEL
  -> make_event
  -> event.print(...)
  -> submit_log(...)
```

### 3.2 如果是普通 Logger

如果传入的是普通 `Logger`，`submit_log` 会走：

```cpp
logger.log(event);
```

然后：

```text
Logger::log
  -> 判断日志等级
  -> 遍历 appenders_
  -> appender->log(event)
  -> formatter 格式化
  -> 输出
```

普通 logger 是同步输出，调用线程会直接进入 appender。

### 3.3 如果是 AsyncLogger

如果传入的是 `AsyncLogger`，`submit_log` 会走：

```cpp
logger.append(std::move(event));
```

然后：

```text
AsyncLogger::append
  -> 加锁
  -> 写入 current_buffer_
  -> 缓冲满时切到 buffers_to_write_
  -> 通知后台线程
  -> 后台线程批量消费
  -> this->log(event)
  -> Logger::log
  -> appender 输出
```

代码位置：

- `include/logger/AsyncLogger.h`
  - `append(LogEvent event)`
  - `threadFunc_()`

这里对高并发的处理是：

- 前端业务线程只需要把事件写入内存缓冲。
- 通过 `mutex_` 保护 `current_buffer_` 和 `buffers_to_write_`。
- 通过 `condition_variable` 唤醒后台线程。
- 通过 `max_pending_buffers` 限制最大积压。
- 过载时增加 `dropped_events_` 并丢弃日志，防止内存无限增长。

所以从写入角度看，高并发写入是支持的。

## 4. 如何查找“某业务 + 某等级”的日志

这里要看使用哪种输出端。

## 4.1 如果使用 SQL Appender：可以结构化查询

这是当前代码里最接近“按业务 + 等级查询”的实现。

`SqlAppender::log` 中会从 `LogEvent` 取出字段：

- `LevelToString(event.getLevel())`
- `event.getLoggerName()`
- `event.getThreadId()`
- `event.getTime()`
- `event.getFilename()`
- `event.getLine()`
- `event.getContent()`

代码位置：

- `src/LoggerAppender.cpp`
  - `SqlAppender::log`
  - `SqlAppender::buildInsertSql_`

生成的 SQL 类似：

```sql
INSERT INTO logs
  (level, logger, thread_id, timestamp, file, line, message)
VALUES
  ('ERROR', 'order_service', 12345, 1710000000, 'Order.cpp', 88, 'create order failed');
```

也就是说，SQL 表里天然有两个关键列：

```text
logger：业务标识
level ：日志等级
```

所以查找某个业务某个等级可以直接写：

```sql
SELECT *
FROM logs
WHERE logger = 'order_service'
  AND level = 'ERROR'
ORDER BY timestamp DESC;
```

如果再加时间范围：

```sql
SELECT *
FROM logs
WHERE logger = 'order_service'
  AND level = 'ERROR'
  AND timestamp BETWEEN 1710000000 AND 1710003600
ORDER BY timestamp DESC;
```

面试时可以说：

> 如果部署时启用了 SQL Appender，这个功能我是支持的。因为我写入数据库时没有只写纯文本，而是把 logger 和 level 拆成了独立字段，所以可以按业务名和日志等级查询。

### SQL 方案的优点

- 查询准确，不依赖字符串 grep。
- 可以按 logger、level、时间范围组合查询。
- 可以给 `(logger, level, timestamp)` 建联合索引。
- 适合高并发、多业务、线上排查场景。

### SQL 方案当前还可以优化

当前代码里只是生成 INSERT 语句，没有创建索引。真正生产环境建议表结构这样设计：

```sql
CREATE INDEX idx_logs_logger_level_time
ON logs(logger, level, timestamp);
```

这样查询：

```sql
WHERE logger = ? AND level = ? ORDER BY timestamp DESC
```

会更快。

另外当前 SQL 是字符串拼接，虽然做了单引号转义，但更生产级的做法是 prepared statement。

## 4.2 如果使用 RollingFileAppender：可以人工搜索，但没有内置精确查询 API

文件 appender 会把日志格式化成文本。

如果 pattern 中包含 `%p` 和 `%c`，那么输出里会有日志等级和 logger 名：

```json
{
  "type": "rolling_file",
  "file": "json_async.log",
  "pattern": "%d{%Y-%m-%d %H:%M:%S} [%p] [%c] %f:%l %m%n"
}
```

例如输出可能是：

```text
2026-05-14 10:00:00 [ERROR] [order_service] Order.cpp:88 create order failed
```

这种情况下，可以用外部命令查：

```bash
grep "\\[ERROR\\].*\\[order_service\\]" json_async.log
```

或者：

```bash
grep "\\[order_service\\]" json_async.log | grep "\\[ERROR\\]"
```

但是这不是我的日志系统内部实现的查询功能，而是依赖文本格式和外部 grep。

当前 MCP 工具有：

- `logger_tail_file`
- `logger_get_metrics`
- `logger_set_level`
- `logger_reload_config`

其中 `logger_tail_file` 只负责读取文件最后 N 行，没有按 logger 和 level 过滤。

所以文件场景下应该诚实回答：

> 文件日志里可以通过 pattern 把业务名和等级打出来，因此人工排查时能 grep；但我目前没有实现一个内置的按业务和等级过滤查询接口。

### 文件方案的优点

- 实现简单。
- 不依赖数据库。
- 适合本地调试和轻量部署。
- 滚动文件可以防止单文件无限增长。

### 文件方案的问题

- 查询依赖文本匹配，精确性不如结构化查询。
- 大文件 grep 成本高。
- 没有索引。
- MCP 目前只 tail，不 filter。

## 4.3 如果使用 SocketAppender：当前只负责投递，不负责查询

`SocketAppender` 的职责是把日志发给远端。

代码位置：

- `src/LoggerAppender.cpp`
  - `SocketAppender::log`
  - `SocketAppender::workerLoop_`
  - `sendTcp_`
  - `sendUdp_`

它会先格式化日志，再把字符串放入内部队列，由后台线程发送。

因此，在 socket 场景下：

- 本地系统负责发送。
- 查询能力取决于远端日志服务。
- 如果远端是 Elasticsearch、Loki、ClickHouse 或自研日志平台，那么可以在远端按 logger 和 level 查询。
- 但当前本项目内没有实现 socket 接收端的存储和查询。

所以应回答：

> SocketAppender 支持把业务名和等级格式化后发送出去，但查询功能依赖接收端日志平台。我的项目里实现了发送端，没有实现远端查询系统。

## 4.4 如果使用 StdoutAppender：只能依赖外部采集系统

`StdoutAppender` 只输出到标准输出。

容器环境里 stdout 通常会被 Docker、Kubernetes 或日志 agent 采集。

如果 pattern 包含 `%p` 和 `%c`，外部采集系统可以继续按字段或文本过滤。

但在我的 C++ 项目内部，stdout appender 不负责查询。

## 5. 我是否实现了这个功能

准确回答如下：

### 5.1 已经实现的部分

#### 1. 高并发写入支持

已经实现。

`AsyncLogger` 支持多个业务线程同时写日志：

- 前台线程进入 `append`。
- 使用 mutex 保护缓冲。
- 使用固定缓冲减少频繁分配。
- 使用后台线程批量输出。
- 使用 `max_pending_buffers` 做过载保护。
- 使用 `accepted_events_`、`dropped_events_`、`written_events_` 记录指标。

#### 2. 业务标识记录

已经实现。

`logger_name` 会进入 `LogEvent`：

```cpp
std::string{logger.getLoggerName()}
```

并且可以通过 `%c` 输出到文本日志，或者通过 SQL appender 写入 `logger` 列。

#### 3. 日志等级记录

已经实现。

日志等级通过宏进入 `LogEvent`：

```cpp
LogLevel::INFO
LogLevel::ERROR
```

并且可以通过 `%p` 输出到文本日志，或者通过 SQL appender 写入 `level` 列。

#### 4. SQL 场景下按业务和等级查询

基本实现。

因为 SQL appender 会写入：

```text
level
logger
thread_id
timestamp
file
line
message
```

所以可以通过 SQL 查询：

```sql
SELECT *
FROM logs
WHERE logger = 'order_service'
  AND level = 'ERROR';
```

### 5.2 没有完整实现的部分

#### 1. 没有统一的日志查询 API

目前没有类似下面这样的 C++ 或 MCP 接口：

```cpp
queryLogs(logger_name, level, begin_time, end_time)
```

也没有 MCP tool：

```text
logger_query_logs
```

#### 2. 文件日志没有索引

RollingFileAppender 只是写文件，不维护索引。

如果要查文件，只能：

- grep
- tail 后人工看
- 外部日志系统采集后查

#### 3. 当前 MCP tail 不支持过滤

`logger_tail_file(file_path, lines)` 只返回最后 N 行，不支持：

- logger_name 过滤
- level 过滤
- 时间范围过滤
- 正则过滤

#### 4. 如果多个业务共用同一个 logger，则不能稳定区分业务

当前系统把 `logger_name` 当作业务标识。

如果多个业务都用同一个 logger，例如都叫 `root`，那么系统内部就无法仅靠结构化字段区分它们。除非业务把业务名写进 message，但那就变成文本约定，不是结构化字段。

因此更推荐：

```text
一个业务或模块使用一个独立 logger_name
```

例如：

```text
order_service
payment_service
user_service
gateway
```

## 6. 面试时可以这样回答

我会这样回答面试官：

> 我这套日志系统在事件构造阶段已经把“业务”和“等级”结构化保存了。业务维度用 logger_name 表示，日志等级用 LogLevel 表示。业务线程调用 `LOG_INFO`、`LOG_ERROR` 这些宏时，宏会构造 `LogEvent`，把当前 logger 的名字、日志级别、线程 id、时间戳、source_location 和用户消息都放进去。  
>
> 在高并发写入场景下，如果使用 `AsyncLogger`，多个业务线程不会直接写文件或数据库，而是先把事件写入固定缓冲区。缓冲满后切换到待写队列，由后台线程批量消费。系统还通过 `max_pending_buffers` 做过载保护，避免日志积压导致内存无限增长。  
>
> 至于查询，如果使用 SQL Appender，我已经把 `level` 和 `logger` 作为独立字段写进数据库，所以可以通过 `WHERE logger = 'order_service' AND level = 'ERROR'` 查询特定业务的特定等级日志。  
>
> 但如果是文件、stdout 或 socket 输出，我目前只是把 `%p` 和 `%c` 格式化到日志文本里，可以通过 grep 或外部日志平台查；项目内部还没有实现统一的 `logger_query_logs` 查询 API。MCP 目前实现了 set level、reload config、tail file 和 get metrics，还没有做按 logger 和 level 过滤查询。  
>
> 所以严格说，我实现了支持这个能力的数据基础和 SQL 场景下的查询方式，但还没有把它抽象成统一的日志检索接口。

## 7. 如果继续优化，我会怎么做

如果要把这个功能补完整，我会按下面路线做。

### 7.1 增加统一查询接口

可以新增：

```cpp
struct LogQuery {
    std::string logger_name;
    LogLevel level;
    std::time_t begin_time;
    std::time_t end_time;
    size_t limit;
};
```

然后提供：

```cpp
queryLogs(const LogQuery& query)
```

### 7.2 MCP 增加 logger_query_logs

新增 MCP tool：

```text
logger_query_logs
```

参数：

```json
{
  "logger_name": "order_service",
  "level": "ERROR",
  "begin_time": 1710000000,
  "end_time": 1710003600,
  "limit": 100
}
```

返回匹配日志。

### 7.3 SQL 表增加索引

为高并发查询建联合索引：

```sql
CREATE INDEX idx_logs_logger_level_time
ON logs(logger, level, timestamp);
```

这样查询某业务某等级最近日志会更快。

### 7.4 文件日志增加结构化 JSON 输出

除了普通 pattern，可以新增 JSON formatter：

```json
{
  "timestamp": 1710000000,
  "logger": "order_service",
  "level": "ERROR",
  "file": "Order.cpp",
  "line": 88,
  "message": "create order failed"
}
```

这样即使写文件，也可以被日志采集系统稳定解析。

### 7.5 增加业务字段而不只依赖 logger_name

如果一个 logger 内部还要区分多个业务，可以给 `LogEvent` 增加字段：

```cpp
std::string business_id_;
std::string trace_id_;
std::string request_id_;
```

这样排查线上问题时，可以按：

```text
business_id + level + trace_id + time range
```

组合查询。

## 8. 最终总结

这个问题的核心不只是“能不能 grep 到日志”，而是日志系统有没有结构化保存可查询字段。

我的当前实现中：

- `logger_name` 可以表示业务。
- `LogLevel` 可以表示日志等级。
- `LogEvent` 保存了这两个字段。
- Formatter 可以把它们输出到文本。
- SQL Appender 会把它们写入数据库字段。
- AsyncLogger 支持高并发写入和过载保护。

但是：

- 文件日志没有索引。
- MCP tail 不支持过滤。
- 项目内没有统一查询 API。
- 多业务必须使用不同 logger_name 才能稳定区分。

所以最准确的结论是：

> **我实现了按业务和等级查询所需的数据结构和 SQL 落库能力；SQL 输出场景下可以查询。  
> 但我还没有实现跨所有输出端的统一查询接口，文件和 stdout 场景目前依赖 grep 或外部日志平台。**

