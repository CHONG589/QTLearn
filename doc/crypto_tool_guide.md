# 加密工具使用指南

本文档介绍 `crypto_tool` 项目的配置、使用流程，以及加密模块在 QT_Learn 主程序中的工作方式。关于算法选型和代码实现细节，请参考 [openssl_crypto_design.md](openssl_crypto_design.md)。

## 1. 背景

数据库连接配置（`config/db_config.yml`）中的用户名和密码如果以明文存储，会被 git 提交到仓库，存在凭证泄露风险。加密方案的核心思路是：

- **密钥不入库**：每个开发者自己生成 `db.key`，不提交到 git
- **密文存 YAML**：YAML 中只存 Base64 密文，不知道密钥的人无法还原
- **运行时自动解密**：程序启动后加载密钥，创建数据库连接时自动解密

## 2. 文件结构

```
QTLearn/
├── crypto_tool/                    ← 独立加密工具项目（VS 控制台项目）
│   ├── crypto_tool.cpp             ← 工具源码（纯 C++，不依赖 Qt）
│   ├── crypto_tool.vcxproj         ← VS 2022 项目文件
│   ├── crypto_tool.vcxproj.filters
│   └── x64/
│       ├── Debug/                  ← 编译产物（exe + dll）
│       └── Release/                ← 编译产物（exe + dll）
│
├── src/crypto/                     ← 主程序用的加密模块（依赖 Qt）
│   ├── Crypto.h                    ← 加解密类声明
│   └── Crypto.cpp                  ← 加解密类实现（AES-256-GCM）
│
├── config/
│   ├── db.key                      ← AES-256 密钥（不入库，gitignore）
│   ├── db_config.yml               ← 真实数据库配置（不入库，gitignore）
│   └── db_config_example.yml       ← 配置模板（入仓库）
│
├── include/openssl/                ← OpenSSL 头文件
└── lib/                            ← OpenSSL 库文件
    ├── Debug/
    │   ├── libcrypto-3-x64.dll
    │   └── libcrypto.lib
    └── Release/
        ├── libcrypto-3-x64.dll
        └── libcrypto.lib
```

**关键认识**：`crypto_tool` 和主程序（`src/crypto/Crypto.cpp`）是两个独立可执行程序的加密模块，各自实现了一套 AES-256-GCM 加密逻辑——`crypto_tool` 不依赖 Qt（纯 C++），主程序模块依赖 Qt（用 `QFile`、`QString` 等）。虽然代码独立，但加密格式完全一致，可以互解密文。

## 3. crypto_tool 项目配置（VS2022 界面操作）

### 3.1 配置独立输出路径

**背景**：`crypto_tool` 和 `QT_Learn` 同在一个解决方案下。默认情况下，MSBuild 中未显式设置 `OutDir` 的项目会继承解决方案级别的输出路径 `$(SolutionDir)$(Platform)\$(Configuration)\`，导致两个项目的编译产物都输出到 `QT_Learn\x64\Debug\` 和 `QT_Learn\x64\Release\` 下，混在一起。

**目标**：让 `crypto_tool` 的输出路径脱离解决方案默认值，改为落在自身项目目录 `crypto_tool\x64\Debug\` 和 `crypto_tool\x64\Release\` 下。

**操作步骤（Debug|x64 和 Release|x64 各做一遍）**：

1. 在 **解决方案资源管理器** 中，右键点击 `crypto_tool` 项目 → **属性**
2. 顶部 **配置** 下拉框选 **Debug**，**平台** 下拉框选 **x64**
3. 左侧导航树选 **配置属性 → 常规**
4. 右侧找到 **输出目录**，当前值为：

   ```
   $(SolutionDir)$(Platform)\$(Configuration)\
   ```

   将其改为：

   ```
   $(ProjectDir)$(Platform)\$(Configuration)\
   ```

   `$(SolutionDir)` 指向 `QTLearn\`，`$(ProjectDir)` 指向 `QTLearn\crypto_tool\`。只改这一个宏，输出目录就从解决方案根目录变为项目自身目录。

5. 同一个页面找到 **中间目录**，改为：

   ```
   $(ProjectDir)$(Platform)\$(Configuration)\obj\
   ```

   这样 `.obj`、`.tlog` 等编译中间文件也隔离到项目目录下。

6. 顶部 **配置** 下拉框切换到 **Release**，重复第 4、5 步
7. 点击 **确定** 保存

**效果验证**：重新编译后产物位置变为：

```
QTLearn\
└── crypto_tool\
    └── x64\
        ├── Debug\
        │   ├── crypto_tool.exe
        │   ├── libcrypto-3-x64.dll      ← 生成后事件自动复制
        │   └── obj\                     ← 中间文件
        └── Release\
            ├── crypto_tool.exe
            ├── libcrypto-3-x64.dll
            └── obj\
