#pragma once

#include <QtWidgets/QWidget>
#include <QAbstractItemModel>
#include <QIcon>

#include "ui_Tree.h"

// 节点类型常量
constexpr int NODE_TYPE_DEPT    = 0;  ///< 部门节点
constexpr int NODE_TYPE_CATEGORY = 1;  ///< 分类节点
constexpr int NODE_TYPE_CONTENT = 2;  ///< 内容节点

// ============================================================
// Node — 数据库查询结果 DTO
// ============================================================
struct Node {
    qlonglong id;
    QString name;
    int nodeType;    // NODE_TYPE_DEPT / NODE_TYPE_CATEGORY / NODE_TYPE_CONTENT
    int sortOrder;   // 同级排序
};

// ============================================================
// TreeItem — 内存树节点
// ============================================================
class TreeItem {
public:
    /**
     * @brief 构造树节点
     * @param[in] id 数据库主键 ID
     * @param[in] name 节点显示名称
     * @param[in] nodeType 节点类型（NODE_TYPE_DEPT / NODE_TYPE_CATEGORY / NODE_TYPE_CONTENT）
     * @param[in] parent 父节点指针，根节点传入 nullptr
     * @details m_loaded 默认 false 以支持懒加载，m_checkState 默认 Qt::Unchecked
     */
    TreeItem(qlonglong id, const QString &name, int nodeType, TreeItem *parent = nullptr);

    /**
     * @brief 析构树节点，递归释放所有子节点
     */
    ~TreeItem();

    // ---- 子节点管理 ----

    /**
     * @brief 添加子节点
     * @param[in] child 待添加的子节点指针（由调用方分配内存）
     */
    void appendChild(TreeItem *child);

    /**
     * @brief 获取指定行号的子节点
     * @param[in] row 行号（从 0 开始）
     * @return 返回子节点指针，行号越界时返回 nullptr
     */
    TreeItem *child(int row) const;

    /**
     * @brief 移除并返回指定行号的子节点
     * @param[in] row 行号（从 0 开始）
     * @return 返回被移除的子节点指针，调用方负责释放内存；越界返回 nullptr
     * @details 从子节点列表中取出但不释放内存，用于删除操作
     */
    TreeItem *takeChild(int row);

    /**
     * @brief 获取子节点数量
     * @return 返回子节点个数
     */
    int childCount() const;

    /**
     * @brief 获取当前节点在父节点中的行号
     * @return 返回行号（从 0 开始），根节点返回 0
     */
    int row() const;

    /**
     * @brief 获取父节点指针
     * @return 返回父节点指针，根节点返回 nullptr
     */
    TreeItem *parent() const;

    // ---- 数据访问 ----

    /**
     * @brief 获取数据库主键 ID
     * @return 返回节点 ID
     */
    qlonglong id() const;

    /**
     * @brief 获取节点显示名称
     * @return 返回节点名称
     */
    QString name() const;

    /**
     * @brief 设置节点显示名称
     * @param[in] name 新名称
     */
    void setName(const QString &name);

    /**
     * @brief 获取节点类型
     * @return 返回节点类型（NODE_TYPE_DEPT / NODE_TYPE_CATEGORY / NODE_TYPE_CONTENT）
     */
    int nodeType() const;

    /**
     * @brief 设置节点类型
     * @param[in] type 新类型
     */
    void setNodeType(int type);

    // ---- 懒加载 ----

    /**
     * @brief 查询子节点是否已从数据库加载
     * @return 已加载返回 true，未加载返回 false
     */
    bool isLoaded() const;

    /**
     * @brief 设置子节点加载状态
     * @param[in] loaded 加载状态
     */
    void setLoaded(bool loaded);

    // ---- 勾选状态 ----

    /**
     * @brief 获取节点勾选状态
     * @return 返回勾选状态（Qt::Checked / Qt::Unchecked / Qt::PartiallyChecked）
     */
    Qt::CheckState checkState() const;

