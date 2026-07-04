# QTreeView Item 复制功能实现原理

## 概述

本文档详细解释 TreeExplorer 项目中为 `QTreeView` 树形控件实现的复制功能，包含两种复制方式：

| 方式 | 触发操作 | 复制内容 |
|------|----------|----------|
| 整项复制 | 右键菜单 "复制" / Ctrl+C | 当前选中 item 的完整名称 |
| 部分文字复制 | 双击 item 进入编辑模式 → 拖选文字 → Ctrl+C | 用户拖选的部分文字 |

---

## 1. 背景知识：QTreeView 的交互模型

### 1.1 QTreeView 的选中机制

`QTreeView` 的选中是**行级**的——你只能选中整行，不能像文本编辑器那样选中某几个字。这是因为它内部通过 `QItemSelectionModel` 管理选中状态，选中粒度是 `QModelIndex`（一个 item），不是字符位置。

```
┌──────────────────────────────┐
│  📁 技术部                    │  ← 点击后整行变蓝，这是"行选中"
│  📁 产品部                    │
│  📄 需求文档                  │
└──────────────────────────────┘
```

### 1.2 QTreeView 的编辑机制

`QTreeView` 内置了一套**编辑框架**：

1. **Model 声明可编辑**：`flags()` 返回 `Qt::ItemIsEditable`
2. **View 决定何时触发编辑**：`editTriggers` 属性控制
3. **Delegate 创建编辑器**：`createEditor()` 返回一个临时控件覆盖在 item 文字上
4. **Delegate 读写数据**：`setEditorData()` 从 Model 读数据填入编辑器，`setModelData()` 把编辑结果写回 Model

```
用户双击 item
    │
    ▼
QTreeView 检测到 editTriggers 条件满足
    │
    ▼
调用 Delegate::createEditor()  →  创建一个 QLineEdit 覆盖在文字上
    │
    ▼
调用 Delegate::setEditorData() →  把 Model 中的文字填入 QLineEdit
    │
    ▼
用户在 QLineEdit 中拖选文字、Ctrl+C 复制  ←── 我们要的就是这个效果
    │
    ▼
用户点击其他地方，编辑器失去焦点
    │
    ▼
调用 Delegate::setModelData()  →  把 QLineEdit 的文字写回 Model
    │
    ▼
编辑器销毁，QTreeView 恢复普通显示
```

我们的核心思路就是**借用这套编辑框架，但把编辑器设为只读**，这样用户可以在编辑器中拖选文字复制，却无法修改内容。

---

## 2. 实现架构

### 2.1 总览

```
┌─────────────────────────────────────────────────────────────┐
│                        Tree (主窗口)                         │
│                                                             │
│  ┌─────────────────────┐    ┌─────────────────────┐         │
│  │   treeView_Class    │    │   treeView_Info     │         │
│  │   (部门分类树)       │    │   (内容树)           │         │
│  │                     │    │                     │         │
│  │  editTriggers:      │    │  editTriggers:      │         │
│  │  DoubleClicked      │    │  DoubleClicked      │         │
│  │                     │    │                     │         │
│  │  itemDelegate:      │    │  itemDelegate:      │         │
│  │  ReadOnlyEditDelegate│   │  ReadOnlyEditDelegate│        │
│  │  (同一个实例)        │    │  (同一个实例)        │         │
│  │                     │    │                     │         │
│  │  contextMenuPolicy: │    │  contextMenuPolicy: │         │
│  │  CustomContextMenu  │    │  CustomContextMenu  │         │
│  └─────────┬───────────┘    └─────────┬───────────┘         │
│            │ model                     │ model               │
│  ┌─────────▼───────────┐    ┌─────────▼───────────┐         │
│  │     ClassModel      │    │     InfoModel       │         │
│  │  flags():           │    │  flags():           │         │
│  │  + ItemIsEditable   │    │  + ItemIsEditable   │         │
│  │  data():            │    │  data():            │         │
│  │  EditRole → name    │    │  EditRole → name    │         │
│  │  setData():         │    │  setData():         │         │
│  │  return false       │    │  EditRole→false     │         │
│  └─────────────────────┘    └─────────────────────┘         │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  m_copyAction (QAction, Ctrl+C 快捷键)               │   │
│  │  → onCopyItem() → copyFromTreeView() → clipboard     │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  onShowContextMenu() → 右键菜单 "复制"                │   │
│  │  → copyFromTreeView() → clipboard                    │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 涉及的文件和类

| 文件 | 类/方法 | 角色 |
|------|---------|------|
| `Tree.h` | `ReadOnlyEditDelegate` | 自定义代理，创建只读编辑器 |
| `Tree.h` | `Tree` | 主窗口，组装所有组件 |
| `Tree.cpp` | `ReadOnlyEditDelegate::createEditor()` | 创建只读 QLineEdit |
| `Tree.cpp` | `ReadOnlyEditDelegate::setModelData()` | 阻止数据写回（空实现） |
| `Tree.cpp` | `ClassModel::data()` | 提供 EditRole 数据 |
| `Tree.cpp` | `ClassModel::flags()` | 声明 ItemIsEditable |
| `Tree.cpp` | `ClassModel::setData()` | 拒绝所有编辑 |
| `Tree.cpp` | `InfoModel::data()` | 提供 EditRole 数据 |
| `Tree.cpp` | `InfoModel::flags()` | 声明 ItemIsEditable |
| `Tree.cpp` | `InfoModel::setData()` | 拒绝 EditRole 编辑 |
| `Tree.cpp` | `Tree::onCopyItem()` | Ctrl+C 响应 |
| `Tree.cpp` | `Tree::onShowContextMenu()` | 右键菜单响应 |
| `Tree.cpp` | `Tree::copyFromTreeView()` | 实际剪贴板操作 |

---

## 3. 逐层详解

### 3.1 第一层：Model 宣告 "我可以被编辑"

要让 QTreeView 允许对某个 item 触发编辑模式，Model 的 `flags()` 必须返回 `Qt::ItemIsEditable`。

```cpp
// ClassModel::flags() — 部门分类树
Qt::ItemFlags ClassModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled          // 可交互
         | Qt::ItemIsSelectable       // 可选中
         | Qt::ItemIsEditable;        // 可编辑 ← 新增
}

