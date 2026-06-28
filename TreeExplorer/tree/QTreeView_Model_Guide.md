# QTreeView + QAbstractItemModel 树状结构完整指南

> 基于 QTLearn 项目中 TreeExplorer 模块的实际代码，深入剖析从数据库到界面渲染的完整树形数据流。

---

## 1. 项目概述

TreeExplorer 是一个**组织架构与内容管理**的桌面工具，实现了"部门 -> 分类 -> 内容"三层树形结构。左侧 `QTreeView` 展示部门层级树（部门下挂分类），右侧 `QTreeView` 展示选中分类下的内容树（带勾选框，支持多选联动），用户可将勾选的内容以 ASCII 树状文本输出到编辑框中，用于后续处理。

简而言之：**左侧选分类，右侧勾内容，一键汇总成树形报告**。

---

## 2. 整体架构

### 2.1 类关系图

```
┌─────────────────────────────────────────────────────────────┐
│                        Tree (QWidget)                        │
│  主窗口：持有 UI 布局、两个 Model、双向绑定到 QTreeView       │
│                                                              │
│  ┌────────────┐    ┌──────────────┐    ┌──────────────┐     │
│  │  Ui::Tree   │───>│  ClassModel  │    │  InfoModel   │     │
│  │  Designer   │    │  (QAbstract  │    │  (QAbstract  │     │
│  │  生成 UI    │    │  ItemModel)  │    │  ItemModel)  │     │
│  └────────────┘    └──────┬───────┘    └──────┬───────┘     │
│                           │                    │              │
│                    ┌──────┴───────┐    ┌──────┴───────┐      │
│                    │  treeView_   │    │  treeView_   │      │
│                    │  Class       │    │  Info        │      │
│                    │  (QTreeView) │    │  (QTreeView) │      │
│                    └──────────────┘    └──────────────┘      │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  TreeItem  — 内存树节点（通用数据结构，被两个 Model 共用）│    │
│  └──────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  DataManager  — 数据库操作单例                          │    │
│  │  底层依赖：ScopedConn -> DBConn -> DBPool              │    │
│  └──────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 依赖与交互流程

| 调用方向 | 触发场景 | 方法链 |
|----------|----------|--------|
| Tree -> ClassModel | 构造时 | `new ClassModel(this)` 构造时自动加载顶级部门 |
| ClassModel -> DataManager | 懒加载展开 | `fetchMore()` -> `loadChildren()` -> `DataManager::queryChildren()` |
| Tree -> InfoModel | 点击分类 | `onCategoryClicked()` -> `InfoModel::loadCategory()` |
| InfoModel -> DataManager | 懒加载内容 | `fetchMore()` -> `loadChildren()` -> `DataManager::queryChildren()` |
| Tree -> InfoModel | 全选/取消 | `onSelectAll()` -> `InfoModel::selectAll()` |
| Tree -> InfoModel | 汇总已选 | `formatCheckedTree()` -> `InfoModel::data(CheckStateRole)` / `InfoModel::index()` / `rowCount()` |
| DataManager -> ScopedConn | 每次 DB 操作 | `ScopedConn conn` RAII 获取连接 -> `conn->prepare()` -> `execPrepared()` |

### 2.3 数据流全景（从点击到渲染）

```
用户点击左侧树展开按钮
  │
  ▼
QTreeView 调用 ClassModel::canFetchMore()
  │  → 未加载且非分类节点 → true
  ▼
QTreeView 调用 ClassModel::fetchMore()
  │
  ▼
ClassModel::loadChildren(parentItem)
  │  ① DataManager::queryChildren(id) 查数据库
  │  ② beginInsertRows() 通知视图
  │  ③ new TreeItem(...) 构建内存节点
  │  ④ endInsertRows() 视图渲染新行
  │
  ▼
用户点击分类节点 → Tree::onCategoryClicked()
  │
  ▼
InfoModel::loadCategory(categoryId)
  │  ① beginResetModel() 清空旧树
  │  ② 查询该分类下的顶层内容节点
  │  ③ endResetModel() 视图刷新
  │
  ▼
Tree::onCategoryClicked() → treeView_Info->expandAll()
  │  → 视图展开全部节点 → 触发 InfoModel::canFetchMore/fetchMore 链式加载