```

> **注意**：Win32 配置可以不管。该项目 Win32 配置未配置 OpenSSL 头文件和库依赖，实际上不可编译。

**相关宏速查**：

| 宏 | 展开值 | 说明 |
|----|--------|------|
| `$(SolutionDir)` | `E:\Code\QTCode\QTLearn\` | 解决方案目录 |
| `$(ProjectDir)` | `E:\Code\QTCode\QTLearn\crypto_tool\` | 项目目录 |
| `$(Platform)` | `x64` | 平台 |
| `$(Configuration)` | `Debug` 或 `Release` | 配置 |
| `$(OutDir)` | 等于 **输出目录** 文本框的值 | 最终输出路径 |

### 3.2 配置调试参数（F5 直运行）

**背景**：`crypto_tool` 需要命令行参数才会执行实际操作——`--genkey` 生成密钥，`--encrypt` 加密凭证。不传参数时只打印用法提示就退出。如果希望直接在 VS 中按 F5（或 Ctrl+F5）就能进入加密交互流程，需要配置调试参数。

**推荐配置**：将命令参数设为 `--encrypt`。原因是 `--genkey` 是真正的终身一次性操作，而 `--encrypt` 的重复使用场景更多（换密码、换数据库、新成员配置等）。

**操作步骤（Debug|x64 和 Release|x64 各做一遍）**：

1. 右键 `crypto_tool` 项目 → **属性**
2. 左侧导航树选 **配置属性 → 调试**
3. **命令参数** 填入：

   ```
   --encrypt
   ```

4. **工作目录** 填入：

   ```
   $(ProjectDir)
   ```

   这确保程序运行时的当前目录是 `crypto_tool\`，代码中的 `"../config/db.key"` 路径才能正确解析到 `QTLearn\config\db.key`。

5. 切换 **Release** 配置，重复第 3、4 步
6. 点击 **确定** 保存

**使用效果**：

1. 在 VS 解决方案资源管理器中右键 `crypto_tool` → **设为启动项目**
2. 按 **Ctrl+F5**（开始执行，不调试）或 **F5**（开始调试）
3. 控制台弹出，进入交互式加密流程：

   ```
   当前所在目录为：E:\Code\QTCode\QTLearn\crypto_tool
   请输入用户名: ******
   请输入密码:   ******

   --- 复制下面的密文到 config/db_config.yml ---

   user: LhoGOD+AtE9RYKkd4E5lb1jkWMs7rOSs50LUvIzKyy8=
   pwd:  PwqaOWCnorcbrqxxV9CLx1Ihqbz19OQ574AX2S+vuezpvQ==
   ```

> **偶尔需要 `--genkey` 时**：打开命令行，cd 到 `crypto_tool\x64\Release\`，执行 `crypto_tool.exe --genkey` 即可。不必为此修改 VS 配置。

### 3.3 编译依赖

**头文件路径**（配置属性 → C/C++ → 常规 → 附加包含目录）：
```
$(SolutionDir)include
```
指向 `QTLearn/include/`，用于找到 `<openssl/evp.h>` 等头文件。

**库路径**（配置属性 → 链接器 → 常规 → 附加库目录）：
```
$(SolutionDir)lib\$(Configuration)
```
Debug 配置指向 `lib/Debug/`，Release 配置指向 `lib/Release/`。

**链接库**（配置属性 → 链接器 → 输入 → 附加依赖项）：
```
libcrypto.lib
```
只链接 libcrypto，不需要 libssl（加密工具只用 EVP 对称加密）。

### 3.4 编译选项

x64 配置额外指定了（配置属性 → C/C++ → 命令行 → 其他选项）：
```
/utf-8 /wd4267
```
- `/utf-8`：源文件和执行字符集均为 UTF-8，确保中文输出正常
- `/wd4267`：忽略 64 位 `size_t` 转 `int` 的截断警告（EVP API 大量使用 `int` 长度参数）

### 3.5 生成后事件（自动复制 DLL）

（配置属性 → 生成事件 → 生成后事件）
```bat
copy "$(SolutionDir)lib\$(Configuration)\libcrypto-3-x64.dll" "$(OutDir)"
```
编译后自动将 OpenSSL DLL 复制到输出目录。`$(OutDir)` 会跟随 3.1 节的输出路径配置自动适配，**这行命令不需要改动**。

## 4. 新成员初始化流程（首次配置）

以下步骤在 **VS2022 中打开 `QT_Learn.sln`** 后操作。

### 步骤 1：从模板复制配置文件

在命令行中：
```bash
copy config\db_config_example.yml config\db_config.yml
```

`db_config_example.yml` 内容：
```yaml
database:
  driver: QMYSQL
  ip: 127.0.0.1
  port: 3306
  user: <运行 crypto_tool --encrypt 生成加密用户名后替换此行>
  pwd:  <运行 crypto_tool --encrypt 生成加密密码后替换此行>
  db: <数据库名字>
  minsize: 5
  maxsize: 20
  timeout: 3000