// InfoModel::flags() — 内容树
Qt::ItemFlags InfoModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled
         | Qt::ItemIsSelectable
         | Qt::ItemIsUserCheckable    // 可勾选（内容树特有）
         | Qt::ItemIsEditable;        // 可编辑 ← 新增
}
```

**关键理解**：`Qt::ItemIsEditable` 只是一个"许可标志"，告诉 View "这个 item 允许进入编辑模式"，但**实际能不能修改数据**还要看 `setData()` 是否真的接受修改。

### 3.2 第二层：Model 提供编辑时显示的文字

进入编辑模式时，Delegate 会调用 `model->data(index, Qt::EditRole)` 来获取初始文字。Model 的 `data()` 需要处理 `Qt::EditRole`：

```cpp
// 两种 Model 都做了同样的处理：
if (role == Qt::DisplayRole || role == Qt::EditRole) {
    return item->name();   // EditRole 和 DisplayRole 返回同样的文字
}
```

**关键理解**：`DisplayRole` 是普通显示时的文字，`EditRole` 是编辑时的初始文字。通常它们相同，但也可以不同（比如日期格式化显示 vs 原始数值编辑）。

### 3.3 第三层：Model 拒绝实际修改

`Qt::ItemIsEditable` 让 View 允许进入编辑模式，但如果我们不做额外处理，用户在编辑器中修改文字并确认后，修改会被写回 Model。我们要**阻止写回**：

```cpp
// ClassModel::setData() — 新增的方法，拒绝所有编辑
bool ClassModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_UNUSED(index);
    Q_UNUSED(value);
    Q_UNUSED(role);
    return false;   // 拒绝一切修改
}

// InfoModel::setData() — 已有方法，本就拒绝 EditRole
bool InfoModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::CheckStateRole) {
        return false;   // 只有勾选状态可以修改，EditRole 被拒绝
    }
    // ... 勾选联动逻辑 ...
}
```

**关键理解**：`setData()` 返回 `false` 意味着"这个修改不被接受"。但即使返回 `false`，编辑器的文字也不会被强制还原——因为我们的编辑器是只读的，文字本来就没变。

### 3.4 第四层：Delegate 创建只读编辑器（核心）

这是整个方案最关键的部分。当 View 决定进入编辑模式时，它调用 Delegate 的 `createEditor()` 来创建编辑器控件。

```cpp
QWidget *ReadOnlyEditDelegate::createEditor(QWidget *parent,
                                            const QStyleOptionViewItem &,
                                            const QModelIndex &) const
{
    QLineEdit *editor = new QLineEdit(parent);

    // 核心：设为只读
    // - 用户可以用鼠标拖选文字
    // - 用户可以用 Ctrl+C / 右键复制
    // - 用户不能输入、删除、修改文字
    editor->setReadOnly(true);

    // 样式：蓝色边框，让用户知道进入了"文字选择模式"
    editor->setStyleSheet(
        "QLineEdit {"
        "  background: white;"
        "  border: 1px solid #3399FF;"
        "  border-radius: 2px;"
        "  padding: 1px 3px;"
        "}"
    );

    return editor;
}
```

**`setReadOnly(true)` 的效果**：

```
只读 QLineEdit 中可以做的事：
  ✅ 鼠标拖选文字
  ✅ Ctrl+C 复制选中文字
  ✅ 右键 → 复制
  ✅ Ctrl+A 全选
  ✅ 键盘方向键移动光标（配合 Shift 选中）