```

---

## 3. 核心数据结构

### 3.1 Node DTO — 数据库查询结果传输对象

`Node` 是一个轻量 POD 结构体，定义在 `Tree.h` 顶部，用于 `DataManager::queryChildren()` 的返回值：

```cpp
struct Node {
    qlonglong id;        // 节点在 org_tree 表中的主键 ID
    QString name;        // 节点显示名称
    int nodeType;        // 节点类型：0=部门, 1=分类, 2=内容
    int sortOrder;       // 同级排序序号
};
```

之所以单独设计 `Node` 而非直接返回 `TreeItem`，是因为 **DTO 与领域对象的职责分离**：`Node` 只承载数据库原始行数据，不包含父子关系、勾选状态、懒加载标志等运行时状态。`TreeItem` 在内存树中才被构建，附带 UI 所需的全部运行时属性。

### 3.2 TreeItem — 内存树节点

`TreeItem` 是整个树形结构的核心数据载体，被 `ClassModel` 和 `InfoModel` 两个 model 共用。

| 字段 | 类型 | 用途 | 说明 |
|------|------|------|------|
| `m_id` | `qlonglong` | 数据库主键 | 对应 org_tree.id，用于懒加载时查询子节点 |
| `m_name` | `QString` | 节点显示名称 | `data(Qt::DisplayRole)` 的返回值 |
| `m_nodeType` | `int` | 节点类型常量 | 0=部门(NODE_TYPE_DEPT), 1=分类(NODE_TYPE_CATEGORY), 2=内容(NODE_TYPE_CONTENT) |
| `m_children` | `QList<TreeItem*>` | 子节点列表 | 析构时通过 `qDeleteAll` 递归释放 |
| `m_parent` | `TreeItem*` | 父节点指针 | 非所有者（父节点负责子节点生命周期），根节点为 nullptr |
| `m_loaded` | `bool` | 懒加载标志 | 默认 false，由 `loadChildren()` 设为 true |
| `m_checkState` | `Qt::CheckState` | 勾选状态 | 仅 InfoModel 使用，ClassModel 完全忽略；默认 `Unchecked` |

关键设计要点：

1. **`m_parent` 不持有所有权**：子节点的生命周期由父节点的 `m_children` 管理，通过 `qDeleteAll(m_children)` 递归释放。`m_parent` 仅用于 `row()` 方法计算行号和 `parent()` 方法向上导航。

2. **`m_loaded` 的默认值 false**：这是懒加载机制的基础。节点构造时并不预加载子节点，只有视图展开时才触发数据库查询。

3. **`m_checkState` 的初始值 Unchecked**：由于 C++ 没有在类内初始化列表中默认构造，`TreeItem` 的构造函数在初始化列表中明确设置 `m_checkState(Qt::Unchecked)`，确保无论是 ClassModel 还是 InfoModel 构造节点，勾选状态始终从"未勾选"开始。

4. **`row()` 方法的实现有意义的选择**：通过遍历父节点的子列表查找 `this` 指针来定位行号，而非存储 `m_row` 成员变量。这样做避免了中间插入/删除节点时需要更新所有后序兄弟节点的行号缓存。虽然 `O(n)` 的查找在极深树中可能有性能问题，但对于组织架构管理这种规模（通常数千节点以内），简洁性优于微优化。

```cpp
int TreeItem::row() const
{
    if (!m_parent) return 0;
    for (int i = 0; i < m_parent->childCount(); ++i) {
        if (m_parent->child(i) == this) return i;
    }
    return 0;
}
```

---

## 4. Model/View 原理

### 4.1 QAbstractItemModel 核心虚函数

Qt 的 Model/View 架构中，`QAbstractItemModel` 是数据与视图之间的桥梁。视图通过调用 model 的虚函数获取数据，model 通过发射信号通知视图数据变化。

`ClassModel` 和 `InfoModel` 都继承自 `QAbstractItemModel`，必须实现以下 6 个纯虚函数和 3 个常用重写：

| 虚函数 | 分类 | 用途 | 视图调用时机 |
|--------|------|------|-------------|
| `index()` | 必实现 | 根据行列和父索引创建 `QModelIndex` | 视图需要访问某行某列时 |
| `parent()` | 必实现 | 返回指定索引的父索引 | 视图需要向上导航时 |
| `rowCount()` | 必实现 | 返回指定父节点下的行数 | 视图需要知道显示多少行时 |
| `columnCount()` | 必实现 | 返回列数（本项目固定 1 列） | 视图初始化布局时 |
| `data()` | 必实现 | 返回指定角色下的数据 | 视图需要渲染单元格时 |
| `flags()` | 必实现 | 返回节点的交互标志 | 视图需要知道是否可点/可勾选时 |
| `canFetchMore()` | 可选 | 是否还有更多数据可加载 | 视图判断是否显示展开箭头 |
| `fetchMore()` | 可选 | 加载更多数据 | 用户点击展开箭头时 |
| `hasChildren()` | 可选 | 节点是否有子节点 | 视图判断是否显示展开箭头 |

### 4.2 index() — 创建模型索引

`QModelIndex` 是视图访问 model 数据的"句柄"。每个有效的 `QModelIndex` 内部存储了 `row`、`column`、`internalPointer`（指向对应的 `TreeItem` 指针）和指向 model 自身的 `const void *`。

```cpp
QModelIndex ClassModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;   // 无效父索引 → 使用虚拟根节点

    TreeItem *child = parentItem->child(row);
    return child ? createIndex(row, column, child) : QModelIndex();
}
```

**设计要点**：
- `parent.isValid()` 判断：当 `parent` 是无效 `QModelIndex` 时，代表请求的是**顶层节点**（即 `m_rootItem` 的直接子节点）。这是 Qt 的约定——无效索引代表模型根。
- `createIndex(row, column, child)`：第三个参数是 `internalPointer`，存入门节点的裸指针。后续 `data()`、`flags()`、`parent()` 等方法都能通过 `static_cast<TreeItem *>(index.internalPointer())` 直接取回节点。
- 越界保护：`hasIndex()` 内部检查 row 和 column 的合法性。

### 4.3 parent() — 向上导航

```cpp
QModelIndex ClassModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    TreeItem *child = static_cast<TreeItem *>(index.internalPointer());
    TreeItem *parentItem = child->parent();
    if (!parentItem || parentItem == m_rootItem)
        return QModelIndex();   // m_rootItem 对视图中不可见

    return createIndex(parentItem->row(), 0, parentItem);
}
```

**设计要点**：
- `m_rootItem` 是虚拟根节点，对视图不可见。因此，当父节点是 `m_rootItem` 时，返回无效索引，视图认为这些节点是顶层项。
- 这是实现"虚拟根节点"模式的标准做法：model 内部有一层额外的根节点，但对视图透明。

### 4.4 rowCount() / columnCount() — 行列结构

```cpp
int ClassModel::rowCount(const QModelIndex &parent) const
{
    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;
    return parentItem->childCount();
}

int ClassModel::columnCount(const QModelIndex &) const
{
    return 1;  // 树形控件仅显示一列名称
}
```

`columnCount()` 固定返回 1，因为树形控件只需要显示节点名称。如果未来需要显示更多列（如节点类型、创建时间等），只需修改 `columnCount()` 和 `data()` 中对应列号的逻辑。

### 4.5 data() — 数据角色分发

`data()` 是 model 中最具表达力的函数，通过 `role` 参数区分不同的数据用途：

```cpp
QVariant InfoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    if (item == m_rootItem) return QVariant();

    if (role == Qt::DisplayRole)       return item->name();       // 显示文本
    if (role == Qt::CheckStateRole)    return item->checkState(); // 勾选状态
    return QVariant();
}
```

常见的 role 及其用途：

| Role 常量 | 用途 | 本项目中的值 |
|-----------|------|-------------|
| `Qt::DisplayRole` | 单元格显示文本 | `item->name()` |
| `Qt::DecorationRole` | 图标/装饰 | 部门用文件夹图标，分类用文件图标（仅 ClassModel） |
| `Qt::CheckStateRole` | 勾选框状态 | `item->checkState()`（仅 InfoModel） |
| `Qt::ToolTipRole` | 悬停提示 | 本项目未使用 |
| `Qt::FontRole` | 字体 | 本项目未使用 |

### 4.6 flags() — 节点交互能力

```cpp
// ClassModel — 只读，无勾选框
Qt::ItemFlags ClassModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

