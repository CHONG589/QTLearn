# QTLearn

Qt 6.5.3 学习项目，实现了一个数据库驱动的公司组织架构树形控件演示程序（部门 → 分类 → 内容三层结构），包含自建的配置系统、AES-256-GCM 加密模块、数据库连接池和日志系统。

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

# 3. 在 MySQL 中执行建表和数据脚本
#     mysql> source E:/Code/QTCode/QTLearn/sql/org_tree_init.sql

# 4. VS 中设置 TreeExplorer 为启动项目，F5 运行
```

## 项目结构

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
├─sql                      # 数据库脚本
│  └─org_tree_init.sql     # 建表 + 测试数据
├─QTLearnCommon            # 公共静态库
│  ├─common                # AppPaths.h
│  ├─config                # 配置系统
│  ├─crypto                # 加密模块（std::string 接口）
│  ├─db                    # 数据库连接池
│  └─log                   # 日志系统
├─TreeExplorer             # 业务层 GUI 应用
│  ├─main.cpp
│  └─tree                  # Tree 窗口 + ClassModel + InfoModel + TreeItem + DataManager
│     ├─code_review.md     # 代码审查报告
│     └─QTreeView_Model_Guide.md  # QTreeView+Model 技术文档
```

## 架构分层

解决方案包含三个项目：

| 项目 | 类型 | 依赖 | 说明 |
|------|------|------|------|
| QTLearnCommon | 静态库 .lib | Qt(core;sql), yaml-cpp, OpenSSL | 公共基础设施 |
| TreeExplorer | GUI .exe | QTLearnCommon, Qt(gui;widgets) | 业务层应用 |
| crypto_tool | 控制台 .exe | QTLearnCommon, OpenSSL | 独立加密工具 |

```
crypto_tool ──→ QTLearnCommon.lib ←── TreeExplorer
 (无 Qt)          (仅链接 Crypto.obj)      (链接全部模块)
```

### 公共模块 (QTLearnCommon)

- **Config**: `Config::lookup<T>()` 懒注册配置项，支持热更新回调
- **Crypto**: AES-256-GCM 认证加密，接口使用 `std::string`（与 Qt 解耦），密钥不入库
- **DBPool**: `QThreadStorage` 每线程独立连接池，`ScopedConn`/`DBTransaction` RAII 管理
- **Log**: 多日志器 + 文件/标准输出，YAML 配置热更新

### 业务层 (TreeExplorer)

- **Tree 窗口**: 承载 `treeView_Class`（部门分类树）和 `treeView_Info`（内容树）+ 六个操作按钮 + `textEdit`
- **ClassModel**: 实现 `QAbstractItemModel`，展示部门层级 + 分类叶子节点，点击分类自动加载内容
- **InfoModel**: 实现 `QAbstractItemModel`，展示内容树，支持勾选框 + 父子勾选联动（全选/半选/取消）
- **TreeItem**: 内存树节点，维护父子关系、懒加载状态、勾选状态
- **DataManager**: 单例，封装对 `org_tree` 表的全部数据库操作，使用预处理防 SQL 注入
- **数据库表**: `org_tree`（id/name/node_type/parent_id/sort_order），废弃旧 `tree_nodes` 表

详见 `TreeExplorer/tree/QTreeView_Model_Guide.md`。
