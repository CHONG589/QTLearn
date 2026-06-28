# Code Review: Tree.h / Tree.cpp

## 检查摘要

| 级别 | 数量 | 说明 |
|------|------|------|
| ✅ 已修复 | 7 | 全部问题已解决 |

---

## 修复明细

| 文件:行号 | 严重程度 | 问题描述 | 修复方式 |
|-----------|---------|---------|---------|
| Tree.cpp:90-100 | !! 严重 | `TreeItem::row()` 直接访问 `m_parent->m_children`，`if (m_parent)` 判空未保护循环条件 | 改为 early return `if (!m_parent) return 0;`，遍历改用公有 API `childCount()`/`child(i)` |
| Tree.h + Tree.cpp | ! 警告 | 函数注释缺失 | 全部公有/私有方法补全 Doxygen 注释 |
| Tree.h + Tree.cpp | ! 警告 | 节点类型魔法数字 `0/1/2` | 定义 `NODE_TYPE_DEPT/CATEGORY/CONTENT` 常量 |
| Tree.cpp:selectAll/deselectAll | ! 警告 | `beginResetModel/endResetModel` 导致展开状态丢失 | 移除 reset，`setChildrenCheckState` 已逐个 emit `dataChanged` |
| Tree.cpp:collectCheckedPaths | * 建议 | 未加载的子树不会被遍历 | 递归前增加 `loadChildren(child)` 调用 |
| Tree.cpp:queryChildren | * 建议 | SQL 运行时拼接的性能关注 | 添加注释说明动态拼接是刻意的（WHERE 结构根据参数变化） |
| Tree.cpp:formatCheckedTree | * 建议 | `std::function` 递归 lambda | 添加注释说明这是 C++ lambda 递归的标准写法 |
| Tree.cpp:onAdd | * 建议 | 空树文本仍写入 textEdit | 增加 `treeText.isEmpty()` 判断 |
| Tree.h 槽函数 | * 建议 | 槽函数缺少 UI 控件对应说明 | 每个槽函数 `@brief` 标明对应的按钮 |

---

## 通过项

- [x] **命名规范**：类名 PascalCase、函数名 camelCase、成员变量 `m_` 前缀、全局变量 `g_` 前缀
- [x] **缩进与大括号**：4 空格缩进，大括号独占一行（Allman 风格）
- [x] **指针与引用**：`*` 和 `&` 统一靠近类型
- [x] **信号槽新语法**：全部 `connect(sender, &Sender::signal, receiver, &Receiver::slot)`
- [x] **Model/View 接口**：`beginInsertRows/endInsertRows` 成对调用，`dataChanged` 正确处理
- [x] **数据库预处理**：所有 SQL 使用 `prepare()` + `addBindValue()`
- [x] **数据库 RAII**：`ScopedConn` 管理连接生命周期
- [x] **异常处理**：全部 `catch (const DBException &e)`
- [x] **const 正确性**：getter 均标记 `const`（`collectCheckedPaths` 因需 `loadChildren` 而去掉 const 是合理的）
- [x] **内存管理**：Qt 对象父子树回收，TreeItem 用 `qDeleteAll`
- [x] **架构分层**：DataManager(DB) → Model(数据) → Tree(UI)
- [x] **日志使用**：全部 `LOG_*` 宏
- [x] **函数注释**：全部 Doxygen 格式
- [x] **成员变量注释**：全部 `///<` 行尾注释

---

## 总结

代码审查中的所有问题已修复。代码质量良好，符合项目 code-style.md 规范。
