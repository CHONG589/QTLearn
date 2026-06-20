# 代码检查报告：Tree.h

**检查文件**：`src/tree/Tree.h`
**检查依据**：`.claude/rules/code-style.md`
**检查日期**：2026-06-20

---

## !! 严重问题

| 文件:行号 | 问题描述 | 规范依据 | 修复建议 |
|-----------|----------|----------|----------|
| Tree.h:27-36 | `DataManager` 单例模式不完整：缺少 `private` 构造函数和 `= delete` 拷贝构造/赋值运算符，外部可直接 `DataManager dm;` 或拷贝单例 | C++ 单例模式 | 添加 `private: DataManager() = default;` 和 `DataManager(const DataManager &) = delete; DataManager &operator=(const DataManager &) = delete;` |
| Tree.h:104 | `TreeItem *rootItem` 无默认初始化值，若构造函数提前返回或被误用会产生悬空指针 | 防御性编程 | 改为 `TreeItem *rootItem = nullptr;` |

## ! 警告

| 文件:行号 | 问题描述 | 规范依据 | 修复建议 |
|-----------|----------|----------|----------|
| Tree.h:18 | `Ui::TreeClass ui;` 私有成员未使用 `m_` 前缀 | 命名规范：私有成员 `m_` 前缀 | 改为 `m_ui`，对应 .cpp 中 `ui.setupUi` → `m_ui.setupUi` |
| Tree.h:104 | `TreeItem *rootItem;` 私有成员未使用 `m_` 前缀 | 命名规范：私有成员 `m_` 前缀 | 改为 `m_rootItem`，对应 .cpp 中所有引用 |
| Tree.h:6-7 | `#include "../log/log.h"` 和 `#include "../db/QDBConn.h"` 在头文件中未使用（无任何类引用其中符号） | 头文件最小化原则 | 移除这两个 include，移入 `Tree.cpp` |
| Tree.h:73 | `TreeModel` 继承 `QAbstractItemModel` 但未显式包含对应头文件，依赖 `<QtWidgets/QWidget>` 的传递包含 | 显式包含原则 | 添加 `#include <QAbstractItemModel>` |
| Tree.h:21,32 | `Node::id` 为 `int`，但 `DataManager::insertNode` 输出参数 `newId` 为 `qlonglong`，类型不一致；数据库自增 ID 为 64 位，截断为 `int` 有风险 | 类型安全 | 将 `Node::id` 和 `TreeItem::m_id` 改为 `qlonglong`，或将 `newId` 退回 `int`（若确认 ID 不超过 32 位范围） |
| Tree.h:9,27,38,73 | 类声明大括号风格不一致：`Tree` 的 `{` 独占一行（Allman），`DataManager`/`TreeItem`/`TreeModel` 的 `{` 与声明同行（K&R） | 格式一致性 | 统一为 K&R 风格（与规范示例一致），将 `Tree` 的 `{` 移至同行 |

## * 建议

| 文件:行号 | 问题描述 | 规范依据 | 修复建议 |
|-----------|----------|----------|----------|
| Tree.h:21-25 | `Node` 结构体成员缺少注释，新读者难以理解字段语义 | 代码可读性 | 为 `id`、`name`、`type` 添加注释说明含义和取值 |
| Tree.h:27,38,73 | `DataManager`、`TreeItem`、`TreeModel` 类缺少 Doxygen 类注释（`@brief`/`@details`） | 注释规范 | 为每个类添加 `@brief` 和 `@details` 注释 |
| Tree.h:67 | `QList<TreeItem *>` 手动管理子节点生命周期，`~TreeItem` 通过 `qDeleteAll` 释放 | 现代 C++ | 可考虑 `QList<std::unique_ptr<TreeItem>>`，自动析构无需手动 `qDeleteAll` |

---

## 通过项

| 检查项 | 状态 |
|--------|------|
| 类名 PascalCase（Tree / Node / DataManager / TreeItem / TreeModel） | ✓ |
| 函数名 camelCase（所有 32 个公开方法） | ✓ |
| 宏命名 UPPER_SNAKE_CASE | N/A |
| 缩进使用 4 个空格、无 Tab | ✓ |
| 指针/引用 `*` `&` 靠近类型 | ✓ |
| const 正确性（所有 getter 均标记 const） | ✓ |
| `Q_OBJECT` 宏（`Tree`、`TreeModel`） | ✓ |
| `QAbstractItemModel` 接口完整（index/parent/rowCount/columnCount/data/flags/setData/canFetchMore/fetchMore/hasChildren） | ✓ |
| `explicit` 构造函数（TreeModel） | ✓ |
| override 关键字（所有虚函数重写） | ✓ |
| 业务与 UI 分离（Tree=UI, DataManager=DB, TreeItem=Data, TreeModel=Bridge） | ✓ |

---

## 总结

Tree.h 整体代码质量良好，核心架构职责清晰。2 个严重问题集中在单例模式完整性（可导致多实例 bug）和成员初始化（悬空指针风险），建议优先修复。6 个警告主要为命名规范一致性和头文件依赖清理，影响可控但应统一。与上次检查相比，`catch(...)` 吞噬异常、`const_cast` 破坏 const 正确性、`lastInsertId` 类型截断等问题已在 Tree.cpp 中修复。
