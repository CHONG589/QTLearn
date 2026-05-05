# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

Qt 6.5.3 学习项目，Windows 平台，Visual Studio 2022 + Qt VS Tools 构建。实现了一个数据库驱动的树形控件演示程序，包含自建的数据库连接池和日志系统。

## 构建与环境

- **IDE**: Visual Studio 2022 (v143 工具链)，打开 `QT_Learn.sln` 即可编译
- **Qt 版本**: 6.5.3_msvc2019_64
- **Qt 模块**: core;gui;network;widgets;sql
- **数据库**: MySQL (通过 QMYSQL 驱动)
- **配置**: Debug|x64 和 Release|x64
- **项目文件**: `QT_Learn.vcxproj` (Qt VS Tools 格式)

## 架构概览

```
main.cpp          → 入口：初始化 Logger → DBPool → 显示 Tree 窗口
Tree.h/cpp        → UI 层：Tree 窗口 + TreeModel + TreeItem + DataManager
QLog.h/cpp        → 日志：异步流式日志，按天/按大小滚动
QDBConn.h/cpp     → 数据库：DBPool（线程隔离连接池）+ DBConn + ScopedConn(RAII) + DBTransaction(RAII)
QDB/              → 独立子项目：QDB 共享库（当前为空壳）
```

### 核心分层

1. **UI 层** (`Tree.h/cpp`)：`Tree` 是主窗口（包含 `QTreeView`），`TreeModel` 实现 `QAbstractItemModel`（懒加载 + 编辑），`TreeItem` 是内存树节点，`DataManager` 封装所有 DB 操作（单例）。`Node` 是数据库查询结果的 DTO。

2. **数据库层** (`QDBConn.h/cpp`)：自建连接池 `DBPool`（单例），基于 `QThreadStorage` 实现每线程独立连接池，避免 `QSqlDatabase` 跨线程使用问题。`DBConn` 封装 `QSqlDatabase` 操作，支持预处理、流式查询、事务。`ScopedConn` 通过 RAII 自动获取/归还连接。`DBTransaction` 通过 RAII 自动提交/回滚。所有数据库错误通过 `DBException` 抛出。

3. **日志层** (`QLog.h/cpp`)：`Logger` 单例，支持同步/异步模式，异步时使用独立 `QThread` 写文件。日志按天滚动，按大小切分。通过 `LogStream` 临时对象 + 宏实现流式 API（`LOG_DEBUG() << "msg"`）。

### 关键设计决策

- `main.cpp` 使用 `/subsystem:console /entry:mainCRTStartup` 链接选项，使 GUI 程序同时启动控制台窗口用于调试输出。
- `Tree.ui` 定义主窗口布局（一个 `QFrame` 内含 `QTreeView`）。
- `Tree.qrc` 当前为空，预留用于图标等资源（`data()` 中 `DecorationRole` 引用了 `:/icons/folder.png` 和 `:/icons/file.png`，但尚未添加资源文件）。
- 数据库通过 `DBPool::instance().init(config)` 初始化一次（在 `main.cpp` 中），之后所有线程通过 `ScopedConn` 获取连接。

## 日志使用

```cpp
Logger::instance().init("./logs", LogLevel::DEBUG, true);  // 异步模式

LOG_DEBUG() << "debug message";
LOG_INFO()  << "info message";
LOG_WARN()  << "warning message";
LOG_ERROR() << "error message";
```

级别过滤在宏层面完成：低于当前级别的日志不会构造 `LogStream` 对象，避免无效开销。

## 代码风格

### 函数

- 每个函数的注释都必须按照如下格式添加注释；

```Cpp
/**
 * @brief 获取/创建对应参数名的配置参数
 * @param[in] name 配置参数名称
 * @param[in] default_value 参数默认值
 * @param[in] description 参数描述
 * @details 获取参数名为name的配置参数,如果存在直接返回
 *          如果不存在,创建参数配置并用default_value赋值
 * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
 * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
 */
```

- 函数(包含类中的函数)命名：小驼峰命名法；

- 函数实现过程中，添加必要的注释解释原因；

### 类

- 类名：大驼峰命名；

- 类变量：`m_varName` 这样用小写 `m_` 加小驼峰命名的方式，并且添加变量作用的注释

### 普通变量命名

采用小驼峰命名；
