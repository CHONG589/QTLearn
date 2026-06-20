# Code Style Rules

## 基础约定

- C++ 标准：遵循现代 C++，优先使用智能指针。
- GUI 框架：优先使用 Qt 6.x。使用 Widgets 处理复杂桌面应用，使用 QML 处理现代化或移动端界面。

## 命名规范

- 类名 (Class)：大驼峰（PascalCase），例如 MainWindow、NetworkManager。
- 函数名 (Function)：小驼峰（camelCase），例如 updateData()、setHostName()。
- 私有成员变量：小驼峰加 m_ 前缀，例如 m_socket、m_timeout。
- 全局/静态变量：小驼峰加 g_ 前缀，例如 g_appConfig。
- 宏与常量：全大写加下划线，例如 MAX_BUFFER_SIZE、DEFAULT_TIMEOUT。

## 代码排版与格式

- 缩进：4 个空格，禁止使用 Tab 键。
- 大括号 {}：独占一行。例如：

```Cpp
if (isValid) {
    doSomething();
}

```
- 指针与引用：`*` 和 `&` 靠近类型。例如：`QString *name = nullptr;`。

## 函数注释

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
- 函数实现过程中，添加必要的注释解释原因；

## 核心架构与设计模式

- 信号与槽 (Signals & Slots)：优先使用基于函数指针的新语法（`connect(sender, &Sender::signal, receiver, &Receiver::slot)`），不使用老式字符串宏 `SIGNAL/SLOT`。
- 内存管理：使用父子对象树进行内存回收（向构造函数传递 `parent`）；其余情况强制使用智能指针（如 `std::unique_ptr` 或 `QSharedPointer`）。
- 业务与UI分离：必须将业务逻辑封装在 C++ 类中，不要在 UI（如 `MainWindow.cpp` 或 `*.qml`）中直接处理复杂数据计算和网络请求。