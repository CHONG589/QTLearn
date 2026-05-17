# CLAUDE.md

## 项目概述

Qt 6.5.3 学习项目，Windows 平台，Visual Studio 2022 + Qt VS Tools 构建。实现了一个数据库驱动的树形控件演示程序，包含自建的配置系统、加密模块、数据库连接池和日志系统。

## 构建与环境

- **IDE**: Visual Studio 2022 (v143 工具链)，打开 `QT_Learn.sln` 即可编译
- **Qt 版本**: 6.5.3_msvc2019_64
- **Qt 模块**: core;gui;network;widgets;sql
- **数据库**: MySQL (通过 QMYSQL 驱动)
- **配置**: Debug|x64 和 Release|x64
- **项目文件**: `QT_Learn.vcxproj` (Qt VS Tools 格式)
- **依赖库**: yaml-cpp, OpenSSL

## 架构概览

```
main.cpp              → 入口：Config::loadFromConfDir → Crypto::loadKey → DBPool → Tree窗口
src/config/           → 配置系统：yaml-cpp 驱动，类型安全的 ConfigVar<T>，支持变更回调热更新
src/crypto/           → 加密模块：AES-256-GCM 认证加密，用于数据库凭证保护
src/log/              → 日志系统：多日志器 + 多输出器（文件/标准输出），格式模板驱动，YAML 配置
src/db/               → 数据库：DBPool（线程隔离连接池）+ DBConn + ScopedConn(RAII) + DBTransaction(RAII)
src/tree/             → UI 层：Tree 窗口 + TreeModel + TreeItem + DataManager
```

### 核心分层

1. **配置层** (`src/config/config.h|cpp`)：`Config` 全局管理器（全静态），通过 `Config::lookup<T>(name, default, desc)` 懒注册类型安全的配置项。`ConfigVar<T>` 持有 `QReadWriteLock` 保护的值和变更回调表。`LexicalCast` 模板体系打通 `std::string`（yaml-cpp 原生格式）与 STL/Qt 容器之间的双向转换。`loadFromConfDir()` 加载目录下 `*.yml`，递归展平 YAML 树为 `"a.b.c"` 点号 key 并匹配已注册的 `ConfigVarBase`。详见 `doc/config_guide.md`。

2. **加密层** (`src/crypto/Crypto.h|cpp`)：`Crypto` 纯静态类，基于 OpenSSL EVP API 实现 AES-256-GCM 认证加密。密钥从 `config/db.key`（64 位十六进制）加载。加密输出格式：`Base64(IV 12字节 + 密文 + GCM Tag 16字节)`。数据库配置中的 `user` 和 `pwd` 字段经加密后写入 YAML，运行时解密。

3. **UI 层** (`src/tree/Tree.h|cpp`)：`Tree` 是主窗口（包含 `QTreeView`），`TreeModel` 实现 `QAbstractItemModel`（懒加载 + 编辑），`TreeItem` 是内存树节点，`DataManager` 封装所有 DB 操作（单例）。`Node` 是数据库查询结果的 DTO。

4. **数据库层** (`src/db/QDBConn.h|cpp`)：自建连接池 `DBPool`（单例），基于 `QThreadStorage` 实现每线程独立连接池，避免 `QSqlDatabase` 跨线程使用问题。`DBConn` 封装 `QSqlDatabase` 操作，支持预处理、流式查询、事务。`ScopedConn` 通过 RAII 自动获取/归还连接。`DBTransaction` 通过 RAII 自动提交/回滚。所有数据库错误通过 `DBException` 抛出。

5. **日志层** (`src/log/Log.h|cpp`)：`LoggerManager` 单例管理所有 `Logger` 实例，每个 `Logger` 持有日志级别和一组 `LogAppender`。`LogFormatter` 解析格式模板字符串（`%d`、`%p`、`%f` 等占位符），将 `LogEvent` 格式化为文本。内置两种输出器：`StdoutLogAppender`（标准输出）和 `FileLogAppender`（文件输出，按天滚动 + 每 3 秒 reopen）。通过 `LogEventWrap` RAII 对象 + 宏实现流式 API。

### 关键设计决策