    /**
     * @brief 设置节点勾选状态
     * @param[in] state 新勾选状态
     */
    void setCheckState(Qt::CheckState state);

private:
    qlonglong m_id;                             ///< 节点在数据库中的主键 ID
    QString m_name;                             ///< 节点显示名称
    int m_nodeType;                             ///< 节点类型（NODE_TYPE_DEPT / NODE_TYPE_CATEGORY / NODE_TYPE_CONTENT）
    QList<TreeItem *> m_children;               ///< 子节点列表，析构时通过 qDeleteAll 递归释放
    TreeItem *m_parent;                         ///< 父节点指针（不作为所有者），根节点为 nullptr
    bool m_loaded;                              ///< 子节点是否已从数据库加载
    Qt::CheckState m_checkState;                ///< 勾选状态（仅 InfoModel 使用，ClassModel 忽略）
};

// ============================================================
// DataManager — 数据库操作（单例）
// ============================================================
class DataManager {
public:
    /**
     * @brief 获取 DataManager 单例实例
     * @return 返回 DataManager 的单例引用
     * @details 使用局部静态变量实现线程安全的懒加载单例
     */
    static DataManager &instance();

    /**
     * @brief 查询指定节点的子节点列表
     * @param[in] parentId 父节点 ID，传入 0 表示查询根节点（parent_id IS NULL）
     * @param[in] nodeTypes 节点类型过滤列表，空列表表示查询所有类型
     * @return 返回子节点列表，按 sort_order, id 排序；查询失败时返回空列表
     * @details 使用预处理查询防止 SQL 注入
     */
    QList<Node> queryChildren(qlonglong parentId, const QList<int> &nodeTypes = {});

    /**
     * @brief 向数据库插入新节点
     * @param[in] name 节点名称
     * @param[in] nodeType 节点类型
     * @param[in] parentId 父节点 ID
     * @param[out] newId 输出参数，返回新插入节点的自增 ID
     * @return 成功返回 true，失败返回 false
     * @details 使用预处理插入防止 SQL 注入
     */
    bool insertNode(const QString &name, int nodeType, qlonglong parentId, qlonglong &newId);

    /**
     * @brief 更新节点名称
     * @param[in] id 目标节点 ID
     * @param[in] name 新名称
     * @return 成功返回 true，失败返回 false
     */
    bool updateName(qlonglong id, const QString &name);

    /**
     * @brief 删除指定节点及其所有子孙节点
     * @param[in] id 目标节点 ID
     * @return 成功返回 true，失败返回 false
     * @details 通过广度优先遍历收集所有子孙节点 ID，从叶子向根逐层删除
     */
    bool deleteNode(qlonglong id);

    /**
     * @brief 检查节点是否有子节点
     * @param[in] parentId 父节点 ID
     * @return 有子节点返回 true，无子节点或查询失败返回 false
     * @details 使用 LIMIT 1 优化查询，仅需判断是否存在至少一条记录
     */
    bool hasChildren(qlonglong parentId);

private:
    DataManager() = default;
    DataManager(const DataManager &) = delete;
    DataManager &operator=(const DataManager &) = delete;
};

// ============================================================
// ClassModel — treeView_Class 的部门分类树模型
// ============================================================
class ClassModel : public QAbstractItemModel {
    Q_OBJECT
public:
    /**
     * @brief 构造部门分类树模型
     * @param[in] parent 父 QObject 指针
     * @details 创建不可见的虚拟根节点，从数据库加载顶级部门（NODE_TYPE_DEPT, parent_id IS NULL）
     *          作为根节点的子节点。m_rootItem 不显示在视图中。
     */
    explicit ClassModel(QObject *parent = nullptr);

    /**
     * @brief 析构树模型，释放根节点及其所有子孙节点
     */
    ~ClassModel();

    // ---- QAbstractItemModel 接口 ----

    /**
     * @brief 根据行号、列号和父索引创建 QModelIndex
     * @param[in] row 行号
     * @param[in] column 列号
     * @param[in] parent 父索引，无效时使用 m_rootItem 作为父节点
     * @return 返回创建的模型索引，越界时返回无效索引
     * @details 通过 internalPointer 存储对应 TreeItem 指针
     */
    QModelIndex index(int row, int column, const QModelIndex &parent) const override;