// InfoModel — 可勾选
Qt::ItemFlags InfoModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}
```

`InfoModel` 比 `ClassModel` 多了一个 `Qt::ItemIsUserCheckable` 标志，这让 `QTreeView` 在对应节点前渲染一个勾选框。当用户点击勾选框时，视图会自动调用 `model->setData(index, newState, Qt::CheckStateRole)`。

---

## 5. 懒加载机制

### 5.1 为什么需要懒加载

组织架构可能包含成千上万的节点，如果一次性从数据库加载所有节点：
- 内存占用巨大
- 初始加载时间过长
- 用户可能只浏览了顶部几个部门，大量数据加载后从未使用

懒加载的核心理念：**按需加载，展开时查询**。

### 5.2 canFetchMore / fetchMore 协议

Qt 的 `QAbstractItemModel` 提供了一对可选的虚函数 `canFetchMore()` 和 `fetchMore()`，视图通过它们实现增量加载。工作流程如下：

```
用户点击展开箭头
       │
       ▼
视图调用 model->canFetchMore(parent)
       │
       ├── 返回 false → 视图不显示展开箭头（或箭头变灰）
       │
       └── 返回 true  → 视图显示展开箭头
                │
                用户点击展开箭头
                │
                ▼
           视图调用 model->fetchMore(parent)
                │
                ▼
           查询数据库 → 创建 TreeItem → beginInsertRows/endInsertRows
                │
                ▼
           视图渲染新行
```

### 5.3 ClassModel 中的懒加载细节

```cpp
bool ClassModel::canFetchMore(const QModelIndex &parent) const
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    // 分类节点没有子节点，始终返回 false → 无展开箭头
    if (item->nodeType() == NODE_TYPE_CATEGORY) {
        return false;
    }
    return !item->isLoaded();
}

