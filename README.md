# QTLearn

Qt 6.5.3 学习项目，实现了一个数据库驱动的树形控件演示程序，包含自建的配置系统、AES-256-GCM 加密模块、数据库连接池和日志系统。

## 环境

- **IDE**: Visual Studio 2022 (v143)，打开 `QT_Learn.sln`
- **Qt**: 6.5.3_msvc2019_64，模块 `core;gui;widgets;sql`
- **数据库**: MySQL (QMYSQL)
- **依赖**: yaml-cpp, OpenSSL

## 快速开始

```bash
# 1. 复制配置模板，修改数据库连接信息
copy config\db_config_example.yml.template config\db_config.yml

# 2. 生成 AES 密钥 + 加密数据库凭证（在项目根目录运行）
crypto_tool\x64\Debug\crypto_tool.exe --genkey

crypto_tool\x64\Debug\crypto_tool.exe --encrypt

# 3. VS 中 F5 运行 QT_Learn
```

## 项目结构

```
QTLearn/
├── src/
│   ├── common/          # 路径与常量（AppPaths.h）
│   ├── config/          # 配置系统（yaml-cpp + 类型安全 ConfigVar）
│   ├── crypto/          # AES-256-GCM 加解密
│   ├── db/              # 数据库连接池 + RAII 事务
│   ├── log/             # 日志系统（多日志器/输出器/格式模板）
│   └── tree/            # UI：QTreeView + QAbstractItemModel + 懒加载
├── crypto_tool/         # 独立加密工具（纯 C++，不依赖 Qt）
├── config/              # 配置文件（db.key 和 db_config.yml 不入库）
├── include/
│   ├── openssl/         # OpenSSL 头文件
│   └── yaml-cpp/        # Yaml-cpp 头文件
├── lib/                 # 第三方库
└── doc/                 # 文档
```

## 架构分层

```
main → Config(加载YAML) → Crypto(加载密钥) → DBPool(连接池) → Tree(UI)
```

- **Config**: `Config::lookup<T>()` 懒注册配置项，支持热更新回调
- **Crypto**: AES-256-GCM 认证加密，密钥不入库
- **DBPool**: `QThreadStorage` 每线程独立连接池，`ScopedConn`/`DBTransaction` RAII 管理
- **Log**: 多日志器 + 文件/标准输出，YAML 配置热更新
- **Tree**: `TreeModel`(QAbstractItemModel) + `TreeItem`(内存节点) + `DataManager`(DB操作) + 懒加载
