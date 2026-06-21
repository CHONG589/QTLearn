# CLAUDE.md

## 项目概述

Qt 6.5.3 学习项目，Windows 平台，Visual Studio 2022 + Qt VS Tools 构建。实现了一个数据库驱动的树形控件演示程序，公共模块（配置系统、加密模块、数据库连接池、日志系统）位于独立静态库项目 QTLearnCommon 中。

## 构建与环境

- **IDE**: Visual Studio 2022 (v143 工具链)，打开 `QT_Learn.sln` 即可编译
- **解决方案项目**: QTLearnCommon (.lib) + TreeExplorer (.exe) + crypto_tool (.exe)
- **Qt 版本**: 6.5.3_msvc2019_64
- **Qt 模块**: QTLearnCommon 使用 core;sql，TreeExplorer 额外使用 gui;widgets
- **数据库**: MySQL (通过 QMYSQL 驱动)
- **配置**: Debug|x64 和 Release|x64
- **项目文件**: `QTLearnCommon.vcxproj` (静态库), `TreeExplorer.vcxproj` (GUI), `crypto_tool.vcxproj` (控制台)
- **依赖库**: yaml-cpp, OpenSSL

## 架构概览

```
QTLearnCommon/ (公共静态库)
├── common/AppPaths.h   → 路径与常量的唯一数据源
├── config/             → 配置系统：yaml-cpp 驱动，类型安全的 ConfigVar<T>，支持变更回调热更新
├── crypto/             → 加密模块：AES-256-GCM 认证加密，接口使用 std::string（不依赖 Qt）
├── db/                 → 数据库：DBPool + DBConn + ScopedConn(RAII) + DBTransaction(RAII)
└── log/                → 日志系统：多日志器 + 多输出器，格式模板驱动，YAML 配置

TreeExplorer/ (业务层 GUI)
├── main.cpp            → 入口：Config::loadFromConfDir → Crypto::loadKey → Tree窗口
└── tree/               → UI 层：Tree 窗口 + TreeModel + TreeItem + DataManager

crypto_tool/ (独立控制台工具)
└── crypto_tool.cpp     → 委托 QTLearnCommon 的 Crypto 类，自身仅负责控制台交互
```

### 核心分层

1. **配置层** (`QTLearnCommon/config/`)：`Config` 全局管理器（全静态），通过 `Config::lookup<T>(name, default, desc)` 懒注册类型安全的配置项。`ConfigVar<T>` 持有 `QReadWriteLock` 保护的值和变更回调表。`LexicalCast` 模板体系打通 `std::string`（yaml-cpp 原生格式）与 STL/Qt 容器之间的双向转换。`loadFromConfDir()` 加载目录下 `*.yml`，递归展平 YAML 树为 `"a.b.c"` 点号 key 并匹配已注册的 `ConfigVarBase`。详见 `doc/config_guide.md`。

2. **加密层** (`QTLearnCommon/crypto/`)：`Crypto` 纯静态类，基于 OpenSSL EVP API 实现 AES-256-GCM 认证加密。接口使用 `std::string`（与 Qt 解耦，crypto_tool 无需链接 Qt）。密钥从 `config/db.key`（64 位十六进制）加载。加密输出格式：`Base64(IV 12字节 + 密文 + GCM Tag 16字节)`。数据库配置中的 `user` 和 `pwd` 字段经加密后写入 YAML，运行时解密。

3. **UI 层** (`TreeExplorer/tree/`)：`Tree` 是主窗口（包含 `QTreeView`），`TreeModel` 实现 `QAbstractItemModel`（懒加载 + 编辑），`TreeItem` 是内存树节点，`DataManager` 封装所有 DB 操作（单例）。`Node` 是数据库查询结果的 DTO。

4. **数据库层** (`QTLearnCommon/db/`)：自建连接池 `DBPool`（单例），基于 `QThreadStorage` 实现每线程独立连接池，避免 `QSqlDatabase` 跨线程使用问题。`DBConn` 封装 `QSqlDatabase` 操作，支持预处理、流式查询、事务。`ScopedConn` 通过 RAII 自动获取/归还连接。`DBTransaction` 通过 RAII 自动提交/回滚。所有数据库错误通过 `DBException` 抛出。

