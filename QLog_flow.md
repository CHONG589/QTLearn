# 日志系统流程图

## 1. 整体架构

```mermaid
graph TB
    subgraph "业务线程"
        MACRO["LOG_INFO() << msg"]
        LS["LogStream<br/>(临时对象，收集消息)"]
    end

    subgraph "Logger（单例）"
        LOG["log()<br/>构造 LogEntry + 入队"]
        Q["m_queue<br/>QQueue&lt;LogEntry&gt;"]
    end

    subgraph "写线程（asyncLoop）"
        DEQ["dequeue 取出 LogEntry"]
        WTS["writeToSinks()"]
        FMT["m_formatter-&gtformat()<br/>ILogFormatter"]
    end

    subgraph "输出目标"
        FS["FileSink<br/>文件 + 滚动"]
        CS["ConsoleSink<br/>qDebug()"]
        NS["... 可扩展"]
    end

    MACRO --> LS
    LS -->|"析构时提交"| LOG
    LOG -->|"入队 + wake"| Q
    Q -->|"取出"| DEQ
    DEQ --> WTS
    WTS --> FMT
    FMT --> FS
    FMT --> CS
    FMT --> NS
```

## 2. 主流程：异步日志写入

```mermaid
sequenceDiagram
    participant B as 业务线程
    participant L as Logger
    participant Q as m_queue
    participant W as 写线程 (asyncLoop)
    participant F as ILogFormatter
    participant S as ILogSink[]

    B->>B: LOG_INFO() << "hello"
    Note over B: LogStream 临时对象 << 拼接字符串
    B->>L: ~LogStream() → log(level, msg, file, line)
    L->>L: level < m_level ?<br/>是 → 直接返回（过滤）
    L->>L: 构造 LogEntry{timestamp, level, msg, file, line}
    L->>Q: 加锁 → enqueue(entry)
    L->>W: m_cond.wakeOne()
    L->>Q: 解锁

    W->>Q: 加锁
    Q-->>W: dequeue(entry)
    W->>W: 解锁（I/O 期间不持锁）
    W->>F: format(entry) → formattedMsg
    W->>S: 加 m_sinkMutex → 遍历所有 sink
    S->>S: FileSink::write(msg)
    S->>S: ConsoleSink::write(msg)
    W->>Q: 重新加锁，检查队列
```

## 3. 同步模式流程

```mermaid
sequenceDiagram
    participant B as 业务线程
    participant L as Logger
    participant F as ILogFormatter
    participant S as ILogSink[]

    B->>L: log(level, msg, file, line)
    L->>L: 构造 LogEntry
    Note over L: m_async == false<br/>直接在当前线程处理
    L->>F: format(entry) → formattedMsg
    L->>S: 加锁 → 遍历所有 sink 写入
    S->>S: FileSink::write(msg)
    S->>S: ConsoleSink::write(msg)
```

## 4. FileSink 内部流程

```mermaid
flowchart TD
    W["write(formattedMsg)"]
    LOCK["QMutexLocker 加锁"]
    R{"rotateIfNeeded()"}
    D1{"按天切？<br/>today != m_currentDate"}
    D2{"按大小切？<br/>m_file.size() > m_maxFileSize"}
    WT["m_stream << formattedMsg"]
    FLUSH["m_stream.flush()"]
    UNLOCK["QMutexLocker 析构 → 解锁"]

    W --> LOCK
    LOCK --> R
    R --> D1
    D1 -->|"是"| CLOSE1["close() → setFileName(newDateFile)<br/>→ open(Append)"]
    D1 -->|"否"| D2
    CLOSE1 --> D2
    D2 -->|"是"| RENAME["close() → rename(日期_时分秒.log)<br/>→ setFileName(new) → open(Append)"]
    D2 -->|"否"| WT
    RENAME --> WT
    WT --> FLUSH
    FLUSH --> UNLOCK
```

## 5. 关闭流程 (stop)

```mermaid
sequenceDiagram
    participant C as 调用线程
    participant L as Logger
    participant W as 写线程
    participant S as ILogSink[]

    C->>L: stop()
    L->>L: m_running = false
    L->>W: wakeAll()<br/>（唤醒可能在 wait 的写线程）
    Note over L,W: 必须在 wait() 之前 wake<br/>否则写线程永远不被唤醒 → 死锁
    L->>W: m_thread->wait()<br/>（等待写线程退出循环）
    W->>W: 检查 m_running == false<br/>退出 while 循环
    W-->>L: 线程结束
    L->>S: 加 m_sinkMutex → 遍历所有 sink
    S->>S: FileSink::flush()
    S->>S: ConsoleSink::flush()
    L->>L: delete m_thread
```

## 6. flush 流程

```mermaid
sequenceDiagram
    participant C as 调用线程
    participant L as Logger
    participant Q as m_queue
    participant F as ILogFormatter
    participant S as ILogSink[]

    C->>L: flush()
    L->>L: m_async ?
    alt 异步模式
        loop 队列非空
            L->>Q: 加锁 → dequeue(entry) → 解锁
            L->>F: format(entry)
            L->>S: 写入所有 sink
        end
        Note over L: 直接在调用线程消费队列<br/>避免与写线程的唤醒竞态
    end
    L->>S: 加 m_sinkMutex → 遍历
    S->>S: sink->flush()
    Note over S: FileSink: stream flush + file flush
```

## 7. 关键设计决策

| 决策 | 原因 |
|------|------|
| 时间戳在 log() 中捕获 | 日志时间反映"事件发生时刻"而非"写入时刻" |
| LogStream 析构提交 | RAII 保证即使抛异常也能提交日志 |
| 写线程 dequeue 后立即解锁 | I/O 不持锁，业务线程可以继续入队 |
| stop() 先 wakeAll 再 wait | 防止写线程在 m_cond.wait() 中永久阻塞导致死锁 |
| flush() 直接消费队列 | 避免与写线程的 cond wait/wake 竞态 |
| FileSink 独立锁 | write() 和 flush() 可能并发（flush 从外部调用） |
| 格式化发生在写线程 | 减少业务线程开销，且便于将来按 sink 定制格式 |