```

将 `db` 字段改为实际的数据库名，其他字段（ip、port 等）根据本地 MySQL 配置修改。

### 步骤 2：编译 crypto_tool

在 VS 顶部工具栏选择配置 **Release**、平台 **x64**，右键 `crypto_tool` 项目 → **生成**。

### 步骤 3：生成密钥

打开命令行，cd 到 `crypto_tool\x64\Release\`，执行：
```bash
crypto_tool.exe --genkey
```
输出：`密钥已生成: ../config/db.key`

这会创建一个包含 64 个十六进制字符的文件 `config/db.key`（代表 32 字节 = 256 位的 AES 密钥）。

> `KEY_FILE` 常量定义为 `"../config/db.key"`，相对于 `crypto_tool.exe` 所在目录（`crypto_tool\x64\Release\`），上一级正好是项目根目录，所以密钥文件落在 `config/db.key`。

### 步骤 4：加密数据库凭证

配置好 [3.2 节](#32-配置调试参数f5-直运行) 的调试参数后，直接在 VS 中右键 `crypto_tool` → **设为启动项目**，按 **Ctrl+F5** 运行。

控制台弹出后：
1. 提示输入用户名（输入时屏幕显示 `*`，不回显实际字符）
2. 提示输入密码（同样不回显）
3. 输出 Base64 密文

### 步骤 5：将密文填入配置文件

将输出的 `user:` 和 `pwd:` 后面的密文字符串复制到 `config/db_config.yml` 的对应字段：

```yaml
database:
  driver: QMYSQL
  ip: 127.0.0.1
  port: 3306
  user: LhoGOD+AtE9RYKkd4E5lb1jkWMs7rOSs50LUvIzKyy8=
  pwd:  PwqaOWCnorcbrqxxV9CLx1Ihqbz19OQ574AX2S+vuezpvQ==
  db: learn
  minsize: 5
  maxsize: 20
  timeout: 3000
```

### 步骤 6：验证

将启动项目切换回 `QT_Learn`，按 F5 运行。如果数据库连接成功，说明加解密链路正常。如果密钥文件缺失或密文格式不对，日志中会输出相应的错误信息。

## 5. 为什么有两处加载 config/db.key

阅读代码时会发现两个地方都加载了 `config/db.key`：

| 位置 | 所属程序 | 方向 | 目的 |
|------|---------|------|------|
| `crypto_tool/crypto_tool.cpp` 的 `loadKey()` | `crypto_tool.exe` | **加密** | 加载密钥 → 加密用户输入的明文凭证 → 输出 Base64 密文 |
| `src/main.cpp` 中的 `Crypto::loadKey()` | `QT_Learn.exe` | **解密** | 加载密钥 → 解密 YAML 中的密文 → 还原数据库用户名/密码明文 |

它们是 **两个完全独立的可执行程序**：

```
crypto_tool.exe（加密工具）              QT_Learn.exe（主程序）
───────────────────────────         ─────────────────────────
配置阶段（一次性）                       运行阶段（每次启动）
       │                                      │
  loadKey() 加载 db.key                Crypto::loadKey() 加载 db.key
       │                                      │
       ▼                                      ▼
  encrypt(用户名) → Base64 密文         decrypt(密文) → 数据库用户名
  encrypt(密码)   → Base64 密文         decrypt(密文) → 数据库密码
       │                                      │
       ▼                                      ▼
  用户手动贴到 db_config.yml           传给 QSqlDatabase 连接 MySQL