    /**
     * @brief 获取指定索引的父索引
     * @param[in] index 子节点索引
     * @return 返回父节点的 QModelIndex，根节点或无效索引返回无效 QModelIndex
     */
    QModelIndex parent(const QModelIndex &index) const override;

    /**
     * @brief 获取指定父节点下的子节点行数
     * @param[in] parent 父索引，无效时返回根级别行数
     * @return 返回子节点数量
     */
    int rowCount(const QModelIndex &parent) const override;

    /**
     * @brief 获取列数
     * @return 始终返回 1（树形控件仅显示一列）
     */
    int columnCount(const QModelIndex &) const override;

    /**
     * @brief 获取指定索引和角色的显示数据
     * @param[in] index 节点索引
     * @param[in] role 数据角色（Qt::DisplayRole 返回名称，Qt::DecorationRole 根据 nodeType 返回图标）
     * @return 返回对应的 QVariant 数据，无效索引返回空 QVariant
     */
    QVariant data(const QModelIndex &index, int role) const override;

    /**
     * @brief 获取节点标志位
     * @param[in] index 节点索引
     * @return 有效节点返回 启用|可选；无效索引返回 NoItemFlags
     * @details 部门分类树不包含勾选框，因此不加 Qt::ItemIsUserCheckable
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /**
     * @brief 判断节点是否还有更多子节点可加载
     * @param[in] parent 父索引
     * @return 未加载且非分类节点返回 true，否则返回 false
     * @details 分类节点（NODE_TYPE_CATEGORY）始终返回 false，使其在视图中无展开箭头
     */
    bool canFetchMore(const QModelIndex &parent) const override;

    /**
     * @brief 触发懒加载，从数据库加载子节点数据
     * @param[in] parent 父索引
     * @details 由视图在展开节点时自动调用，内部委托给 loadChildren
     */
    void fetchMore(const QModelIndex &parent) override;

    /**
     * @brief 判断节点是否有子节点
     * @param[in] parent 父索引
     * @return 有子节点返回 true
     * @details 分类节点始终返回 false；已加载节点看缓存；未加载节点查询数据库
     */
    bool hasChildren(const QModelIndex &parent) const override;

private:
    TreeItem *m_rootItem;   ///< 不可见的虚拟根节点，其子节点为顶级部门，析构时递归释放全部子孙

    /**
     * @brief 从数据库加载指定节点的子节点（懒加载核心实现）
     * @param[in] parentItem 父节点指针
     * @details 若已加载则直接返回；否则查询数据库（查询所有 nodeType），
     *          通过 beginInsertRows/endInsertRows 通知视图批量插入新行
     */
    void loadChildren(TreeItem *parentItem);

    /**
     * @brief 将 TreeItem 指针转换为 QModelIndex
     * @param[in] item 树节点指针，若为 m_rootItem 则返回无效索引
     * @return 返回对应的模型索引
     */
    QModelIndex indexFromItem(TreeItem *item) const;
};

// ============================================================
// InfoModel — treeView_Info 的内容树模型（带勾选框）
// ============================================================
class InfoModel : public QAbstractItemModel {
    Q_OBJECT
public:
    /**
     * @brief 构造内容树模型
     * @param[in] parent 父 QObject 指针
     * @details 创建空的内容树，需调用 loadCategory 加载具体分类下的内容
     */
    explicit InfoModel(QObject *parent = nullptr);

    /**
     * @brief 析构内容树模型，释放根节点及其所有子孙节点
     */
    ~InfoModel();

    // ---- QAbstractItemModel 接口 ----

    /**
     * @brief 根据行号、列号和父索引创建 QModelIndex
     * @param[in] row 行号
     * @param[in] column 列号
     * @param[in] parent 父索引，无效时使用 m_rootItem 作为父节点
     * @return 返回创建的模型索引，越界时返回无效索引
     */
    QModelIndex index(int row, int column, const QModelIndex &parent) const override;

    /**
     * @brief 获取指定索引的父索引
     * @param[in] index 子节点索引
     * @return 返回父节点的 QModelIndex，无效索引返回无效 QModelIndex
     */
    QModelIndex parent(const QModelIndex &index) const override;

