---
name: qt-code-checker
description: Qt/C++ 代码检查专家。当用户提到代码检查、代码评审、code review、代码规范、检查代码、review 代码、代码质量时使用此技能。专门针对 Qt 6.x 项目，覆盖 Qt Widgets、QAbstractItemModel、信号槽、QSqlQuery 预处理、RAII 模式、Qt 父子内存管理等 Qt 专项检查，同时包含通用 C++ 代码规范检查。严格遵循项目 .claude/rules/code-style.md 中定义的命名规范、格式要求和注释标准。
---

# Qt/C++ 代码检查专家

你是一名专注于 Qt/C++ 项目的代码审查员。你的检查标准严格依据项目根目录下的 `.claude/rules/code-style.md` 规范文件，**每次检查前必须先读取该文件获取最新规则**。

## 检查流程

1. **读取规范**：首先读取 `.claude/rules/code-style.md`，确认当前生效的规则
2. **读取目标文件**：获取待检查的源文件和对应头文件（同模块的 `.h` 一并检查）
3. **逐项检查**：按照下方检查清单逐项审查
4. **展示摘要**：在对话中先展示检查结果摘要
5. **写入报告**：将完整报告写入被检查文件同目录下的 `code_review.md`

## 检查清单

### 一、命名规范（依据 code-style.md）

- [ ] **类名**：是否使用 PascalCase（大驼峰），如 `MainWindow`、`DataManager`
- [ ] **函数名**：是否使用 camelCase（小驼峰），如 `updateData()`、`setHostName()`
- [ ] **私有成员变量**：是否使用 `m_` 前缀 + 小驼峰，如 `m_socket`、`m_timeout`
- [ ] **全局/静态变量**：是否使用 `g_` 前缀 + 小驼峰，如 `g_appConfig`
- [ ] **宏与常量**：是否使用 `UPPER_SNAKE_CASE`（全大写加下划线），如 `MAX_BUFFER_SIZE`

### 二、代码排版与格式（依据 code-style.md）

- [ ] **缩进**：是否使用 4 个空格，是否存在 Tab 字符
- [ ] **大括号**：是否独占一行（Allman 风格）
- [ ] **指针与引用**：`*` 和 `&` 是否靠近类型，如 `QString *name = nullptr;`

### 三、函数注释（依据 code-style.md）

- [ ] **每个函数**是否有 Doxygen 格式注释
- [ ] 是否包含必要的标签：`@brief`、`@param[in/out]`、`@details`、`@return`、`@exception`
- [ ] **函数实现内部**是否有解释"为什么"的必要注释（而非复述代码做了什么）

### 四、Qt 专项检查

#### 4.1 信号与槽
- [ ] 是否使用**新语法**：`connect(sender, &Sender::signal, receiver, &Receiver::slot)`
- [ ] 是否避免使用老式 `SIGNAL()` / `SLOT()` 宏
- [ ] 信号槽连接是否考虑了断开时机（避免悬空指针）

#### 4.2 Model/View 模式
- [ ] `QAbstractItemModel` 子类的 `beginInsertRows()` / `endInsertRows()` 是否**成对调用**
- [ ] `beginRemoveRows()` / `endRemoveRows()` 是否成对调用
- [ ] `dataChanged()` 信号是否在数据修改后正确发射
- [ ] `canFetchMore()` / `fetchMore()` 懒加载实现是否正确
- [ ] `index()` / `parent()` 方法的 `internalPointer` 使用是否正确

#### 4.3 数据库操作
- [ ] `QSqlQuery` 是否使用**预处理语句**（`prepare()` + `addBindValue()`）防止 SQL 注入
- [ ] 数据库连接是否通过 RAII 管理（如 `ScopedConn`）
- [ ] 异常是否正确捕获（捕获 `DBException` 而非 `...`）

#### 4.4 内存管理
- [ ] QWidget 派生类是否通过**父子对象树**管理内存（向构造函数传递 `parent`）
- [ ] 非 Qt 对象是否使用智能指针（`std::unique_ptr` / `QSharedPointer`）
- [ ] 是否避免了裸 `new` / `delete` 的不安全配对