- `main.cpp` 使用 `/subsystem:console /entry:mainCRTStartup` 链接选项，使 GUI 程序同时启动控制台窗口用于调试输出。
- **配置优先于一切**：`Config::loadFromConfDir("./config")` 在 main 中最先执行，之后各模块通过文件级 `static ConfigVar<T>::ptr` 读取配置。模块在静态初始化阶段通过 `Config::lookup()` 声明所需配置项（在 `main()` 之前）。
- **数据库凭证加密存储**：YAML 中的 `user` 和 `pwd` 为 AES-256-GCM 加密后的 Base64 文本，运行时由 `Crypto::decrypt()` 解密后使用。密钥文件 `config/db.key` 不提交到 git。
- **数据库连接池自动初始化**：首次 `acquire()` 时若检测到未初始化则自动调用 `init()`，无需在 main 中显式调用 `DBPool::instance().init()`。
- 日志系统通过 `LogIniter` 静态对象注册配置变更回调，YAML 中 `logs` 配置变化时自动热更新 Logger 的 level 和 appenders。
- `Tree.ui` 定义主窗口布局（一个 `QFrame` 内含 `QTreeView`）。
- `Tree.qrc` 当前为空，预留用于图标等资源。

## 配置使用

```cpp
// main.cpp — 启动时加载配置目录下所有 .yml 文件
zch::Config::loadFromConfDir("./config");
Crypto::loadKey("config/db.key");

// 模块中声明配置项（文件级 static，在 main() 前注册）
static zch::ConfigVar<QString>::ptr g_db_ip =
    zch::Config::lookup("database.ip", QString("127.0.0.1"), "database ip address");

// 运行时读取
QString ip = g_db_ip->getValue();

// 注册变更回调（支持热更新）
g_db_ip->addListener([](const QString &oldVal, const QString &newVal) {
    // 响应配置变化...
});
```

### 配置文件结构

```
config/
├── db_config.yml    # 数据库连接配置（database.driver, .ip, .port, .user, .pwd, .db, .minsize, .maxsize, .timeout）
├── logs.yml         # 日志器配置（logs 数组，每个元素含 name/level/appenders）
└── db.key           # AES-256 密钥（64 位十六进制，不提交 git）
```

配置项名称规则：仅允许 `[0-9a-z_.]`，自动转小写。YAML 树展平为点号分隔的 key 后与已注册 ConfigVar 匹配。

## 日志使用

```cpp
// 获取日志器（通常声明为文件级 static）
static zch::Logger::ptr g_logger = LOG_NAME("default");

// 流式写日志（级别过滤在宏层面完成，低于当前级别的日志不构造 LogEvent）
LOG_DEBUG(g_logger) << "debug message";
LOG_INFO(g_logger)  << "info message";
LOG_WARN(g_logger)  << "warning message";
LOG_ERROR(g_logger) << "error message";

// 日志级别从低到高：DEBUG < INFO < NOTICE < WARN < ERROR < CRIT < ALERT < FATAL
// 日志器默认级别为 NOTSET（不输出），通过 YAML 配置或 setLevel() 设置
```

### 格式模板

默认格式：`[%d{yyyy-MM-dd HH:mm:ss}][%rms][%p][%c][%f:%l] %m%n`

| 占位符 | 含义 | 可选子格式 |
|--------|------|-----------|
| `%m` | 日志消息 | - |
| `%p` | 日志级别 | - |
| `%c` | 日志器名称 | - |
| `%d{...}` | 日期时间 | Qt 日期格式字符串 |
| `%r` | 累计运行毫秒数 | - |
| `%f` | 源文件名 | - |
| `%l` | 行号 | - |
| `%n` | 换行 | - |
| `%T` | 制表符 | - |
| `%%` | 百分号 | - |

## 代码风格

### 函数

- 每个函数的注释都必须按照如下格式添加注释；

```cpp
/**
 * @brief 获取/创建对应参数名的配置参数
 * @param[in] name 配置参数名称
 * @param[in] defaultValue 参数默认值
 * @param[in] description 参数描述
 * @details 获取参数名为name的配置参数,如果存在直接返回
 *          如果不存在,创建参数配置并用defaultValue赋值
 * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
 * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
 */
```

- 函数(包含类中的函数)命名：QT 风格的命名方式；

- 函数实现过程中，添加必要的注释解释原因；

### 类

- 类名：大驼峰命名；

- 类变量：`m_varName` 这样用小写 `m_` 加小驼峰命名的方式，并且添加变量作用的注释

### 普通变量命名

采用小驼峰命名；
