# 配置系统指南

## 概述

配置模块位于 `src/config/config.h` / `src/config/config.cpp`，命名空间 `zch`。基于 yaml-cpp 实现，提供线程安全的类型化配置参数管理，支持从 YAML 文件加载配置并自动匹配到代码中注册的配置项。

### 核心能力

| 能力 | 说明 |
|------|------|
| 类型安全 | 每个配置项是 `ConfigVar<T>`，编译期绑定类型 |
| 线程安全 | 所有读写操作由 `QReadWriteLock` 保护 |
| 变更回调 | 值变化时自动通知监听者（支持热更新） |
| 懒注册 | 模块在静态初始化阶段通过 `Config::lookup()` 声明配置项 |
| 文件 md5 去重 | 记录文件修改时间，跳过未变化的文件 |

---

## 架构分层

```
Config (管理器，全静态方法)
  ├── ConfigVarMap (QHash<QString, ConfigVarBase::ptr>) — 全局配置项注册表
  ├── lookup<T>()          — 注册 / 查找配置项
  ├── loadFromYaml()       — 从 YAML 节点树加载
  ├── loadFromConfDir()    — 从目录加载所有 .yml
  └── visit()              — 遍历所有配置项

ConfigVarBase (抽象基类)
  ├── m_name / m_description
  └── 纯虚: toString() / fromString() / getTypeName()

ConfigVar<T> (类型化配置项)
  ├── m_val (T 类型值)
  ├── m_cbs (QMap<quint64, onChangeCb>) — 变更回调表
  ├── getValue() / setValue()
  ├── addListener() / delListener()
  └── 序列化依赖 FromStr / ToStr 仿函数

LexicalCast<F, T> (类型转换层)
  └── std::string ↔ T 的双向转换，通过 yaml-cpp 中转序列化 STL/Qt 容器
```

---

## 核心类

### 1. Config — 配置管理器

全静态方法，持有两个文件级静态变量：

- `ConfigVarMap s_datas` — 全局配置项哈希表（key 为小写的点号分隔名称）
- `QReadWriteLock s_mutex` — 保护 `s_datas` 的读写锁

#### lookup<T>(name, default, description) — 注册并获取配置项

```cpp
// 如果 "database.ip" 已存在且类型匹配 → 返回已有项
// 如果不存在 → 创建新 ConfigVar<T> 并存入全局表
// 如果存在但类型不匹配 → 返回 nullptr
static ConfigVar<QString>::ptr g_db_ip =
    Config::lookup("database.ip", QString("127.0.0.1"), "database ip address");
```

**规则：**
- 名称仅允许 `[0-9a-z_.]`，自动转为小写
- 名称不合法时抛出 `std::invalid_argument`
- 通常声明为文件级 `static` 变量，在 `main()` 之前完成注册

#### loadFromConfDir(path, force) — 从目录加载配置

```cpp
Config::loadFromConfDir("./config");  // 加载 ./config 下所有 .yml 文件
```

加载流程：
1. 遍历目录下 `*.yml` 文件
2. 检查文件修改时间（`QFileInfo::lastModified()`），未变化则跳过
3. `YAML::LoadFile()` 解析 → 调用 `loadFromYaml()`
4. `force=true` 时跳过修改时间检查，强制重载

#### loadFromYaml(root) — 从 YAML 节点加载

内部流程：
1. `listAllMember()` 递归展平 YAML 树为 `"a.b.c"` 形式的 key 列表
2. 对每个 key，调用 `lookupBase()` 查找已注册的 `ConfigVarBase`
3. 找到 → 调用 `fromString()` 赋值；未找到 → 忽略（允许 YAML 中有暂未注册的字段）

---

### 2. ConfigVarBase — 配置项基类

```cpp
class ConfigVarBase {
    QString m_name;          // 配置项名称（小写）
    QString m_description;   // 描述文本
    virtual QString toString() = 0;        // 序列化为 YAML 字符串
    virtual bool fromString(const QString &val) = 0;  // 从字符串反序列化
    virtual QString getTypeName() const = 0;           // 获取类型名（调试用）
};
```

---

### 3. ConfigVar<T> — 类型化配置项

```cpp
template <class T,
          class FromStr = LexicalCast<std::string, T>,
          class ToStr   = LexicalCast<T, std::string>>
class ConfigVar : public ConfigVarBase { ... };
```

#### 关键方法

| 方法 | 说明 |
|------|------|
| `getValue()` | 读值（`QReadLocker`） |
| `setValue(v)` | 写值，值变化时触发所有回调，然后加写锁更新 |
| `addListener(cb)` | 注册变更回调 `void(const T& oldVal, const T& newVal)`，返回回调 ID |
| `delListener(id)` | 按 ID 移除回调 |
| `toString()` | `ToStr()(m_val)` 序列化 |
| `fromString(s)` | `setValue(FromStr()(s))` 反序列化 |