void ClassModel::fetchMore(const QModelIndex &parent)
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;
    loadChildren(item);
}
```

**设计要点**：
- `NODE_TYPE_CATEGORY` 始终返回 `false`，因为 "分类" 是部门树的叶子节点，其下的"内容"属于右侧 `InfoModel` 的领域。这样左侧树在分类节点处不再显示展开箭头，给用户清晰的导航提示。
- `loadChildren()` 是实际的加载逻辑，被 `fetchMore()` 调用。它通过 `beginInsertRows / endInsertRows` 通知视图，这是 Qt 允许视图增量更新的关键——如果不使用这对函数，视图无法知晓新行的加入。

### 5.4 loadChildren 实现

```cpp
void ClassModel::loadChildren(TreeItem *parentItem)
{
    if (parentItem->isLoaded()) return;  // 已加载则跳过

    auto list = DataManager::instance().queryChildren(parentItem->id());
    int begin = 0;
    int end = list.size() - 1;
    if (end >= begin) {
        beginInsertRows(indexFromItem(parentItem), begin, end);
        for (auto &dto : list) {
            auto *child = new TreeItem(dto.id, dto.name, dto.nodeType, parentItem);
            parentItem->appendChild(child);
        }
        endInsertRows();
    }
    parentItem->setLoaded(true);
}
```

**为什么先 `beginInsertRows` 再添加子节点？** Qt 要求：在调用 `beginInsertRows()` 之后、`endInsertRows()` 之前，model 的内部状态必须与 `beginInsertRows` 调用时的预期一致。如果先 `appendChild` 再 `beginInsertRows`，视图在 `beginInsertRows` 时会检查 `rowCount`，发现行数已经变化，导致断言失败。

### 5.5 为什么还需要 hasChildren

`canFetchMore()` 告诉视图"是否还有数据可加载"，但视图还需要知道**是否有子节点**来决定展开箭头的显示——即使数据还未加载，视图也需要知道箭头是否存在。

```cpp
bool ClassModel::hasChildren(const QModelIndex &parent) const
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    if (item->nodeType() == NODE_TYPE_CATEGORY)
        return false;           // 分类节点没有子节点
    if (item->isLoaded())
        return item->childCount() > 0;  // 已加载 → 看缓存
    return DataManager::instance().hasChildren(item->id());  // 未加载 → 查 DB
}
```

注意 `hasChildren` 在未加载时会查询数据库，使用 `SELECT 1 FROM org_tree WHERE parent_id=? LIMIT 1` 优化——只查是否存在，不返回具体数据。

### 5.6 InfoModel 的 expandAll 触发链式加载

在 `Tree::onCategoryClicked()` 中，加载完分类后立刻调用 `treeView_Info->expandAll()`：

```cpp
m_infoModel->loadCategory(item->id(), item->name());
m_ui.treeView_Info->expandAll();  // 展开全部节点
```

`expandAll()` 会递归触发每个节点的展开操作，每个展开操作都会调用 `canFetchMore()` → `fetchMore()` 链，从而实现**分类下所有内容节点的全量懒加载**。虽然叫"懒加载"，但在用户点击分类时通过 `expandAll` 实现了实际上的"预加载"效果——用户不需要逐个展开节点。

---

## 6. 双模型设计

### 6.1 为什么需要两个 Model

同一个项目中出现了 `ClassModel` 和 `InfoModel` 两个 `QAbstractItemModel` 子类。为什么不做成一个通用 model？

| 维度 | ClassModel | InfoModel |
|------|-----------|-----------|
| **用途** | 左侧部门分类树（导航） | 右侧内容树（选择） |
| **节点类型** | 部门(NODE_TYPE_DEPT) + 分类(NODE_TYPE_CATEGORY) | 内容(NODE_TYPE_CONTENT) |
| **勾选框** | 无 | 有 |
| **数据来源** | 构造时自动加载顶级部门 | 通过 `loadCategory()` 手动加载 |
| **只读/可编辑** | 只读（不可勾选、不可编辑） | 可勾选（`setData()` 处理 CheckStateRole） |
| **更新方式** | `beginInsertRows / endInsertRows` 增量更新 | `beginResetModel / endResetModel` 整体重建（`loadCategory` 时） |
| **canFetchMore 条件** | 未加载 *且* 非分类节点 | 仅检查未加载 |

### 6.2 设计考量

**ClassModel 的设计哲学**：
- 作为导航树，它的职责是**展示组织结构的层级关系**，让用户快速定位到感兴趣的"分类"。
- 不需要交互（勾选/编辑），因此 `flags()` 只返回 `ItemIsEnabled | ItemIsSelectable`。
- 构造时自动加载顶级部门，随后通过懒加载展开子部门。
- 分类节点在 `canFetchMore()` 中返回 false，禁止展开箭头——因为分类下的内容数据量可能很大，且应当由 InfoModel 管理。

**InfoModel 的设计哲学**：
- 作为选择树，它的职责是**展示某个分类下的内容结构**，让用户勾选需要的内容项。
- 需要交互（勾选），因此 `flags()` 额外添加 `ItemIsUserCheckable`，`setData()` 处理勾选状态变更。
- 初始状态为空（仅有虚拟根节点），通过 `loadCategory()` 手动加载指定分类的内容。
- `loadCategory()` 使用 `beginResetModel / endResetModel` 整体重建视图，因为这是**替换**整个内容集而非增量添加。

### 6.3 从"单元测试"角度看双模型设计

如果两个模型合二为一，model 需要同时处理：
- 两种节点类型的混合显示
- 部分视图有勾选框、部分没有
- 不同的加载策略

这将导致 `data()`、`flags()`、`canFetchMore()` 等函数中出现大量 `if (nodeType == ...)` 分支判断。将两类树的逻辑分离到两个 model 中，每个 model 的职责单一，代码更清晰、更易维护和测试。

---

## 7. 勾选联动机制

### 7.1 需求分析

内容树的勾选需要满足以下交互需求：
1. **勾选父节点 → 自动勾选所有子孙节点**（全选该分类下所有子项）
2. **取消父节点 → 自动取消所有子孙节点**（一键取消）
3. **子节点状态变化 → 自动更新祖先节点的半选状态**（部分子节点勾选时父节点显示半选）
4. **批量操作（selectAll/deselectAll）时避免递归爆炸**

### 7.2 setData — 入口点

当用户在 `QTreeView` 中点击勾选框时，视图自动调用 `setData(index, newState, Qt::CheckStateRole)`。

```cpp
bool InfoModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::CheckStateRole) return false;

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    Qt::CheckState state = static_cast<Qt::CheckState>(value.toInt());

    // 快速通道：批量更新时直接设状态 + 通知视图，不递归
    if (m_updatingCheckState) {
        item->setCheckState(state);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    m_updatingCheckState = true;
    item->setCheckState(state);
    setChildrenCheckState(item, state);       // 向下同步所有子孙
    updateAncestorCheckStates(item);           // 向上更新祖先半选
    m_updatingCheckState = false;

    emit dataChanged(index, index, {Qt::CheckStateRole});
    return true;
}
```

完整流程：
1. 用户点击勾选框 → 视图调用 `setData(index, newValue, Qt::CheckStateRole)`
2. 设置 `m_updatingCheckState = true` 防止递归
3. 更新当前节点的状态
4. `setChildrenCheckState()` 递归设置所有子孙节点
5. `updateAncestorCheckStates()` 向上更新祖先节点的半选状态
6. 恢复 `m_updatingCheckState = false`
7. 发射 `dataChanged` 通知视图刷新

### 7.3 防递归标志位 m_updatingCheckState

这是整个联动机制中最关键的设计。考虑以下场景：

```
用户勾选节点 A
  → setData(A, Checked, CheckStateRole)
    → setChildrenCheckState(A, Checked)
      → 对子节点 B 调用 B->setCheckState()
        → emit dataChanged(B, B, {CheckStateRole})
          → 视图收到 dataChanged 信号
            → 视图自动调用 setData(B, Checked, CheckStateRole)  <-- 递归！
              → setChildrenCheckState(B, Checked)
                → ... 无限递归 ...