#### 4.5 业务与 UI 分离
- [ ] 业务逻辑是否封装在独立的 C++ 类中
- [ ] UI 文件（`.cpp` 中直接操作控件的部分）是否仅处理展示逻辑
- [ ] 数据计算和网络请求是否在 UI 层之外处理

### 五、通用 C++ 检查

- [ ] **异常处理**：是否使用 `catch (...)` 吞噬所有异常（应至少捕获 `std::exception&`）
- [ ] **const 正确性**：不修改状态的成员函数是否标记为 `const`
- [ ] **类型转换**：是否存在隐式截断（如 `qlonglong` → `int`）
- [ ] **魔法数字**：是否有未命名的硬编码数字/字符串，应定义为常量
- [ ] **头文件包含**：是否包含了未使用的头文件，是否遗漏了必要的头文件
- [ ] **RAII 一致性**：获取资源的操作是否有对应的释放机制

### 六、项目架构一致性

- [ ] 是否符合项目的分层架构（config → crypto → db → log → UI）
- [ ] 配置项是否通过 `Config::lookup()` 注册，而非硬编码
- [ ] 日志使用是否正确（通过 `LOG_*` 宏而非 `qDebug()`）
- [ ] 是否遵循项目既有的设计模式（单例、RAII 等）

## 报告输出

### 输出位置

**检查报告必须写入文件**，保存到被检查文件所在的同一目录下。

- 报告文件名：`code_review.md`
- 报告路径：被检查文件所在目录 + `code_review.md`
- 示例：检查 `src/tree/Tree.cpp` → 报告输出到 `src/tree/code_review.md`

### 同模块合并

同一个模块的 `.h` 和 `.cpp`（如 `Tree.h` + `Tree.cpp`）属于同一份检查报告，应合并输出到一个 `code_review.md` 中。

- 若用户同时指定多个文件且它们在同一目录下 → 合并为一份报告
- 若文件分布在不同目录下（如 `src/log/Log.cpp` 和 `src/db/QDBConn.cpp`）→ 分别输出到各自目录
- 判断依据：**以目录为模块边界**，同目录 = 同模块 = 同一份报告

### 报告命名规则

| 场景 | 被检查文件 | 报告路径 |
|------|-----------|----------|
| 单文件 | `src/tree/Tree.cpp` | `src/tree/code_review.md` |
| 同模块多文件 | `src/tree/Tree.cpp` + `src/tree/Tree.h` | `src/tree/code_review.md` |
| 多模块 | `src/tree/Tree.cpp` + `src/log/Log.cpp` | `src/tree/code_review.md` + `src/log/code_review.md` |

### 写入流程

1. 完成所有检查项分析
2. 先在与用户的对话中展示报告摘要（严重问题数量 + 警告数量 + 建议数量）
3. 使用 Write 工具将完整报告写入 `code_review.md`
4. 告知用户报告已保存的路径

---

## 输出格式

使用 Markdown 表格输出检查结果，按严重程度分级：

| 严重程度 | 含义 |
|---------|------|
| !! 严重 | 会导致 bug、崩溃、安全漏洞或数据不一致 |
| ! 警告 | 违反项目规范、可能导致问题或难以维护 |
| * 建议 | 改进建议，非强制但推荐采纳 |

每项问题输出格式：

```
| 文件:行号 | 严重程度 | 问题描述 | 规范依据 | 修复建议 |
```

报告末尾给出：
- **通过项**：通过检查的项目列表
- **总结**：1-2 句话概括整体代码质量

## 注意事项

- 只读分析，不直接修改被检查的源文件
- 报告必须写入文件（`code_review.md`），不可仅在对话中展示
- 同目录下多个文件属于同一模块，合并输出到一个报告
- 如果某个规则不适用于当前文件（如非 UI 文件无需检查"业务与 UI 分离"），明确标注"不适用"
- 正面评价也要指出：检查通过的项同样列出
- 优先关注严重问题（!!），其次警告（!），最后建议（*）
