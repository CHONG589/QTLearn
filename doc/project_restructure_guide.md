# 项目结构重构指南

## 目录

1. [重构目标](#重构目标)
2. [最终结构](#最终结构)
3. [实现细节](#实现细节)
4. [关键设计原理](#关键设计原理)
5. [在 VS 中创建静态库项目](#在-vs-中创建静态库项目)

---

## 重构目标

将一个单体 Qt GUI 项目拆分为三个项目，实现**公共模块**与**业务代码**的分离：

- **QTLearnCommon**：公共静态库，包含 config / crypto / db / log / common
- **TreeExplorer**：业务层 GUI 应用，包含 Tree 模块（未来可扩展更多业务模块）
- **crypto_tool**：独立控制台工具，复用 QTLearnCommon 的加密模块

### 要解决的问题

| 问题 | 之前 | 之后 |
|------|------|------|
| 代码复用 | crypto_tool 重复实现了加密逻辑（~200 行） | crypto_tool 直接调用 Crypto 类，消除重复 |
| 模块边界 | 公共模块和业务代码混在一个项目里 | 物理隔离：公共库 .lib + 业务 .exe |
| 项目归属 | 业务代码散落在方案根目录 `src/` 下 | 收敛到 `TreeExplorer/` 目录，输出 exe 也在自身 `x64/` |
| Crypto 耦合 | 接口使用 `QString`，crypto_tool 无法复用 | 接口改为 `std::string`，与 Qt 解耦 |

---

## 最终结构

```
QT_Learn/                               ← 方案根目录
├── QT_Learn.sln                        ← 解决方案文件
├── config/                             ← 运行时配置（YAML + 密钥）
├── logs/                               ← 日志输出目录
├── include/                            ← 第三方头文件 (OpenSSL, yaml-cpp)
├── lib/                                ← 第三方库 (.lib + .dll)
│
├── QTLearnCommon/                      ← ① 公共静态库项目
│   ├── QTLearnCommon.vcxproj
│   ├── QTLearnCommon.vcxproj.filters
│   ├── common/AppPaths.h               ← 路径常量的唯一数据源
│   ├── config/config.h | config.cpp    ← 配置系统（依赖 yaml-cpp, QtCore）
│   ├── crypto/Crypto.h | Crypto.cpp    ← 加密模块（依赖 OpenSSL，接口 std::string）
│   ├── db/QDBConn.h | QDBConn.cpp      ← 数据库连接池（依赖 QtCore, QtSql）
│   └── log/Log.h | Log.cpp             ← 日志系统（依赖 QtCore, yaml-cpp）
│
├── TreeExplorer/                       ← ② 业务层 GUI 应用项目
│   ├── TreeExplorer.vcxproj
│   ├── TreeExplorer.vcxproj.filters
│   ├── main.cpp                        ← 入口：加载配置 → 解密凭证 → 启动 Tree 窗口
│   ├── tree/
│   │   ├── Tree.h | Tree.cpp           ← Tree 窗口 + TreeModel + TreeItem + DataManager
│   │   ├── Tree.ui                     ← Qt Designer 布局文件
│   │   └── Tree.qrc                    ← Qt 资源文件
│   └── x64/                            ← 构建输出（Debug/Release，编译后自动生成）
│
└── crypto_tool/                        ← ③ 独立控制台工具项目
    ├── crypto_tool.vcxproj
    ├── crypto_tool.vcxproj.filters
    ├── crypto_tool.cpp                 ← 仅负责控制台 UI（密码输入、参数解析）
    └── x64/                            ← 构建输出
```

### 编译依赖关系

```
          ┌──────────────┐
          │ QTLearnCommon │  静态库 .lib
          │  (Qt core;sql)│  依赖: Qt, yaml-cpp, OpenSSL
          └──────┬───────┘
        ┌────────┴────────┐
        ↓                  ↓
┌──────────────┐   ┌──────────────┐
│ TreeExplorer │   │ crypto_tool  │
│  GUI .exe    │   │ Console .exe │
│  Qt: core;gui│   │  无 Qt 依赖  │
│  widgets;sql │   │   仅需 OpenSSL│
└──────────────┘   └──────────────┘
```

---

## 实现细节

### 1. 创建 QTLearnCommon 静态库项目

见 [在 VS 中创建静态库项目](#在-vs-中创建静态库项目)。

关键配置项：

```xml
<!-- 项目类型：静态库 -->
<ConfigurationType>StaticLibrary</ConfigurationType>

<!-- Qt 模块：core 和 sql（不需要 gui/widgets） -->
<QtModules>core;sql</QtModules>

<!-- 第三方头文件路径 -->
<AdditionalIncludeDirectories>$(SolutionDir)include;...</AdditionalIncludeDirectories>
```

### 2. Crypto 模块去 Qt 化

**目标**：让 crypto_tool（纯控制台程序）能调用 Crypto 而不引入 Qt 依赖。

**原理**：静态库 .lib 是 .obj 文件的打包。链接时只有被引用的 .obj 会被拉入可执行文件。如果 `Crypto.obj` 内部不引用任何 Qt 符号，crypto_tool 链接它时就不会触发 Qt 依赖。

**改动对比**：

```cpp
// ===== 之前（依赖 Qt）=====
// Crypto.h
#include <QString>                          // 引入 Qt 依赖
#include "../log/log.h"                     // 间接引入更多 Qt 依赖
static bool loadKey(const QString &keyPath);
static QString encrypt(const QString &plaintext);
static QString decrypt(const QString &b64Cipher);

// Crypto.cpp
#include <QFile>                            // Qt 文件 I/O
#include <QTextStream>                      // Qt 文本流
static zch::Logger::ptr g_logger = LOG_NAME("default");  // 依赖日志模块 → Qt
// loadKey 内部：QFile, QTextStream, LOG_ERROR, LOG_INFO
// encrypt 内部：QByteArray plainBytes = plaintext.toUtf8()
// decrypt 内部：QByteArray b64Bytes = b64Cipher.toLatin1()
//            return QString::fromUtf8(...)

// ===== 之后（纯标准库）=====
// Crypto.h
#include <string>                           // 标准库，无 Qt
// 移除了 #include "../log/log.h"
static bool generateKey(const std::string &keyPath);  // 新增
static bool loadKey(const std::string &keyPath);
static std::string encrypt(const std::string &plaintext);
static std::string decrypt(const std::string &b64Cipher);

// Crypto.cpp
#include <fstream>                          // std::ifstream/ofstream 替代 QFile
#include <algorithm>                        // std::remove_if 替代 QString::trimmed()
// 移除了 g_logger 和所有 LOG_* 调用
// loadKey 内部：std::ifstream, std::getline
// encrypt 内部：直接使用 plaintext.data()（UTF-8 字节流）
// decrypt 内部：return std::string(reinterpret_cast<const char*>(...), plainLen)
```

**调用方适配**（QDBConn.cpp）：

```cpp
// 之前：Crypto 返回 QString，直接传给 QSqlDatabase
db.setUserName(Crypto::decrypt(g_db_user->getValue()));

// 之后：Crypto 返回 std::string，需要转换
db.setUserName(QString::fromStdString(
    Crypto::decrypt(g_db_user->getValue().toStdString())));
```

### 3. crypto_tool 代码精简

crypto_tool.cpp 从 ~310 行精简到 ~110 行。

**删除的重复代码**：
- `bin2hex()` / `hex2bin()` — 已在 Crypto.cpp 中
- `generateKey()` — 改用 `Crypto::generateKey()`
- `loadKey()` — 改用 `Crypto::loadKey()`
- `encrypt()` — 改用 `Crypto::encrypt()`

**保留的代码**：
- `readPassword()` — Windows 控制台密码输入（这是 UI 逻辑，不属于加密模块）
- `current_working_directory()` — 调试辅助
- `main()` — 参数解析和流程编排

### 4. include 路径策略

```
QTLearnCommon 内部：  相对路径  (如 config.cpp 中用 "../log/Log.h")
TreeExplorer 引用：   $(SolutionDir)QTLearnCommon 加入 include 路径
crypto_tool 引用：    $(SolutionDir)QTLearnCommon 加入 include 路径
第三方头文件：        $(SolutionDir)include (所有项目统一)
```

### 5. 项目引用关系

TreeExplorer 和 crypto_tool 通过 **ProjectReference** 而不是手动配置链接依赖来引用 QTLearnCommon：

```xml
<ItemGroup>
  <ProjectReference Include="..\QTLearnCommon\QTLearnCommon.vcxproj">
    <Project>{D4E3F2A1-B6C5-4987-8E9D-0F1A2B3C4D5E}</Project>
  </ProjectReference>
</ItemGroup>
```

ProjectReference 的优势：
- MSBuild 自动推导编译顺序（QTLearnCommon 先编译）
- 自动解析 `.lib` 输出路径，无需手动配置 `AdditionalDependencies`
- 代码修改后自动触发重新编译

### 6. 调试工作目录

```xml
<!-- 两个 .exe 项目都设置了工作目录为方案根目录 -->
<LocalDebuggerWorkingDirectory>$(SolutionDir)</LocalDebuggerWorkingDirectory>
```

因为 `AppPaths.h` 中所有路径都是相对方案根目录的（如 `config/db.key`），必须确保调试时工作目录为方案根目录。

---

## 关键设计原理

### 为什么用静态库而不是 DLL

| 考量 | 静态库 .lib | 动态库 .dll |
|------|-----------|-----------|
| 部署复杂度 | 无额外的 .dll 需要分发 | 需要额外的 .dll |
| 链接优化 | 链接器可以剔除未引用的 .obj | 整个 .dll 必须存在 |
| crypto_tool 隔离 | crypto_tool 只拉入 Crypto.obj，不触发 Qt | 整个 .dll 链接了 Qt，无法隔离 |
| 编译速度 | 修改一个 .cpp 只需重新链接 | 修改后需要重新编译 .dll |

对于本项目的规模，静态库既简化了部署，又允许 crypto_tool 做到零 Qt 依赖。

### crypto_tool 为什么不需要链接 Qt

静态库链接按需拉取的原理：

```
QTLearnCommon.lib 包含:
  ├── Crypto.obj      → 依赖: OpenSSL (C API), 标准库    ← crypto_tool 拉取这个
  ├── config.obj      → 依赖: QtCore, yaml-cpp            ← 不被 crypto_tool 引用，跳过
  ├── QDBConn.obj     → 依赖: QtCore, QtSql               ← 不被引用，跳过
  └── Log.obj         → 依赖: QtCore, yaml-cpp            ← 不被引用，跳过

crypto_tool.exe 链接结果: 仅包含 Crypto.obj → 无 Qt 符号 → 无需 Qt DLL
```

### de-Qt 化的边界

只改 `Crypto` 模块，因为：
- 它是唯一被 Qt 项目和非 Qt 项目同时使用的模块
- config / db / log 仅被 Qt 项目（TreeExplorer）使用，保留 Qt 类型更高效
- Crypto 本身是纯数据处理（字节流加密），不应该依赖 GUI 框架

---

## 在 VS 中创建静态库项目

> 以下演示如何在 Visual Studio 2022 中手动创建 QTLearnCommon 静态库项目。实际操作中已通过手写 `.vcxproj` 完成，此节供参考。

### 步骤 1：创建项目

1. 右键解决方案 → **添加 → 新建项目**
2. 搜索框输入 "静态库" 或 "Static Library"
3. 选择 **"静态库 (C++)"** 模板（不要选 Qt 模板）
4. 项目名称：`QTLearnCommon`
5. 位置：选择方案根目录 `E:\Code\QTCode\QTLearn\`

### 步骤 2：启用 Qt 支持

Qt VS Tools 不会自动为静态库模板生成 Qt 配置，需要手动添加：

1. 右键 QTLearnCommon 项目 → **Qt → Qt Project Settings**
2. 在 Qt Project Settings 对话框中：
   - 勾选所需的 **Qt Modules**：`core`、`sql`
   - Version 选择 `6.5.3_msvc2019_64`
3. 点击确定，Qt VS Tools 会自动修改 `.vcxproj`，添加：
   - `<Keyword>QtVS_v304</Keyword>`
   - `<QtInstall>6.5.3_msvc2019_64</QtInstall>`
   - `<QtModules>core;sql</QtModules>`
   - 以及 `Qt.props`、`Qt.targets` 的 import

### 步骤 3：配置项目属性

右键项目 → **属性**：

| 配置 | 属性 | 值 |
|------|------|-----|
| C/C++ → 常规 → 附加包含目录 | `$(SolutionDir)include` | OpenSSL 和 yaml-cpp 头文件路径 |
| C/C++ → 预处理器 → 预处理器定义 | 添加 `YAML_CPP_STATIC_DEFINE` | yaml-cpp 静态链接必需 |
| C/C++ → 命令行 → 其他选项 | `/utf-8 /wd4267` | UTF-8 源文件编码，忽略 size_t 截断警告 |

**注意**：静态库不需要配置链接器（Linker），因为没有链接步骤。但 **Librarian** 页可以留空——第三方库依赖由最终的可执行文件负责链接。

### 步骤 4：添加源文件

将现有源码文件添加到项目中：

1. 在项目中创建虚拟文件夹（Filter）：`common`、`config`、`crypto`、`db`、`log`
2. 添加现有项，将对应的 `.h` 和 `.cpp` 加入各文件夹

或在 VS 中直接用文件资源管理器把文件拖入 Solution Explorer。

### 步骤 5：设置项目依赖

在 TreeExplorer 和 crypto_tool 中：

1. 右键项目 → **添加 → 引用**
2. 勾选 `QTLearnCommon`
3. 确定——VS 会自动添加 ProjectReference

### 人工手写 vs IDE 创建

本项目采用的是**手写 .vcxproj** 方式，原因：

- 精确控制每个 XML 元素，避免 IDE 自动生成多余配置
- Qt VS Tools 创建的配置有时不完整（特别是静态库场景）
- 方便版本控制和 diff 审查

IDE 创建和手写产生的 `.vcxproj` 在功能上等价，选择哪种取决于个人偏好。

---

## 编译与运行

### 编译

在 VS 中打开 `QT_Learn.sln` → **生成 → 生成解决方案**（Ctrl+Shift+B）

MSBuild 自动按依赖顺序编译：
```
QTLearnCommon.lib → TreeExplorer.exe + crypto_tool.exe (后两者并行)
```

### 首次运行流程

```powershell
# 1. 生成密钥（在方案根目录执行）
.\crypto_tool\x64\Debug\crypto_tool.exe --genkey
# → 生成 config/db.key

# 2. 加密数据库凭证
.\crypto_tool\x64\Debug\crypto_tool.exe --encrypt
# 输入用户名和密码 → 输出 Base64 密文
# 将输出的 user: / pwd: 值填入 config/db_config.yml

# 3. 在 VS 中按 F5 启动 TreeExplorer
```

### VS 中设置启动项目

右键 TreeExplorer → **设为启动项目**，然后 F5 调试运行。