#### 线程安全

- 读操作：`QReadLocker`（多读者并发）
- 写操作：`QWriteLocker`（独占写入）
- 回调在**写锁获取前**执行（`QReadLocker` 持有期间），确保回调中能读到旧值

---

### 4. LexicalCast — 类型转换体系

连接 `std::string`（yaml-cpp 内部格式）与各类型之间的桥梁。

#### 基础特化

```
LexicalCast<std::string, QString>     // 通过 fromStdString / toStdString
LexicalCast<QString, std::string>
```

#### STL 容器特化（双向）

| 源 → 目标 | 序列化方式 |
|-----------|-----------|
| `string` → `vector<T>` / `list<T>` / `set<T>` / `unordered_set<T>` | YAML Sequence → 逐元素 LexicalCast |
| 容器 → `string` | 逐元素 LexicalCast → YAML Sequence |
| `string` → `map<string,T>` / `unordered_map<string,T>` | YAML Map → 逐 kv LexicalCast |
| 容器 → `string` | 逐 kv LexicalCast → YAML Map |

#### Qt 容器特化

```
QVector<T>  ↔ string    （同 std::vector）
QSet<T>     ↔ string    （同 std::set）
```

**注意：** 不支持 `QMap` / `QHash` / `QList`，如果需要，参照已有特化自行添加。

---

## 配置文件格式

### YAML 文件结构 → 配置项 key 映射

YAML 树被递归展平为点号分隔的 key：

```yaml
# config/db_config.yml
database:           → database
  driver: QMYSQL    → database.driver
  ip: 127.0.0.1     → database.ip
  port: 3306        → database.port
  user: <encrypted> → database.user
  pwd:  <encrypted> → database.pwd
  db: learn         → database.db
  minsize: 5        → database.minsize
  maxsize: 20       → database.maxsize
  timeout: 3000     → database.timeout
```

```yaml
# config/logs.yml
logs:                          → logs (YAML Sequence 整体序列化为字符串)
  - name: default              （由 LexicalCast<string, QSet<LogDefine>> 解析）
    level: DEBUG
    appenders:
      - type: StdoutLogAppender
        pattern: "[%d{yyyy-MM-dd HH:mm:ss}][%p][%f:%l] %m%n"
      - type: FileLogAppender
        file: ./logs
        pattern: "[%d{yyyy-MM-dd HH:mm:ss}][%p][%f:%l] %m%n"
```

对于标量节点，直接取 `Scalar()` 值；对于容器节点（Sequence/Map），整个节点被序列化为字符串后交给 `fromString()` 处理，由 `LexicalCast` 特化完成解析。

### 密钥文件

`config/db.key`：64 个十六进制字符的 AES-256 密钥，由 `Crypto::loadKey()` 加载，用于解密数据库连接凭证。

---

## 完整使用流程

### 1. 入口初始化（main.cpp）

```cpp
// 第一步：加载所有配置文件（必须在任何 Config::lookup 调用之后）
zch::Config::loadFromConfDir("./config");

// 第二步：加载加密密钥（数据库密码需要解密）
if (!Crypto::loadKey("config/db.key")) {
    LOG_ERROR(g_logger) << "加载密钥失败";
    return 1;
}
```

### 2. 模块注册配置项（静态初始化）

在各模块 `.cpp` 文件顶部声明文件级静态指针，利用 C++ 静态初始化在 `main()` 之前执行：

```cpp
// src/db/QDBConn.cpp
static zch::ConfigVar<QString>::ptr g_db_ip =
    zch::Config::lookup("database.ip", QString("127.0.0.1"), "database ip");

static zch::ConfigVar<int>::ptr g_db_port =
    zch::Config::lookup("database.port", (int)(3306), "database port");
// ... 其余配置项同理
```

### 3. 运行时读取配置

```cpp
QString ip   = g_db_ip->getValue();
int port     = g_db_port->getValue();
int minSize  = g_db_min_size->getValue();
```

### 4. 热更新（变更回调）

```cpp
// src/log/Log.cpp — 日志模块监听 logs 配置变更
static ConfigVar<QSet<LogDefine>>::ptr g_logDefines =
    Config::lookup("logs", QSet<LogDefine>(), "logs config");

struct LogIniter {
    LogIniter() {
        g_logDefines->addListener([](const QSet<LogDefine> &oldVal,
                                     const QSet<LogDefine> &newVal) {
            // 比较新旧值，增量更新 Logger 的 level 和 appender
            for (auto &i : newVal) {
                auto logger = LOG_NAME(i.name);
                logger->setLevel(i.level);
                logger->clearAppenders();
                for (auto &a : i.appenders) {
                    // 重建 appender...
                }
            }
        });
    }
};
static LogIniter __log_init;  // 静态对象，构造时注册回调
```