只读 QLineEdit 中不能做的事：
  ❌ 输入新文字
  ❌ Delete / Backspace 删除
  ❌ Ctrl+V 粘贴
  ❌ Ctrl+X 剪切
```

同时，我们重写 `setModelData()` 为空实现，双重保险：

```cpp
void ReadOnlyEditDelegate::setModelData(QWidget *, QAbstractItemModel *, const QModelIndex &) const
{
    // 空实现：即使编辑器不是只读的，也不会把数据写回 Model
}
```

**关键理解**：`QStyledItemDelegate` 的默认 `setEditorData()` 和 `setModelData()` 会自动从 Model 读取和写回数据。我们只重写了 `setModelData()` 来阻止写回，`setEditorData()` 保持默认行为（自动从 `Qt::EditRole` 读取数据填入编辑器）。

### 3.5 第五层：View 设置触发时机

View 的 `editTriggers` 属性决定什么操作会触发编辑模式：

```cpp
// 设置为双击触发
m_ui.treeView_Class->setEditTriggers(QAbstractItemView::DoubleClicked);
m_ui.treeView_Info->setEditTriggers(QAbstractItemView::DoubleClicked);
```

常见的触发方式：

| 枚举值 | 触发条件 | 适用场景 |
|--------|----------|----------|
| `NoEditTriggers` | 永远不进入编辑模式 | 只读列表 |
| `DoubleClicked` | 双击 item | 当前项目使用 |
| `SelectedClicked` | 单击已选中的 item | 资源管理器重命名 |
| `EditKeyPressed` | 按 F2 键 | 专业软件常用 |
| `CurrentChanged` | 切换选中项即进入编辑 | 内联编辑表格 |

### 3.6 第六层：右键菜单复制（整项名称）

除了编辑模式下的部分文字复制，还保留了右键菜单复制整项名称的功能：

```cpp
void Tree::onShowContextMenu(const QPoint &pos)
{
    // 1. 通过 sender() 判断是哪个 QTreeView 触发的信号
    QTreeView *treeView = qobject_cast<QTreeView *>(sender());

    // 2. indexAt(pos) 获取右键点击位置的 item
    QModelIndex index = treeView->indexAt(pos);
    if (!index.isValid()) return;   // 空白区域右键不弹菜单

    // 3. 先选中该 item
    treeView->setCurrentIndex(index);

    // 4. 弹出菜单
    QMenu menu(this);
    QAction *act = menu.addAction("复制");

    // 5. Lambda 捕获 treeView 指针，确保复制正确的视图
    connect(act, &QAction::triggered, this, [this, treeView]() {
        copyFromTreeView(treeView);
    });

    menu.exec(treeView->viewport()->mapToGlobal(pos));
}
```

```cpp
void Tree::copyFromTreeView(QTreeView *treeView)
{
    // 通过 Model 的公有 API 获取文字，不依赖具体 Model 类型
    QModelIndex index = treeView->currentIndex();
    QString text = treeView->model()->data(index, Qt::DisplayRole).toString();

    // 写入系统剪贴板
    QApplication::clipboard()->setText(text);
}
```

### 3.7 第七层：Ctrl+C 快捷键

```cpp
// 构造函数中：
m_copyAction = new QAction("复制", this);
m_copyAction->setShortcut(QKeySequence::Copy);   // 即 Ctrl+C

// addAction 将 Action 注册到 QTreeView 上，
// 只有当该 QTreeView 有焦点时快捷键才生效
m_ui.treeView_Class->addAction(m_copyAction);
m_ui.treeView_Info->addAction(m_copyAction);

connect(m_copyAction, &QAction::triggered, this, &Tree::onCopyItem);
```

**关键理解**：`QWidget::addAction()` 将 Action 注册到特定控件。快捷键只在控件有焦点时生效。如果两个 treeView 都没有焦点，Ctrl+C 不会被触发。如果在编辑模式下的 QLineEdit 中按 Ctrl+C，QLineEdit 自身处理该快捷键（复制选中文字），我们的 Action 不会收到。

---

## 4. 完整交互流程

### 4.1 场景一：双击拖选部分文字复制

```
1. 用户看到 treeView_Class 中的 item "技术研发部"

2. 用户双击 "技术研发部"
   ├── QTreeView 检测到 DoubleClicked 事件
   ├── 检查 flags() → 有 ItemIsEditable → 允许进入编辑
   ├── 调用 delegate->createEditor()
   │   └── 创建只读 QLineEdit，文字为 "技术研发部"
   ├── 调用 delegate->setEditorData()
   │   └── 调用 model->data(index, EditRole) 获取 "技术研发部"
   │   └── 设置到 QLineEdit 中
   └── QLineEdit 覆盖在 item 文字上，蓝色边框可见