```

为了防止这种情况，`setData()` 通过 `m_updatingCheckState` 标志区分"用户点击"和"程序内部设置"：

- **用户点击**（`m_updatingCheckState == false`）：走完整联动流程（向下同步 + 向上更新）
- **内部批量设置**（`m_updatingCheckState == true`）：快速通道，仅设置状态 + 发射信号，不走递归

这种设计模式称为 **"哨兵变量"** 或 **"重入保护"**，在 Qt 的事件驱动编程中非常常见（类似的场景还有 `QTextEdit` 的 `m_ignoreNextUpdate`、`QComboBox` 的 `m_ignoreCurrentChanged` 等）。

### 7.4 向下同步：setChildrenCheckState

```cpp
void InfoModel::setChildrenCheckState(TreeItem *parent, Qt::CheckState state)
{
    for (int i = 0; i < parent->childCount(); ++i) {
        TreeItem *child = parent->child(i);

        // 关键：未加载的节点也要同步状态
        if (!child->isLoaded()) {
            loadChildren(child);  // 先加载该节点下的所有内容
        }

        child->setCheckState(state);
        QModelIndex childIdx = indexFromItem(child);
        if (childIdx.isValid()) {
            emit dataChanged(childIdx, childIdx, {Qt::CheckStateRole});
        }

        setChildrenCheckState(child, state);  // 递归
    }
}
```

**为什么要 `loadChildren`？** 考虑一个场景：用户仅展开了顶层节点，没有展开子节点。如果用户勾选了顶层节点，期望的是所有子节点（包括未展开的）都被勾选。这个过程被 `setChildrenCheckState` 的 `loadChildren` 调用覆盖——先将未加载的子树从数据库拉到内存，再设置状态。

### 7.5 向上更新：updateAncestorCheckStates

```cpp
void InfoModel::updateAncestorCheckStates(TreeItem *child)
{
    TreeItem *ancestor = child->parent();
    while (ancestor && ancestor != m_rootItem) {
        int checkedCount = 0;
        int uncheckedCount = 0;

        // 统计该祖先所有直接子节点的勾选状态
        for (int i = 0; i < ancestor->childCount(); ++i) {
            TreeItem *sibling = ancestor->child(i);
            switch (sibling->checkState()) {
            case Qt::Checked:   checkedCount++;   break;
            case Qt::Unchecked: uncheckedCount++; break;
            default: break;  // PartiallyChecked 不计入
            }
        }

        // 三态判定
        Qt::CheckState newState;
        if (checkedCount == ancestor->childCount())
            newState = Qt::Checked;
        else if (uncheckedCount == ancestor->childCount())
            newState = Qt::Unchecked;
        else
            newState = Qt::PartiallyChecked;

        // 仅在状态变化时更新并通知（避免不必要的视图刷新）
        if (ancestor->checkState() != newState) {
            ancestor->setCheckState(newState);
            QModelIndex idx = indexFromItem(ancestor);
            if (idx.isValid())
                emit dataChanged(idx, idx, {Qt::CheckStateRole});
        }

        ancestor = ancestor->parent();  // 向上遍历
    }
}
```

**三态判定逻辑**：

| 子节点状态分布 | 祖先节点状态 |
|---------------|-------------|
| 全部 Checked | Checked |
| 全部 Unchecked | Unchecked |
| 混合（含 PartiallyChecked） | PartiallyChecked |
| 部分 Checked + 部分 Unchecked | PartiallyChecked |

**为什么只在状态变化时更新？** 这是一个性能优化。某些场景下（比如 selectAll 时），`setChildrenCheckState` 已经将所有节点的状态设为全选/全不选，`updateAncestorCheckStates` 向上遍历时会发现祖先的状态已经是目标状态，就跳过 `dataChanged` 发射，避免不必要的视图重绘。

### 7.6 selectAll 和 deselectAll

```cpp
void InfoModel::selectAll()
{
    // 第一步：递归加载所有层级
    std::function<void(TreeItem *)> expandAll = [&](TreeItem *item) {
        loadChildren(item);
        for (int i = 0; i < item->childCount(); ++i)
            expandAll(item->child(i));
    };
    expandAll(m_rootItem);

    // 第二步：批量设置状态（跳过递归联动）
    m_updatingCheckState = true;
    setChildrenCheckState(m_rootItem, Qt::Checked);
    m_updatingCheckState = false;
}
```

`selectAll()` 分两步执行：先递归 `loadChildren` 确保所有节点的子数据都已加载，再通过 `m_updatingCheckState` 哨兵走批量设置通道。如果不先 `expandAll`，`setChildrenCheckState` 虽然也会 `loadChildren`，但 `expandAll` 显式表达了意图——我们需要所有数据在内存中。

`deselectAll()` 不需要先 `expandAll`，因为未加载节点的默认状态就是 `Unchecked`，不需要显式设置。

### 7.7 联动算法完整流程图

```
用户勾选节点 X
    │
    ▼
setData(X, Checked, CheckStateRole)
    │
    ├── m_updatingCheckState == true?
    │      └── 是 → 快速通道：直接设状态，return
    │
    ├── m_updatingCheckState = true  (上锁)
    │
    ├── X->setCheckState(Checked)
    │
    ├── setChildrenCheckState(X, Checked)
    │      ├── 对 X 的每个子节点 C:
    │      │     ├── 若未加载则 loadChildren(C)
    │      │     ├── C->setCheckState(Checked)
    │      │     ├── emit dataChanged(C)
    │      │     └── setChildrenCheckState(C, Checked) ← 递归
    │      └── (注意：递归过程中 m_updatingCheckState 为 true)
    │
    ├── updateAncestorCheckStates(X)
    │      ├── P = X->parent()
    │      ├── 统计 P 的所有子节点状态
    │      ├── 计算 P 的新状态 (Checked/Unchecked/PartiallyChecked)
    │      ├── if (状态变化) → P->setCheckState() + emit dataChanged(P)
    │      ├── P = P->parent() → 继续向上
    │      └── 直到 m_rootItem
    │
    ├── m_updatingCheckState = false (解锁)
    │
    └── emit dataChanged(X)  (通知视图刷新 X)
```

---

## 8. 信号槽连接

`Tree` 构造函数中建立了 7 个信号槽连接，全部使用**新式 connect 语法**（函数指针方式），编译器在编译期进行类型检查，避免了老式 `SIGNAL/SLOT` 宏的运行时字符串匹配风险。

### 8.1 连接清单

```cpp
// (1) 左侧树点击 → 加载分类内容
connect(m_ui.treeView_Class, &QTreeView::clicked,
        this, &Tree::onCategoryClicked);

// (2) "增加到选用的信息" 按钮
connect(m_ui.pushButton_Add, &QPushButton::clicked,
        this, &Tree::onAdd);

// (3) "全选" 按钮
connect(m_ui.pushButton_AllSelect, &QPushButton::clicked,
        this, &Tree::onSelectAll);

// (4) "取消全选" 按钮
connect(m_ui.pushButton_CancelSelect, &QPushButton::clicked,
        this, &Tree::onDeselectAll);

// (5) "清空选用的信息" 按钮
connect(m_ui.pushButton_Reset, &QPushButton::clicked,
        this, &Tree::onReset);

// (6) "确定" 按钮
connect(m_ui.pushButton_OK, &QPushButton::clicked,
        this, &Tree::onOK);

// (7) "取消" 按钮
connect(m_ui.pushButton_Cancel, &QPushButton::clicked,
        this, &Tree::onCancel);