---

## 配置文件目录结构

```
项目根目录/
├── config/                  ← 实际使用的配置（不提交到 git）
│   ├── db_config.yml       数据库连接配置
│   ├── logs.yml            日志配置
│   └── db.key              AES-256 加密密钥（64 位十六进制）
├── db_config_example.yml   ← 配置模板（提交到 git，供参考）
└── src/config/
    ├── config.h            配置模块头文件
    └── config.cpp          配置模块实现
```

---

## 已有配置项清单

### database.* （QDBConn.cpp 注册）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `database.driver` | QString | `""` | 数据库驱动名（如 QMYSQL） |
| `database.ip` | QString | `127.0.0.1` | 数据库 IP |
| `database.port` | int | 3306 | 端口 |
| `database.user` | QString | `""` | 加密的用户名 |
| `database.pwd` | QString | `""` | 加密的密码 |
| `database.db` | QString | `""` | 数据库名 |
| `database.minsize` | int | 100 | 连接池最小连接数 |
| `database.maxsize` | int | 1024 | 连接池最大连接数 |
| `database.timeout` | int | 1000 | 获取连接超时（毫秒） |

### logs （Log.cpp 注册）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `logs` | QSet\<LogDefine\> | `{}` | 日志器配置集合 |

其中 `LogDefine` 结构：

```cpp
struct LogDefine {
    QString name;                        // 日志器名称
    LogLevel::Level level;               // 日志级别
    QVector<LogAppenderDefine> appenders; // 输出器列表
};

struct LogAppenderDefine {
    int type = 0;      // 1=File, 2=Stdout
    QString pattern;   // 格式模板
    QString file;      // type=1 时的文件路径
};
```

---

## 新增配置项的步骤

1. 在对应模块 `.cpp` 顶部声明静态指针：

```cpp
static zch::ConfigVar<int>::ptr g_my_timeout =
    zch::Config::lookup("my_module.timeout", (int)(5000), "my module timeout ms");
```

2. 在 YAML 文件中添加对应节点：

```yaml
my_module:
  timeout: 10000
```

3. 运行时通过 `g_my_timeout->getValue()` 读取。

4. 如需热更新，调用 `addListener()` 注册回调。

---

## 类型支持矩阵

`ConfigVar<T>` 中 `T` 可以是以下任何类型（前提是有对应的 `LexicalCast` 特化）：

| 类型 | 支持状态 |
|------|---------|
| `int`, `double`, `bool` 等基础类型 | 通过通用模板 `stringstream` 转换 |
| `QString` | 已特化 |
| `std::string` | 原生支持（yaml-cpp 内部类型） |
| `std::vector<T>` | 已特化 |
| `std::list<T>` | 已特化 |
| `std::set<T>` | 已特化 |
| `std::unordered_set<T>` | 已特化 |
| `std::map<string,T>` | 已特化 |
| `std::unordered_map<string,T>` | 已特化 |
| `QVector<T>` | 已特化 |
| `QSet<T>` | 已特化 |
| 自定义结构体 | 需特化 `LexicalCast<string, T>` 和 `LexicalCast<T, string>` |

### 自定义类型示例

```cpp
struct MyConfig {
    int a;
    QString b;
};

// 实现 string → MyConfig
template <>
class LexicalCast<std::string, MyConfig> {
public:
    MyConfig operator()(const std::string &v) {
        YAML::Node node = YAML::Load(v);
        MyConfig cfg;
        cfg.a = node["a"].as<int>();
        cfg.b = QString::fromStdString(node["b"].Scalar());
        return cfg;
    }
};

// 实现 MyConfig → string
template <>
class LexicalCast<MyConfig, std::string> {
public:
    std::string operator()(const MyConfig &v) {
        YAML::Node node(YAML::NodeType::Map);
        node["a"] = v.a;
        node["b"] = v.b.toStdString();
        return YAML::Dump(node);
    }
};
```

---

## 安全注意事项

- **数据库密码加密存储**：`user` 和 `pwd` 字段使用 AES-256-GCM 加密后 Base64 编码写入 YAML，运行时由 `Crypto::decrypt()` 解密后使用。不要在 YAML 中直接写明文密码。
- **密钥文件保护**：`config/db.key` 已加入 `.gitignore`，不应提交到版本控制。
- **配置名称校验**：仅允许 `[0-9a-z_.]`，防止注入攻击。