3. 用户在 QLineEdit 中拖选 "研发" 两个字
   └── "研发" 两字反色高亮

4. 用户按 Ctrl+C
   └── QLineEdit 的内置行为：复制选中的文字 "研发" 到剪贴板

5. 用户点击其他 item
   ├── QLineEdit 失去焦点
   ├── 调用 delegate->setModelData() → 空实现，不写回
   └── QLineEdit 销毁，视图恢复普通显示
```

### 4.2 场景二：右键菜单复制整项名称

```
1. 用户右键点击 "技术研发部"
   ├── customContextMenuRequested 信号发射
   ├── onShowContextMenu() 被调用
   ├── indexAt(pos) 获取到 "技术研发部" 对应的 QModelIndex
   └── 弹出菜单，显示 "复制"

2. 用户点击 "复制"
   ├── Lambda 被调用
   ├── copyFromTreeView(treeView_Class)
   ├── model->data(index, DisplayRole) → "技术研发部"
   └── clipboard->setText("技术研发部")
```

### 4.3 场景三：选中后 Ctrl+C

```
1. 用户单击选中 "技术研发部"（整行变蓝）

2. treeView_Class 有焦点

3. 用户按 Ctrl+C
   ├── m_copyAction 的 shortcut 被触发
   ├── onCopyItem() 被调用
   ├── hasFocus() 检查：treeView_Class 有焦点
   └── copyFromTreeView(treeView_Class) → 剪贴板
```

---

## 5. 关键设计决策

### 5.1 为什么用 QLineEdit 而不是其他控件？

| 控件 | 文字选择 | 只读模式 | 复制支持 | 外观 |
|------|----------|----------|----------|------|
| QLineEdit | ✅ 原生支持 | ✅ setReadOnly(true) | ✅ Ctrl+C | 单行，完美匹配 |
| QTextEdit | ✅ 支持 | ✅ | ✅ | 多行，太高 |
| QLabel | ❌ 不支持拖选 | - | ❌ | - |
| 自绘 | ❌ 需手动实现 | - | ❌ | 复杂 |

### 5.2 为什么是 Delegate 而不是其他方案？

| 方案 | 优点 | 缺点 |
|------|------|------|
| **QStyledItemDelegate（当前方案）** | 标准 Qt 机制，代码量少，与现有架构兼容 | 需要 Model 配合 |
| 子类化 QTreeView 重写 mousePressEvent | 完全控制 | 需手动实现文字选择逻辑，极其复杂 |
| QLabel + setTextInteractionFlags | 简单 | QTreeView 的 item 不是独立 widget，无法直接放 QLabel |
| 放弃 QTreeView 改用 QListWidget | 天然的 widget 操作 | 放弃 MVC 架构，改动巨大 |

### 5.3 为什么 setData 返回 false 而不是接受修改？

因为我们的目标是**只复制、不修改**。如果 `setData()` 返回 `true`，用户修改文字后失去焦点时，修改会写回 Model（虽然 QLineEdit 是只读的，但万一有其他代码路径触发写回呢）。返回 `false` 提供了安全保障。

---

## 6. 文件变更清单

| 文件 | 新增行数 | 关键变更 |
|------|----------|----------|
| `Tree.h` | ~40 行 | `ReadOnlyEditDelegate` 类声明、ClassModel `setData()` 声明、include |
| `Tree.cpp` | ~90 行 | Delegate 实现、Model 的 EditRole/flags/setData 修改、View 的 editTriggers/delegate 设置、右键菜单/Ctrl+C 实现 |

---

## 7. Qt 机制回顾

整个方案依赖的 Qt 核心机制：

```
┌──────────────────────────────────────────────────────────┐
│                    Model/View 架构                        │
│                                                          │
│   Model                         View                     │
│  ┌──────────┐    data()       ┌──────────────┐          │
│  │ 数据存储  │ ←───────────── │  显示渲染     │          │
│  │ flags()  │    setData()   │              │          │
│  │ 权限声明  │ ─────────────→ │  用户交互     │          │
│  └──────────┘                │              │          │
│                               │  editTriggers│          │
│  ┌──────────┐  createEditor()│  触发编辑     │          │
│  │ Delegate │ ←──────────────│              │          │
│  │ 编辑器    │  setModelData()│              │          │
│  │ 数据搬运  │ ─────────────→ │              │          │
│  └──────────┘                └──────────────┘          │
│                                                          │
│  Model 负责：数据 + 权限                                  │
│  View  负责：显示 + 交互时机                              │
│  Delegate 负责：编辑器创建 + 数据搬运                      │
└──────────────────────────────────────────────────────────┘
```