```

- `crypto_tool.exe` 不依赖 Qt，用纯 C++ 自己实现了 AES-256-GCM 加密逻辑
- `QT_Learn.exe` 通过 `src/crypto/Crypto.cpp`（Qt 封装版）使用相同的加密算法和密钥格式
- 两者的加密格式一致、密钥格式一致——`crypto_tool` 用 `db.key` 加密的密文，`QT_Learn` 用同一个 `db.key` 能解回来

## 6. 运行时解密流程（主程序侧）

程序正常启动时，加解密链路如下：

```
main.cpp
  │
  ├─ Logger::instance().init()           // 初始化日志
  │
  ├─ zch::Config::loadFromConfDir("./config")
  │     └─ 扫描 config/*.yml
  │     └─ 加载 db_config.yml
  │     └─ 注册配置变量（其中 user/pwd 的值是 Base64 密文字符串）
  │
  ├─ Crypto::loadKey("config/db.key")
  │     └─ 打开 config/db.key
  │     └─ 读取 64 个 hex 字符
  │     └─ 转为 32 字节二进制密钥，存入 s_key
  │     └─ 设置 s_loaded = true
  │     └─ 失败 → LOG_ERROR 并返回 false
  │
  └─ Tree window; window.show();
        └─ 首次 DB 操作触发 DBPool::createConnection()
              │
              ├─ g_db_user->getValue() → "LhoGOD+AtE9R..."（密文）
              ├─ Crypto::decrypt(g_db_user->getValue())
              │     ├─ Base64 解码
              │     ├─ 提取 IV(前12字节) + 密文 + Tag(后16字节)
              │     ├─ EVP_DecryptInit_ex 初始化 AES-256-GCM
              │     ├─ EVP_DecryptUpdate 解密
              │     ├─ EVP_CIPHER_CTX_ctrl 设置 GCM Tag
              │     └─ EVP_DecryptFinal_ex 验证 Tag → 返回明文
              │
              ├─ g_db_pwd->getValue()  → 密文 → Crypto::decrypt() → 明文
              │
              ├─ db.setUserName(明文用户名)
              ├─ db.setPassword(明文密码)
              └─ db.open()  → 实际连接 MySQL
```

### 关键时序

- **密钥加载**在 `main()` 中完成，先于任何数据库操作
- **配置加载**也先于密钥加载，但配置中的 `user`/`pwd` 此时只是字符串，未被解密
- **实际解密**发生在 `DBPool::createConnection()` 中，即首次需要连接数据库时。此时密钥已加载完毕，解密是同步完成的
- 解密后的明文仅在 `QSqlDatabase::open()` 期间存在于内存中，不会写回 YAML 或日志

## 7. 配置注意要点

### 7.1 路径问题

- `crypto_tool.cpp` 中 `KEY_FILE` 定义为 `"../config/db.key"`，这是相对于 **程序运行时的当前工作目录（CWD）**。VS 调试时工作目录默认是 `$(ProjectDir)`（即 `crypto_tool\`），路径 `../config/db.key` 正确指向项目根目录的 `config/db.key`
- 如果从命令行运行，注意 CWD 必须正确。程序启动时第一行会打印当前所在目录，可用于排查
- 主程序中 `Crypto::loadKey("config/db.key")` 的路径也是相对于程序工作目录。VS 调试 QT_Learn 时默认工作目录是项目根目录，路径直接可用

### 7.2 不要提交到 git 的文件

`.gitignore` 中已配置：
```gitignore
config/db.key
config/db_config.yml
```

确保这两个文件永远不会被 `git add` 提交。`db_config_example.yml` 是模板文件，应该正常提交。

### 7.3 密钥安全

- `db.key` 是 32 字节完全随机的密钥，不存在默认值或弱密钥
- 每个开发人员独立生成自己的密钥，互不共享——每个人连的是自己本机的 MySQL，本来就不该共享凭证
- 如果 `db.key` 泄露，攻击者可以解密 YAML 中的密文，应立即重新生成密钥并重新加密所有凭证
- 密钥文件本身不加密——依赖文件系统权限保护。在生产环境中可考虑使用 Windows DPAPI 或硬件安全模块进一步保护

### 7.4 CRT 链接匹配

OpenSSL 的 `.lib` 文件必须与项目的 CRT 链接方式匹配：

| 配置 | CRT 方式 | 使用的 .lib 目录 |
|------|----------|-----------------|
| Debug \| x64 | `/MDd` | `lib/Debug/libcrypto.lib` |
| Release \| x64 | `/MD` | `lib/Release/libcrypto.lib` |

如果用了不匹配的 `.lib`，链接时会报 `LNK2038`（`_ITERATOR_DEBUG_LEVEL` 不匹配）等错误。

### 7.5 crypto_tool 不依赖 Qt

`crypto_tool.exe` 是纯 C++ 程序，不链接任何 Qt 库。这意味着：
- 可以在没有安装 Qt 的机器上运行
- 可用于 CI/CD 流程中加密凭证
- 也可以作为独立工具分发给其他团队成员

### 7.6 密文每次都不同

AES-256-GCM 每次加密生成随机 12 字节 IV，所以同一个明文加密两次得到的密文不同。这是正常行为，不是 bug。不要因为两次运行 `--encrypt` 输出不同而担心。

### 7.7 旧版工具路径

`tools/crypto_tool.cpp` 已被删除，其代码已迁移到 `crypto_tool/` 目录作为独立 VS 项目。迁移前后的主要差异是 `KEY_FILE` 路径从 `"config/db.key"` 改为 `"../config/db.key"`（适配新 exe 所在子目录），并增加了 CWD 调试输出和 UTF-8 控制台设置。

### 7.8 故障排查

| 现象 | 可能原因 | 解决 |
|------|---------|------|
| 程序启动时 "密钥加载失败" | `config/db.key` 不存在 | 运行 `crypto_tool --genkey` |
| 程序启动时 "密钥加载失败" | 密钥文件格式错误 | 检查 `db.key` 内容是否为 64 位 hex 字符，无多余空格或换行 |
| 数据库连接失败 | 密文与密钥不匹配（用另一把密钥加密的） | 重新生成密钥，重新加密凭证 |
| 数据库连接失败 | 用户名/密码本身错误 | 先用明文确认数据库可连接，再加密 |
| `crypto_tool --encrypt` 报 "未找到密钥文件" | 运行目录不对 | 检查 CWD 输出，确认从 `crypto_tool\x64\Release\` 运行 |
| VS 中 F5 运行 crypto_tool 只闪一下就退出 | 未配置命令参数 | 参照 3.2 节，设命令参数为 `--encrypt` |
| VS 中 F5 运行 crypto_tool 提示找不到密钥文件 | 工作目录设置不对 | 参照 3.2 节，设工作目录为 `$(ProjectDir)` |
| 链接报 LNK2038 | libcrypto.lib CRT 版本不匹配 | 确认 Debug/Release 各自使用的是对应目录的 .lib |
| 运行时找不到 `libcrypto-3-x64.dll` | DLL 未在 exe 目录或 PATH 中 | VS 编译后会自动复制（生成后事件），手动编译需自己复制 |

## 8. 加密算法摘要

| 决策点 | 选择 | 原因 |
|--------|------|------|
| 算法 | AES-256-GCM | 认证加密，同时提供机密性和完整性校验 |
| 密钥长度 | 256 位（32 字节） | 安全性足够，性能可接受 |
| IV 长度 | 12 字节 | GCM 推荐值 |
| Tag 长度 | 16 字节 | GCM 最大认证标签，防篡改 |
| API 层级 | OpenSSL EVP | 高层 API，自动处理内存和边界问题 |
| 输出编码 | Base64 | 密文含二进制字节，Base64 编码后才能存入 YAML |

### 密文结构

```
┌──────────────┬──────────────────┬──────────────┐
│  IV (12字节)  │   密文 (变长)     │  Tag (16字节) │
└──────────────┴──────────────────┴──────────────┘
                     ↓ Base64 编码
      YAML 中存储的字符串，如 "LhoGOD+AtE9RYKkd4E5lb1jkWMs7rOSs50LUvIzKyy8="
```

## 9. 相关文档

- [openssl_crypto_design.md](openssl_crypto_design.md) — 算法设计细节、OpenSSL 库编译、完整源码
- [QLog_flow.md](QLog_flow.md) — 日志系统使用说明