5. **日志层** (`QTLearnCommon/log/`)：`LoggerManager` 单例管理所有 `Logger` 实例，每个 `Logger` 持有日志级别和一组 `LogAppender`。`LogFormatter` 解析格式模板字符串（`%d`、`%p`、`%f` 等占位符），将 `LogEvent` 格式化为文本。内置两种输出器：`StdoutLogAppender`（标准输出）和 `FileLogAppender`（文件输出，按天滚动 + 每 3 秒 reopen）。通过 `LogEventWrap` RAII 对象 + 宏实现流式 API。

### 关键设计决策

- `main.cpp` 使用 `/subsystem:console /entry:mainCRTStartup` 链接选项，使 GUI 程序同时启动控制台窗口用于调试输出。
- **配置优先于一切**：`Config::loadFromConfDir()` 在 main 中最先执行，之后各模块通过文件级 `static ConfigVar<T>::ptr` 读取配置。模块在静态初始化阶段通过 `Config::lookup()` 声明所需配置项（在 `main()` 之前）。
- **路径统一定义**：所有文件路径和加密常量定义在 `QTLearnCommon/common/AppPaths.h`，QT_Learn 和 crypto_tool 共用同一份，修改路径只需改一处。
- **数据库凭证加密存储**：YAML 中的 `user` 和 `pwd` 为 AES-256-GCM 加密后的 Base64 文本，运行时由 `Crypto::decrypt()` 解密后使用。密钥文件 `config/db.key` 不提交到 git。新成员通过 `crypto_tool.exe` 交互菜单生成密钥和加密凭证。
- **yaml-cpp 静态链接**：yaml-cpp 以 .lib 静态链接，无需 DLL。项目 vcxproj 中已定义 `YAML_CPP_STATIC_DEFINE` 宏。
- **数据库连接池自动初始化**：首次 `acquire()` 时若检测到未初始化则自动调用 `init()`，无需在 main 中显式调用 `DBPool::instance().init()`。
- 日志系统通过 `LogIniter` 静态对象注册配置变更回调，YAML 中 `logs` 配置变化时自动热更新 Logger 的 level 和 appenders。
- 日志器在头文件中使用 `LOG_*()` 宏时，用 `LOG_ROOT()` 代替文件级 `g_logger`，避免头文件中的静态变量定义冲突。
- `Crypto` 接口使用 `std::string`，与 Qt 解耦。QT_Learn 中调用时通过 `QString::fromStdString()` / `.toStdString()` 转换。crypto_tool 直接调用无需 Qt。
- `Tree.qrc` 中未添加图标资源文件，默认使用 `:/icons/folder.png` 和 `:/icons/file.png` 路径。

## 配置使用

```cpp
// main.cpp — 启动时加载配置（路径统一定义在 QTLearnCommon/common/AppPaths.h）
zch::Config::loadFromConfDir(AppPaths::CONFIG_DIR);
Crypto::loadKey(AppPaths::KEY_FILE);

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
├── db_config.yml                       # 数据库连接配置（不入库，从模板复制）
├── db_config_example.yml.template      # 配置模板（入仓库，占位符）
├── logs.yml                            # 日志器配置
└── db.key                              # AES-256 密钥（64 位十六进制，不入库）
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

### 项目结构

```
├─.claude
│  ├─agents
│  ├─commands
│  └─rules
├─config                   # 运行时配置（YAML + 密钥文件）
├─crypto_tool              # 独立控制台工具（复用 QTLearnCommon 的 Crypto 类）
├─doc
├─include                  # 第三方头文件
│  ├─openssl
│  └─yaml-cpp
├─lib                      # 第三方库（yaml-cpp, OpenSSL）
│  ├─Debug
│  └─Release
├─logs                     # 运行日志输出目录
├─QTLearnCommon            # 公共静态库
│  ├─common                # AppPaths.h
│  ├─config                # 配置系统
│  ├─crypto                # 加密模块（std::string 接口）
│  ├─db                    # 数据库连接池
│  └─log                   # 日志系统
├─TreeExplorer             # 业务层 GUI 应用
│  ├─main.cpp
│  ├─tree                  # Tree 窗口 + TreeModel + DataManager
│  └─x64                   # 构建输出（exe 等）
```
