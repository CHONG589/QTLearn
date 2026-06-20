#pragma once

#include <QtWidgets/QWidget>

#include "ui_Tree.h"
#include <QAbstractItemModel>

/**
 * @brief 主窗口，承载 QTreeView 控件
 * @details 负责初始化 UI 布局，创建 TreeModel 并绑定到视图
 */
class Tree : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造 Tree 窗口
     * @param[in] parent 父窗口指针
     * @details 初始化 UI 布局，创建 TreeModel 并绑定到 QTreeView
     */
    Tree(QWidget *parent = nullptr);

    /**
     * @brief 析构 Tree 窗口
     * @details Qt 父子对象树自动回收子控件，无需手动释放
     */
    ~Tree();

private:
    Ui::TreeClass m_ui;
};

/**
 * @brief 数据库查询结果的数据传输对象
 * @details 封装单条 tree_nodes 记录，用于 DataManager 与 TreeItem 之间的数据传递
 */
struct Node {
    qlonglong id;    /// 数据库主键 ID
    QString name;    /// 节点显示名称
    int type;        /// 节点类型（0=文件夹, 1=文件）
};

/**
 * @brief 内存中的树节点
 * @details 维护 id/name/type 数据、父子关系和懒加载状态。
 *          析构时通过 qDeleteAll 递归释放所有子节点。
 *          m_parent 不作为所有者（不负责释放），仅作为回溯链接。
 */
class TreeItem {
public:
    /**
     * @brief 构造树节点
     * @param[in] id 数据库主键 ID
     * @param[in] name 节点显示名称
     * @param[in] type 节点类型（0=文件夹, 1=文件）
     * @param[in] parent 父节点指针，根节点传入 nullptr
     * @details m_loaded 默认 false 以支持懒加载
     */
    TreeItem(qlonglong id, const QString &name, int type, TreeItem *parent = nullptr);

    /**
     * @brief 析构树节点，递归释放所有子节点
     */
    ~TreeItem();

    // ==================== 子节点管理 ====================

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

    // ==================== 数据访问 ====================

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
     * @return 返回节点类型（0=文件夹, 1=文件）
     */
    int type() const;

    // ==================== 懒加载状态 ====================

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

private:
    qlonglong m_id;                     /// 节点在数据库中的主键 ID
    QString m_name;                     /// 节点显示名称
    int m_type;                         /// 节点类型（0=文件夹, 1=文件）

    QList<TreeItem *> m_children;       /// 节点的孩子节点
    TreeItem *m_parent;                 /// 父节点指针，根节点传入 nullptr

    bool m_loaded;                      /// 是否加载数据
};

/**
 * @brief 数据管理器，封装所有数据库操作
 * @details 单例模式，提供对 tree_nodes 表的 CRUD 操作。
 *          所有方法在异常时记录日志并返回 false/空列表，不向调用方抛出异常。
 */
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
     * @param[in] parentId 父节点 ID，传入 0 表示查询根节点
     * @return 返回子节点列表，查询失败时返回空列表
     * @details 使用预处理查询防止 SQL 注入
     */
    QList<Node> queryChildren(qlonglong parentId);

    /**
     * @brief 向数据库插入新节点
     * @param[in] name 节点名称
     * @param[in] type 节点类型（0=文件夹, 1=文件）
     * @param[in] parentId 父节点 ID
     * @param[out] newId 输出参数，返回新插入节点的自增 ID
     * @return 成功返回 true，失败返回 false
     * @details 使用预处理插入防止 SQL 注入
     */
    bool insertNode(const QString &name, int type, qlonglong parentId, qlonglong &newId);

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
     */
    bool hasChildren(qlonglong parentId);

private:
    DataManager() = default;
    DataManager(const DataManager &) = delete;
    DataManager &operator=(const DataManager &) = delete;
};

/**
 * @brief 树形模型，连接 TreeItem 数据与 QTreeView 视图
 * @details 实现 QAbstractItemModel 接口，支持懒加载（canFetchMore/fetchMore）、
 *          原地编辑（setData/dataChanged）和增删节点（addNode/removeNode）。
 *          m_rootItem 为不可见的虚拟根节点，不显示在视图中。
 */
class TreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    /**
     * @brief 构造树模型
     * @param[in] parent 父 QObject 指针
     * @details 创建不可见的虚拟根节点，从数据库加载顶级节点作为其子节点
     */
    explicit TreeModel(QObject *parent = nullptr);

    /**
     * @brief 析构树模型，释放根节点及其所有子孙节点
     */
    ~TreeModel();

    // ==================== QAbstractItemModel 必须实现的接口 ====================

    /**
     * @brief 根据行号、列号和父索引创建 QModelIndex
     * @param[in] row 行号
     * @param[in] column 列号
     * @param[in] parent 父索引，无效时使用根节点作为父节点
     * @return 返回创建的模型索引，越界时返回无效索引
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
     * @param[in] parent 父索引（本实现未使用）
     * @return 始终返回 1（树形控件仅显示一列）
     */
    int columnCount(const QModelIndex &parent) const override;

    /**
     * @brief 获取指定索引和角色的显示数据
     * @param[in] index 节点索引
     * @param[in] role 数据角色（Qt::DisplayRole 返回名称，Qt::DecorationRole 返回图标）
     * @return 返回对应的 QVariant 数据，无效索引返回空 QVariant
     */
    QVariant data(const QModelIndex &index, int role) const override;

    /**
     * @brief 获取节点标志位
     * @param[in] index 节点索引
     * @return 有效节点返回 启用|可选|可编辑，无效索引返回 NoItemFlags
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /**
     * @brief 修改节点数据（重命名）
     * @param[in] index 节点索引
     * @param[in] value 新值
     * @param[in] role 数据角色，仅处理 Qt::EditRole
     * @return 成功返回 true，失败返回 false
     * @details 先更新数据库，成功后再更新内存并发射 dataChanged 信号
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    // ==================== 懒加载 ====================

    /**
     * @brief 判断节点是否还有更多子节点可加载
     * @param[in] parent 父索引
     * @return 未加载返回 true（驱动视图显示展开箭头），已加载返回 false
     */
    bool canFetchMore(const QModelIndex &parent) const override;

    /**
     * @brief 触发懒加载，从数据库加载子节点数据
     * @param[in] parent 父索引
     * @details 由视图在展开节点时自动调用
     */
    void fetchMore(const QModelIndex &parent) override;

    // ==================== 增删接口 ====================

    /**
     * @brief 向指定父节点下添加新节点
     * @param[in] parent 父节点索引，无效时添加到根级别
     * @param[in] name 新节点名称
     * @param[in] type 新节点类型（0=文件夹, 1=文件）
     * @details 先写入数据库获取自增 ID，再通过 beginInsertRows/endInsertRows 通知视图
     */
    void addNode(const QModelIndex &parent, const QString &name, int type);

    /**
     * @brief 删除指定节点
     * @param[in] index 待删除节点的索引
     * @details 先从内存中取出节点并通知视图，再删除数据库记录。
     *          若 DB 删除失败则将节点重新插回父节点，保证内存与数据库状态一致。
     */
    void removeNode(const QModelIndex &index);

    /**
     * @brief 判断节点是否有子节点
     * @param[in] parent 父索引
     * @return 有子节点返回 true
     * @details 优先使用内存缓存（已加载时直接看 childCount），
     *          未加载时查询数据库确认
     */
    bool hasChildren(const QModelIndex &parent) const override;

private:
    TreeItem *m_rootItem;

    /**
     * @brief 从数据库加载指定节点的子节点（懒加载核心实现）
     * @param[in] parentItem 父节点指针
     * @details 若已加载则直接返回；否则查询数据库，通过 beginInsertRows/endInsertRows
     *          通知视图批量插入新行
     */
    void loadChildren(TreeItem *parentItem);

    /**
     * @brief 将 TreeItem 指针转换为 QModelIndex
     * @param[in] item 树节点指针，若为根节点则返回无效索引
     * @return 返回对应的模型索引
     */
    QModelIndex indexFromItem(TreeItem *item) const;
};
