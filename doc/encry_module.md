# 加密模块说明

## 1. 概述

数据库连接配置中的用户名和密码以 Base64 密文存储在 `config/db_config.yml` 中。密钥文件 `config/db.key` 由每个开发者独立生成、不提交 git。程序启动时加载密钥，运行时自动解密凭证。

```
配置阶段（一次性）                      运行阶段（每次启动）
──────────────────                    ──────────────────
crypto_tool.exe 生成 db.key           QT_Learn.exe 加载 db.key
crypto_tool.exe 加密凭证 → 密文         读取 YAML 中的密文 → 自动解密 → MySQL 连接
```

## 2. 文件结构

```
QTLearn/
├── src/
│   ├── common/AppPaths.h          ← 加密常量 + 路径的唯一数据源
│   └── crypto/
│       ├── Crypto.h               ← 加解密类（Qt 封装）
│       └── Crypto.cpp
├── crypto_tool/
│   ├── crypto_tool.cpp            ← 独立加密工具（纯 C++，不依赖 Qt）
│   └── x64/Debug/crypto_tool.exe
├── config/
│   ├── db.key                     ← AES-256 密钥（gitignore）
│   ├── db_config.yml              ← 真实配置含密文（gitignore）
│   └── db_config_example.yml.template  ← 配置模板（入仓库）
├── include/openssl/               ← OpenSSL 头文件
└── lib/                           ← OpenSSL 库文件
```

**两个加密实现**：`crypto_tool`（纯 C++）和 `src/crypto/Crypto`（Qt 封装）各自独立实现了 AES-256-GCM，加密格式完全一致、可互解。

## 3. 算法与格式

| 决策点 | 选择 | 原因 |
|--------|------|------|
| 算法 | AES-256-GCM | 认证加密，同时提供机密性和完整性校验 |
| 密钥长度 | 256 位（32 字节） | 安全性足够 |
| IV 长度 | 12 字节 | GCM 推荐值，每次加密随机生成 |
| Tag 长度 | 16 字节 | GCM 最大标签，防篡改 |
| 输出编码 | Base64 | 二进制密文编码后可存入 YAML |

**密文结构**（Base64 编码前）：

```
┌──────────────┬──────────────────┬──────────────┐
│  IV (12字节)  │   密文 (变长)     │  Tag (16字节) │
└──────────────┴──────────────────┴──────────────┘
```

> 每次加密生成随机 IV，同一明文两次加密输出不同密文，这是正常行为。

## 4. 新成员首次配置

### 步骤 1：编译 crypto_tool

在 VS 中打开 `QT_Learn.sln`，配置 **Release | x64**，右键 `crypto_tool` 项目 → **生成**。

### 步骤 2：生成密钥

在项目根目录下运行：

```bash
crypto_tool\x64\Debug\crypto_tool.exe --genkey
```

将创建 `config/db.key`（64 位十六进制字符）。

### 步骤 3：加密凭证

同样在项目根目录下运行：

```bash
crypto_tool\x64\Debug\crypto_tool.exe --encrypt
```

按提示输入数据库用户名和密码。输出格式：

```
user: AfDYiadBKis/EdYF13SwWcz8+keudTg6UHTc2evz0A==
pwd:  unMevyQIsFL5HqniAToqVXx2F/prza1A4Snbhq67LYLDzw==
```

### 步骤 4：填入配置文件

参考 `config/db_config_example.yml.template` 的格式创建 `config/db_config.yml`，将第 3 步输出的 `user:` 和 `pwd:` 密文替换对应字段：

```yaml
database:
  driver: QMYSQL
  ip: 127.0.0.1
  port: 3306
  user: AfDYiadBKis/EdYF13SwWcz8+keudTg6UHTc2evz0A==
  pwd:  unMevyQIsFL5HqniAToqVXx2F/prza1A4Snbhq67LYLDzw==
  db: learn
  minsize: 5
  maxsize: 20
  timeout: 3000
```

### 步骤 5：验证

将 VS 启动项目切回 `QT_Learn`，F5 运行。数据库连接正常即表示加解密链路正确。

## 5. 运行时解密流程

```
main()
  ├─ Config::loadFromConfDir(AppPaths::CONFIG_DIR)
  │     └─ 加载 config/*.yml，密文作为普通字符串存入 ConfigVar
  ├─ Crypto::loadKey(AppPaths::KEY_FILE)
  │     └─ 读取 config/db.key → 转为 32 字节密钥
  └─ DBPool 首次连接时
        ├─ Crypto::decrypt(user 密文) → 明文用户名
        ├─ Crypto::decrypt(pwd 密文)  → 明文密码
        └─ db.open()
```

- 密钥在 `main()` 中加载，先于任何数据库操作
- 配置中的 `user`/`pwd` 加载时只是字符串，不被解密
- 实际解密发生在 `DBPool::createConnection()` 中
- 解密后的明文仅存在于内存中，不会写回 YAML 或日志

> 所有路径定义在 `src/common/AppPaths.h` 中，从项目根目录运行即可。

## 7. 设计说明

### 为什么有两个 loadKey()

| 位置 | 程序 | 方向 | 目的 |
|------|------|------|------|
| `crypto_tool.cpp` 的 `loadKey()` | `crypto_tool.exe` | 加密 | 加载密钥 → 加密明文 → 输出密文 |
| `src/main.cpp` 中 `Crypto::loadKey()` | `QT_Learn.exe` | 解密 | 加载密钥 → 解密密文 → 还原凭证 |

两者是独立程序，使用相同密钥格式和加密格式，共用 `src/common/AppPaths.h` 中的常量和路径定义。

### crypto_tool 不依赖 Qt

`crypto_tool.exe` 是纯 C++ 控制台程序，不链接 Qt 库。可在未安装 Qt 的机器上运行，也可用于 CI/CD 中加密凭证。

### 密钥安全

- `db.key` 由 `RAND_bytes()` 生成，32 字节完全随机
- 每个开发者独立生成，互不共享（每个人连自己的 MySQL）
- `db.key` 和 `db_config.yml` 已加入 `.gitignore`，不会提交到仓库
- 若密钥泄露，重新生成密钥并重新加密所有凭证即可