    /**
     * @brief 获取指定父节点下的子节点行数
     * @param[in] parent 父索引，无效时返回根级别行数
     * @return 返回子节点数量
     */
    int rowCount(const QModelIndex &parent) const override;

    /**
     * @brief 获取列数
     * @return 始终返回 1
     */
    int columnCount(const QModelIndex &) const override;

    /**
     * @brief 获取指定索引和角色的显示数据
     * @param[in] index 节点索引
     * @param[in] role 数据角色（Qt::DisplayRole 返回名称，Qt::CheckStateRole 返回勾选状态）
     * @return 返回对应的 QVariant 数据，无效索引返回空 QVariant
     */
    QVariant data(const QModelIndex &index, int role) const override;

    /**
     * @brief 获取节点标志位
     * @param[in] index 节点索引
     * @return 有效节点返回 启用|可选|可勾选；无效索引返回 NoItemFlags
     * @details 内容树包含勾选框，因此加入 Qt::ItemIsUserCheckable
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /**
     * @brief 修改节点数据（处理勾选状态变更）
     * @param[in] index 节点索引
     * @param[in] value 新值
     * @param[in] role 数据角色，仅处理 Qt::CheckStateRole
     * @return 成功返回 true，失败返回 false
     * @details 核心勾选联动逻辑：勾选/取消节点后递归同步所有子孙节点，
     *          再递归向上更新祖先节点的半选状态。通过 m_updatingCheckState
     *          标志位防止递归过程中重复触发联动。
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    /**
     * @brief 判断节点是否还有更多子节点可加载
     * @param[in] parent 父索引
     * @return 未加载返回 true，已加载返回 false
     */
    bool canFetchMore(const QModelIndex &parent) const override;

    /**
     * @brief 触发懒加载，从数据库加载子节点数据
     * @param[in] parent 父索引
     */
    void fetchMore(const QModelIndex &parent) override;

    /**
     * @brief 判断节点是否有子节点
     * @param[in] parent 父索引
     * @return 有子节点返回 true
     * @details 已加载节点看缓存，未加载节点查询数据库
     */
    bool hasChildren(const QModelIndex &parent) const override;

    // ---- 分类加载 ----

    /**
     * @brief 加载指定分类下的内容树
     * @param[in] categoryId 分类节点 ID
     * @param[in] categoryName 分类名称
     * @details 清空旧数据（通过 beginResetModel/endResetModel），
     *          查询该分类下所有 NODE_TYPE_CONTENT 子节点并构建新树
     */
    void loadCategory(qlonglong categoryId, const QString &categoryName);

    /**
     * @brief 获取当前加载的分类 ID
     * @return 返回分类 ID，未加载时返回 0
     */
    qlonglong categoryId() const;

    /**
     * @brief 获取当前加载的分类名称
     * @return 返回分类名称
     */
    QString categoryName() const;

    // ---- 全选 / 取消全选 ----

    /**
     * @brief 将内容树中所有节点全选
     * @details 先递归展开所有层级，再将所有节点设为 Qt::Checked
     */
    void selectAll();

    /**
     * @brief 将内容树中所有节点取消全选
     * @details 将所有已加载节点设为 Qt::Unchecked
     */
    void deselectAll();

    /**
     * @brief 收集已勾选的内容节点路径
     * @return 返回路径字符串列表，格式为 "节点名/子节点名/..."
     * @details 仅遍历已加载的节点
     */
    QStringList checkedPaths();

private:
    TreeItem *m_rootItem;                       ///< 不可见的虚拟根节点，其子节点为顶层内容节点
    qlonglong m_categoryId;                     ///< 当前加载的分类节点 ID，0 表示未加载
    QString m_categoryName;                     ///< 当前加载的分类名称
    bool m_updatingCheckState;                  ///< 批量更新标志，防止 setData 中递归触发勾选联动

    /**
     * @brief 从数据库加载指定内容节点的子节点
     * @param[in] parentItem 父节点指针
     * @details 仅查询 NODE_TYPE_CONTENT 类型的子节点
     */
    void loadChildren(TreeItem *parentItem);