```

### 8.2 各槽函数职责说明

| 序号 | 信号源 | 槽函数 | 职责 |
|------|--------|--------|------|
| 1 | `treeView_Class::clicked` | `onCategoryClicked` | 点击左侧树的节点，判断为分类节点后加载 InfoModel 内容 |
| 2 | `pushButton_Add::clicked` | `onAdd` | 将右侧已勾选的内容格式化为 ASCII 树，追加到 textEdit |
| 3 | `pushButton_AllSelect::clicked` | `onSelectAll` | 调用 InfoModel 的 selectAll() |
| 4 | `pushButton_CancelSelect::clicked` | `onDeselectAll` | 调用 InfoModel 的 deselectAll() |
| 5 | `pushButton_Reset::clicked` | `onReset` | 清空 textEdit 内容 |
| 6 | `pushButton_OK::clicked` | `onOK` | 通过日志系统输出 textEdit 内容到控制台 |
| 7 | `pushButton_Cancel::clicked` | `onCancel` | 关闭当前窗口 |

### 8.3 onCategoryClicked — 左侧到右侧的桥梁

```cpp
void Tree::onCategoryClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    if (!item || item->nodeType() != NODE_TYPE_CATEGORY) return;

    // 加载分类下的内容树
    m_infoModel->loadCategory(item->id(), item->name());

    // 全部展开（触发链式懒加载）
    m_ui.treeView_Info->expandAll();
}
```

这个方法是连接左右两个 QTreeView 的枢纽：左侧树中的"分类"节点被点击时，右侧树加载对应的内容。注意它过滤了非分类节点（部门节点点击不产生任何效果），这符合"左侧导航、右侧详情"的交互模式。

### 8.4 formatCheckedTree — 纯 Model API 遍历

`formatCheckedTree()` 是一个值得学习的例子——它**完全不访问 model 内部的私有数据**，只通过 `QAbstractItemModel` 的公有 API（`index()`、`data()`、`rowCount()`）遍历树：

```cpp
std::function<QString(const QModelIndex &, const QString &, bool)> formatNode;
formatNode = [&](const QModelIndex &idx, const QString &indent, bool isLast) -> QString {
    QString name = m_infoModel->data(idx, Qt::DisplayRole).toString();
    QString branch = isLast ? "└── " : "├── ";
    QString line = indent + branch + name + "\n";

    // 收集勾选的子节点 → 通过公有 API
    int rows = m_infoModel->rowCount(idx);
    QList<QModelIndex> checkedChildren;
    for (int i = 0; i < rows; ++i) {
        QModelIndex childIdx = m_infoModel->index(i, 0, idx);
        Qt::CheckState state = m_infoModel->data(childIdx, Qt::CheckStateRole)
                                .value<Qt::CheckState>();
        if (state != Qt::Unchecked)
            checkedChildren.append(childIdx);
    }

    // 递归格式化子节点
    QString childIndent = indent + (isLast ? "    " : "│   ");
    for (int i = 0; i < checkedChildren.size(); ++i)
        line += formatNode(checkedChildren[i], childIndent, i == checkedChildren.size() - 1);

    return line;
};
```

这种做法符合 Qt Model/View 架构的最佳实践——**View 层不应该知道 Model 的数据内部结构**，通过标准 API 交互可以保证：即使未来 Model 的实现完全重写（只要保持公有 API 不变），View 层的代码无需修改。

### 8.5 appendToTextEdit — 同一分类去重替换

```cpp
void Tree::appendToTextEdit(const QString &categoryName, const QString &treeText)
{
    QString fullText = m_ui.textEdit->toPlainText();
    QString header = "[" + categoryName + "]\n";

    // 查找旧块边界并删除
    int headerPos = fullText.indexOf(header);
    if (headerPos >= 0) {
        int blockEnd = fullText.indexOf("\n[", headerPos + header.length());
        if (blockEnd < 0) blockEnd = fullText.length();
        fullText.remove(headerPos, blockEnd - headerPos);
    }

    // 追加新块
    fullText += "\n" + header + treeText;
    m_ui.textEdit->setPlainText(fullText);
}
```

设计思路：textEdit 中的内容以 `[分类名]` 为块的标记头。同一分类多次"增加到选用的信息"时，会用新内容替换旧内容而非追加。这样可以保持 textEdit 的内容整洁，每个分类只有最新一份数据。

---

## 9. 数据库层

### 9.1 DataManager 单例模式

```cpp
class DataManager {
public:
    static DataManager &instance() {
        static DataManager inst;  // C++11 保证线程安全的局部静态变量
        return inst;
    }

private:
    DataManager() = default;
    DataManager(const DataManager &) = delete;
    DataManager &operator=(const DataManager &) = delete;
};
```

使用 **Meyer's Singleton**（C++11 局部静态变量）：`static DataManager inst` 在首次调用 `instance()` 时初始化，C++11 标准保证初始化过程的线程安全性。构造函数私有化、拷贝构造和赋值操作被删除，确保全局只有一个实例。

### 9.2 ScopedConn RAII 用法

所有数据库操作都通过 `ScopedConn` 获取连接：

```cpp
QList<Node> DataManager::queryChildren(qlonglong parentId, const QList<int> &nodeTypes)
{
    QList<Node> list;
    try {
        ScopedConn conn;  // 构造：从连接池获取连接
        QSqlQuery query = conn->prepare(sql);
        // ... 绑参、执行 ...
        while (query.next()) { /* 读取数据 */ }
    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "queryChildren failed: " << e.what();
    }  // conn 析构：自动归还连接到池
    return list;
}
```

`ScopedConn` 构造时自动调用 `DBPool::instance().acquire()` 获取连接，析构时自动调用 `DBPool::instance().release()` 归还。这种 RAII 模式确保了：
- 即使在异常路径上，连接也会被正确归还
- 无需手动管理连接的获取和释放
- 函数返回时，连接自动归还到池中

### 9.3 预处理查询防 SQL 注入

```cpp
QSqlQuery query = conn->prepare(
    "INSERT INTO org_tree(name, node_type, parent_id) VALUES(?,?,?)"
);
conn->execPrepared(query, {name, nodeType, parentId});
```

使用 `?` 占位符和参数绑定替代字符串拼接，有效防止 SQL 注入攻击。`DataManager` 中的所有写操作（insert、update、delete）都使用预处理查询。

### 9.4 org_tree 表结构

```sql
CREATE TABLE org_tree (
    id         BIGINT AUTO_INCREMENT PRIMARY KEY,
    name       VARCHAR(255) NOT NULL,
    node_type  TINYINT      NOT NULL,    -- 0=部门, 1=分类, 2=内容
    parent_id  BIGINT       NULL,        -- 父节点 ID, NULL=根节点
    sort_order INT          DEFAULT 0,   -- 同级排序

    INDEX idx_parent (parent_id),
    INDEX idx_type   (node_type),

    CONSTRAINT fk_org_parent
        FOREIGN KEY (parent_id) REFERENCES org_tree(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

**表设计要点**：

- **自引用外键**：`parent_id` 引用同一张表的 `id`，使用 `ON DELETE CASCADE` 确保删除父节点时自动删除子孙节点。但 `DataManager::deleteNode()` 并没有依赖这个级联删除，而是手动实现了广度优先遍历 + 逆序删除（见下文"删除算法"）。
- **`parent_id` 可为 NULL**：根节点的 parent_id 存 NULL，而非 0。这影响 `queryChildren()` 中的查询条件——`parentId == 0` 时使用 `WHERE parent_id IS NULL` 而非 `WHERE parent_id = 0`。
- **索引策略**：`idx_parent` 大幅加速"根据父节点查询子节点"的操作（这是最频繁的查询）；`idx_type` 用于按类型过滤。
- **node_type 使用 TINYINT**：仅需要 3 个值（0/1/2），TINYINT 比 INT 节省 3 字节。

### 9.5 删除算法 — 广度优先 + 逆序删除

```cpp
bool DataManager::deleteNode(qlonglong id)
{
    ScopedConn conn;
    QList<qlonglong> idsToDelete;
    QList<qlonglong> pending = {id};

    // 第一轮：BFS 收集所有子孙节点
    while (!pending.isEmpty()) {
        qlonglong currentId = pending.takeFirst();
        idsToDelete.append(currentId);

        QSqlQuery childQuery = conn->prepare("SELECT id FROM org_tree WHERE parent_id=?");
        conn->execPrepared(childQuery, {currentId});
        while (childQuery.next())
            pending.append(childQuery.value(0).toLongLong());
    }

    // 第二轮：逆序删除（叶子 → 根）
    for (int i = idsToDelete.size() - 1; i >= 0; --i)
        conn->execPrepared("DELETE FROM org_tree WHERE id=?", {idsToDelete[i]});

    return true;
}
```

**为什么手动实现而非依赖 ON DELETE CASCADE？** 代码注释没有明确说明原因，但从架构角度看，手动实现有两点好处：
1. 控制删除顺序和可见性：在单条 DELETE 语句中可以更精确地控制，适合批量删除时做日志或审计。
2. MySQL `ON DELETE CASCADE` 是在存储引擎层面隐式执行的，开发者看不到触发过程。手动 BFS + 逆序删除让代码的删除逻辑显式化。

### 9.6 queryChildren 的动态 SQL 构建

```cpp
QString sql = (parentId == 0)
    ? "SELECT id, name, node_type, sort_order FROM org_tree WHERE parent_id IS NULL"
    : "SELECT id, name, node_type, sort_order FROM org_tree WHERE parent_id=?";

if (!nodeTypes.isEmpty()) {
    QStringList placeholders;
    for (int i = 0; i < nodeTypes.size(); ++i)
        placeholders << "?";
    sql += " AND node_type IN (" + placeholders.join(",") + ")";
}
sql += " ORDER BY sort_order, id";

QSqlQuery query = conn->prepare(sql);

if (parentId != 0)
    query.addBindValue(parentId);
for (int t : nodeTypes)
    query.addBindValue(t);
```

代码注释特别说明了这里的权衡：虽然运行时拼接 SQL 字符串看起来不如预编译多个 SQL 模板分支"优雅"，但在 `parentId` 是否为 0 和 `nodeTypes` 是否为空这两个参数的组合下，拼接条件字符串比预编译 4 个模板分支更清晰易维护。这是一个**实用主义优于完美主义**的设计选择。

---

## 10. 关键代码片段赏析

### 10.1 虚拟根节点模式

两个 Model 都使用了一个不可见的 `m_rootItem` 作为所有顶级节点的父节点：

```cpp
// ClassModel 构造
ClassModel::ClassModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_rootItem(nullptr)
{
    m_rootItem = new TreeItem(0, "", NODE_TYPE_DEPT, nullptr);
    auto list = DataManager::instance().queryChildren(0, {NODE_TYPE_DEPT});
    for (auto &dto : list) {
        auto *child = new TreeItem(dto.id, dto.name, dto.nodeType, m_rootItem);
        m_rootItem->appendChild(child);
    }
    m_rootItem->setLoaded(true);
}
```

**设计意图**：虚拟根节点统一了顶级节点和非顶级节点的处理逻辑。在 `index()`、`rowCount()`、`canFetchMore()` 等方法中，当 `parent` 索引无效时，统一使用 `m_rootItem` 作为父节点——无论请求的是第一级还是第 N 级子节点，代码逻辑完全一致。

### 10.2 indexFromItem 的对称设计

```cpp
QModelIndex ClassModel::indexFromItem(TreeItem *item) const
{
    if (item == m_rootItem) return QModelIndex();
    return createIndex(item->row(), 0, item);
}
```

这个辅助方法在两个 Model 中重复出现（代码复用？注意这里是各有一个，不是共享），按照 DRY 原则，理论上可以提取到 TreeItem 层或一个工具函数中。但在目前的结构中，两者对 `indexFromItem` 的实现完全一致，这属于**可容忍的重复**——提取可能导致过度工程化（增加不必要的抽象层），且两个 Model 未来可能有不同的 `indexFromItem` 需求。

### 10.3 canFetchMore 中分类节点的特殊处理

```cpp
bool ClassModel::canFetchMore(const QModelIndex &parent) const
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    if (item->nodeType() == NODE_TYPE_CATEGORY) {
        return false;  // 分类节点不在左侧树展开
    }
    return !item->isLoaded();
}
```

**设计意图**：分类节点的"内容"子节点属于 InfoModel 的管辖范围，不应在 ClassModel 中展示。因此 `canFetchMore` 对分类节点返回 false，让左侧树在分类节点处停止展开，防止用户误以为分类下还有可展开的部门层级。

### 10.4 deleteNode 的 BFS + 逆序删除

```cpp
// BFS 收集所有子孙节点
QList<qlonglong> pending = {id};
while (!pending.isEmpty()) {
    qlonglong currentId = pending.takeFirst();
    idsToDelete.append(currentId);
    // ... 查询子节点 ...
}

// 逆序删除（叶子 → 根）
for (int i = idsToDelete.size() - 1; i >= 0; --i) {
    conn->execPrepared("DELETE FROM org_tree WHERE id=?", {idsToDelete[i]});
}
```

**设计价值**：BFS（广度优先遍历）收集的特点是：父节点永远在子节点之前加入 `idsToDelete`（因为查询 `parent_id=?` 得到子节点是在处理父节点时追加的）。因此 `idsToDelete` 的顺序是"祖先在前、子孙在后"。逆序删除确保了在删除父节点之前，所有子节点都已被删除，完全规避了外键约束冲突。

### 10.5 formatCheckedTree 的纯公有 API 遍历

```cpp
// 仅通过 QAbstractItemModel 公有 API
QString name = m_infoModel->data(idx, Qt::DisplayRole).toString();
int rows = m_infoModel->rowCount(idx);
QModelIndex childIdx = m_infoModel->index(i, 0, idx);
Qt::CheckState state = m_infoModel->data(childIdx, Qt::CheckStateRole)
                        .value<Qt::CheckState>();
```

这段代码刻意避免了直接访问 `TreeItem` 内部指针——虽然 Tree 类确实包含 InfoModel 的指针，完全可以访问到 `TreeItem`，但这种"通过公有 API 访问"的自我约束是良好的架构实践。它确保了 View 层的渲染逻辑不依赖 Model 的实现细节，使得 Model 内部的优化和重构不会影响 View 层的功能。

### 10.6 selectAll 的分步设计

```cpp
void InfoModel::selectAll()
{
    // 第一步：递归展开所有层级
    std::function<void(TreeItem *)> expandAll = [&](TreeItem *item) {
        loadChildren(item);
        for (int i = 0; i < item->childCount(); ++i)
            expandAll(item->child(i));
    };
    expandAll(m_rootItem);

    // 第二步：批量勾选
    m_updatingCheckState = true;
    setChildrenCheckState(m_rootItem, Qt::Checked);
    m_updatingCheckState = false;
}
```

分两步而非一步的原因：`setChildrenCheckState` 虽然会 `loadChildren` 未加载的节点，但将其职责限定在"同步状态"的范围内。`expandAll` 显式地表达了"我需要全部数据"的意图，使得 `selectAll` 的代码可读性更强——任何阅读代码的人都能立即理解这是"先展后选"的流程。

### 10.7 InfoModel::data 的 CheckStateRole 支持

```cpp
QVariant InfoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    if (item == m_rootItem) return QVariant();

    if (role == Qt::DisplayRole)       return item->name();
    if (role == Qt::CheckStateRole)    return item->checkState();
    return QVariant();
}
```

与 ClassModel 的 `data()` 对比，差异在于 `InfoModel` 增加了 `Qt::CheckStateRole` 的处理。当 `QTreeView` 渲染一个带 `Qt::ItemIsUserCheckable` 标志的节点时，它会调用 `data(Qt::CheckStateRole)` 获取勾选框的初始状态。这个初始值来自 `TreeItem::m_checkState`，其默认值在构造时被设为 `Qt::Unchecked`。

---

## 附录 A：常见问题 FAQ

### Q1: 为什么 ClassModel 构造时就加载了顶级部门，而 InfoModel 需要 loadCategory？

ClassModel 的用途是导航，需要在窗口打开时就展示组织架构的顶层，用户才能开始操作。InfoModel 的内容是被动加载的——只有用户点击了具体的分类，才知道要展示什么内容。

### Q2: 为什么 InfoModel 使用 beginResetModel/endResetModel 而 ClassModel 使用 beginInsertRows/endInsertRows？

InfoModel 的 `loadCategory()` 是用全新的内容树替换旧的内容树——这是**整体替换**，使用 reset 模式。ClassModel 的 `loadChildren()` 是在现有父节点下**增量添加**子节点（而不是替换整个树），使用 inserRows 模式。两种模式对应不同的数据变更语义。

### Q3: 为什么 `hasChildren` 在未加载时要查数据库？

因为 `QTreeView` 在渲染时就需要知道每个节点是否有展开箭头，而数据可能还未加载。如果所有未加载节点都返回 `true`（"可能有"），分类节点也会显示展开箭头，用户点击后才发现无数据可加载，体验不好。因此需要查数据库准确判断。

### Q4: 为什么选择两个独立 Model 而非一个通用 Model？

详细分析见第 6 章"双模型设计"。简要总结：一个 Model 同时处理有勾选框和无勾选框的节点、不同的加载策略、不同的节点类型，会导致大量 if-else 分支。两个 Model 各自职责单一，更清晰、易测试、易维护。

### Q5: 删除节点时为什么不用 ON DELETE CASCADE？

项目确实在表定义中添加了 `ON DELETE CASCADE`，但 `deleteNode()` 仍然手动实现了 BFS + 逆序删除。这种"显式优于隐式"的选择让删除逻辑完全可控：可以添加日志、可以控制删除范围、不依赖 MySQL 引擎层面的行为。如果未来切换数据库引擎（如 SQLite 可能不支持级联删除），代码无需改动。

---

## 附录 B：文件清单

| 文件 | 说明 |
|------|------|
| `TreeExplorer/tree/Tree.h` | 头文件：Node、TreeItem、DataManager、ClassModel、InfoModel、Tree 声明 |
| `TreeExplorer/tree/Tree.cpp` | 实现文件：所有类的完整实现（约 1338 行） |
| `TreeExplorer/tree/Tree.ui` | Qt Designer UI 布局文件：双 QTreeView + 6 个按钮 + QTextEdit |
| `QTLearnCommon/db/QDBConn.h` | 数据库连接封装：DBException、DBConn、DBPool、ScopedConn、DBTransaction |
| `sql/org_tree_init.sql` | MySQL 建表 + 测试数据初始化脚本 |

---

*文档版本：基于代码提交 c8a0c24 撰写*
*最后更新：2026-06-28*