    /**
     * @brief 将 TreeItem 指针转换为 QModelIndex
     * @param[in] item 树节点指针，若为 m_rootItem 则返回无效索引
     * @return 返回对应的模型索引
     */
    QModelIndex indexFromItem(TreeItem *item) const;

    /**
     * @brief 递归设置所有子孙节点的勾选状态
     * @param[in] parent 父节点指针
     * @param[in] state 目标勾选状态
     * @details 若子节点未加载则先加载；每设置一个节点后 emit dataChanged 通知视图
     */
    void setChildrenCheckState(TreeItem *parent, Qt::CheckState state);

    /**
     * @brief 向上递归更新祖先节点的勾选状态
     * @param[in] child 子节点指针
     * @details 从 child 的父节点开始向上遍历到 m_rootItem，
     *          根据各祖先的所有子节点状态计算其应为 Checked / Unchecked / PartiallyChecked
     */
    void updateAncestorCheckStates(TreeItem *child);

    /**
     * @brief 递归收集已勾选节点的路径
     * @param[in] item 当前遍历的节点
     * @param[in] parentPath 父路径前缀
     * @param[out] result 输出结果列表
     * @details 仅收集 Checked 或 PartiallyChecked 的节点
     */
    void collectCheckedPaths(TreeItem *item, const QString &parentPath, QStringList &result);
};

// ============================================================
// Tree — 主窗口
// ============================================================
class Tree : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief 构造 Tree 窗口
     * @param[in] parent 父窗口指针
     * @details 初始化 UI 布局，创建 ClassModel 绑定到 treeView_Class，
     *          创建 InfoModel 绑定到 treeView_Info，连接所有按钮信号槽
     */
    Tree(QWidget *parent = nullptr);

    /**
     * @brief 析构 Tree 窗口
     * @details Qt 父子对象树自动回收子控件，无需手动释放
     */
    ~Tree();

private slots:
    /**
     * @brief 响应 treeView_Class 中节点的点击
     * @param[in] index 被点击节点的模型索引
     * @details 仅处理分类节点（NODE_TYPE_CATEGORY），加载对应内容后默认全部展开
     */
    void onCategoryClicked(const QModelIndex &index);

    /**
     * @brief 响应 pushButton_Add 按钮：将 treeView_Info 中勾选的内容以树状文本追加到 textEdit
     */
    void onAdd();

    /**
     * @brief 响应 pushButton_AllSelect 按钮：将 treeView_Info 中所有节点全选
     */
    void onSelectAll();

    /**
     * @brief 响应 pushButton_CancelSelect 按钮：将 treeView_Info 中所有节点取消全选
     */
    void onDeselectAll();

    /**
     * @brief 响应 pushButton_Reset 按钮：清空 textEdit 内容
     */
    void onReset();

    /**
     * @brief 响应 pushButton_OK 按钮：通过日志系统输出 textEdit 内容到控制台
     */
    void onOK();

    /**
     * @brief 响应 pushButton_Cancel 按钮：关闭窗口
     */
    void onCancel();

private:
    Ui::TreeClass m_ui;                 ///< Qt Designer 生成的 UI 布局
    ClassModel *m_classModel;           ///< 左侧部门分类树的模型（由 Qt 父子机制管理生命周期）
    InfoModel  *m_infoModel;            ///< 右侧内容树的模型（由 Qt 父子机制管理生命周期）

    /**
     * @brief 将 treeView_Info 中已勾选的内容格式化为 ASCII 树状文本
     * @return 返回格式化的树形文本，无勾选内容时返回空字符串
     * @details 仅通过 QAbstractItemModel 公有 API 遍历，使用 ├── └── │ 等字符构建树形缩进
     */
    QString formatCheckedTree() const;

    /**
     * @brief 将分类的树状文本追加到 textEdit（同一分类去重替换）
     * @param[in] categoryName 分类名称
     * @param[in] treeText 格式化后的树状文本
     * @details textEdit 中以 "[分类名称]" 行标记每个分类块的起始，
     *          重复添加同一分类时会先移除旧块再追加新块
     */
    void appendToTextEdit(const QString &categoryName, const QString &treeText);
};
