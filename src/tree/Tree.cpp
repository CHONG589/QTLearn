#include "Tree.h"
#include "../log/log.h"
#include "../db/QDBConn.h"

static zch::Logger::ptr g_logger = LOG_NAME("default");

/**
 * @brief 构造 Tree 窗口
 * @param[in] parent 父窗口指针
 * @details 初始化 UI 布局，创建 TreeModel 并绑定到 QTreeView，
 *          隐藏表头使树形控件呈现简洁外观
 */
Tree::Tree(QWidget *parent)
    : QWidget(parent)
{
    LOG_INFO(g_logger) << "Tree 开始构造";

    m_ui.setupUi(this);

    // model 以 this 为 parent，由 Qt 父子机制自动管理生命周期
    TreeModel *model = new TreeModel(this);

    m_ui.treeView->setModel(model);
    // 隐藏表头，使树形控件呈现简洁的单列外观
    m_ui.treeView->setHeaderHidden(true);
}

/**
 * @brief 析构 Tree 窗口
 * @details Qt 父子对象树自动回收子控件，无需手动释放
 */
Tree::~Tree()
{

}

/**
 * @brief 获取 DataManager 单例实例
 * @return 返回 DataManager 的单例引用
 * @details 使用局部静态变量实现线程安全的懒加载单例
 */
DataManager &DataManager::instance() 
{
    static DataManager inst;
    return inst;
}

/**
 * @brief 查询指定节点的子节点列表
 * @param[in] parentId 父节点 ID，传入 0 表示查询根节点（parent_id IS NULL）
 * @return 返回子节点列表，查询失败时返回空列表
 * @details 通过 ScopedConn RAII 获取数据库连接，执行预处理查询防止 SQL 注入
 */
QList<Node> DataManager::queryChildren(qlonglong parentId) 
{
    QList<Node> list;

    try {
        ScopedConn conn;
        // parentId==0 表示查询根节点，此时 parent_id 为 NULL，需用 IS NULL 而非 =?
        QString sql = (parentId == 0)
            ? "SELECT id,name,type FROM tree_nodes WHERE parent_id IS NULL"
            : "SELECT id,name,type FROM tree_nodes WHERE parent_id=?";

        QSqlQuery query = conn->prepare(sql);

        if (parentId != 0) {
            query.addBindValue(parentId);
        }

        // 执行预处理查询，第二个参数 {} 表示无额外输出绑定
        conn->execPrepared(query, {});

        while (query.next()) {
            list.append({
                query.value(0).toLongLong(),
                query.value(1).toString(),
                query.value(2).toInt()
                });
        }

    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "queryChildren failed: " << e.what();
    }

    return list;
}

/**
 * @brief 向数据库插入新节点
 * @param[in] name 节点名称
 * @param[in] type 节点类型（0=文件夹, 1=文件）
 * @param[in] parentId 父节点 ID
 * @param[out] newId 输出参数，返回新插入节点的自增 ID（qlonglong 类型避免截断）
 * @return 成功返回 true，失败返回 false
 * @details 使用预处理插入防止 SQL 注入，通过 lastInsertId 获取自增主键
 */
bool DataManager::insertNode(const QString &name, int type, qlonglong parentId, qlonglong &newId) 
{
    try {
        ScopedConn conn;
        QSqlQuery query = conn->prepare(
            "INSERT INTO tree_nodes(name,type,parent_id) VALUES(?,?,?)"
        );

        conn->execPrepared(query, {name, type, parentId});
        newId = query.lastInsertId().toLongLong();

        return true;

    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "insertNode failed:" << e.what();
        return false;
    }
}

/**
 * @brief 更新节点名称
 * @param[in] id 目标节点 ID
 * @param[in] name 新名称
 * @return 成功返回 true，失败返回 false
 */
bool DataManager::updateName(qlonglong id, const QString &name) 
{
    
    try {
        ScopedConn conn;
        QString sql = "UPDATE tree_nodes SET name=? WHERE id=?";
        conn->execPrepared(sql, {name, id});

        return true;
    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "updateName failed:" << e.what();
        return false;
    }
}

/**
 * @brief 删除指定节点及其所有子孙节点
 * @param[in] id 目标节点 ID
 * @return 成功返回 true，失败返回 false
 * @details 通过广度优先遍历收集该节点下所有子孙节点 ID，
 *          在单个连接内一次性批量删除，避免嵌套 ScopedConn 和孤儿数据。
 */
bool DataManager::deleteNode(qlonglong id) 
{

    try {
        ScopedConn conn;
        // 广度优先遍历收集所有子孙节点 ID
        QList<qlonglong> idsToDelete;
        QList<qlonglong> pending = {id};

        while (!pending.isEmpty()) {
            qlonglong currentId = pending.takeFirst();
            idsToDelete.append(currentId);

            QSqlQuery childQuery = conn->prepare(
                "SELECT id FROM tree_nodes WHERE parent_id=?"
            );
            conn->execPrepared(childQuery, {currentId});

            while (childQuery.next()) {
                pending.append(childQuery.value(0).toLongLong());
            }
        }

        // 从叶子向根逐层删除，避免外键约束问题
        for (int i = idsToDelete.size() - 1; i >= 0; --i) {
            conn->execPrepared("DELETE FROM tree_nodes WHERE id=?", {idsToDelete[i]});
        }

        return true;
    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "deleteNode failed: " << e.what();
        return false;
    }
}

/**
 * @brief 检查节点是否有子节点
 * @param[in] parentId 父节点 ID
 * @return 有子节点返回 true，无子节点或查询失败返回 false
 * @details 使用 LIMIT 1 优化查询，仅需判断是否存在至少一条记录
 */
bool DataManager::hasChildren(qlonglong parentId) 
{
    try {
        ScopedConn conn;
        QSqlQuery query = conn->prepare(
            "SELECT 1 FROM tree_nodes WHERE parent_id=? LIMIT 1"
        );

        conn->execPrepared(query, {parentId});

        return query.next();

    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "hasChildren failed:" << e.what();
        return false;
    }
}

/**
 * @brief 构造树节点
 * @param[in] id 节点在数据库中的主键 ID
 * @param[in] name 节点显示名称
 * @param[in] type 节点类型（0=文件夹, 1=文件）
 * @param[in] parent 父节点指针，根节点传入 nullptr
 * @details 初始化成员变量，m_loaded 默认 false 以支持懒加载
 */
TreeItem::TreeItem(qlonglong id, const QString &name, int type, TreeItem *parent)
    : m_id(id),
    m_name(name),
    m_type(type),
    m_parent(parent),
    m_loaded(false) 
{
}

/**
 * @brief 析构树节点，递归释放所有子节点
 */
TreeItem::~TreeItem() 
{
    qDeleteAll(m_children);
}

// ==================== 子节点管理 ====================

/**
 * @brief 添加子节点
 * @param[in] child 待添加的子节点指针（由调用方分配内存）
 */
void TreeItem::appendChild(TreeItem *child) 
{
    m_children.append(child);
}

/**
 * @brief 获取指定行号的子节点
 * @param[in] row 行号（从 0 开始）
 * @return 返回子节点指针，行号越界时返回 nullptr
 */
TreeItem *TreeItem::child(int row) const 
{
    return m_children.value(row);
}

/**
 * @brief 移除并返回指定行号的子节点
 * @param[in] row 行号（从 0 开始）
 * @return 返回被移除的子节点指针，调用方负责释放内存；越界返回 nullptr
 * @details 从子节点列表中取出但不释放内存，用于删除操作中配合 delete 使用
 */
TreeItem *TreeItem::takeChild(int row) 
{
    if (row < 0 || row >= m_children.size()) {
        LOG_WARN(g_logger) << row << " 溢出：0 <= x < " << m_children.size();
        return nullptr;
    }

    return m_children.takeAt(row);
}

/**
 * @brief 获取子节点数量
 * @return 返回子节点个数
 */
int TreeItem::childCount() const 
{
    return m_children.size();
}

/**
 * @brief 获取当前节点在父节点中的行号
 * @return 返回行号（从 0 开始），根节点返回 0
 */
int TreeItem::row() const 
{
    if (m_parent) {
        // 遍历查找 this 在父节点子列表中的位置，避免 const_cast
        for (int i = 0; i < m_parent->m_children.size(); ++i) {
            if (m_parent->m_children[i] == this) {
                return i;
            }
        }
    }

    return 0;
}

/**
 * @brief 获取父节点指针
 * @return 返回父节点指针，根节点返回 nullptr
 */
TreeItem *TreeItem::parent() const 
{
    return m_parent;
}

/**
 * @brief 获取节点在数据库中的主键 ID
 * @return 返回节点 ID
 */
qlonglong TreeItem::id() const 
{
    return m_id;
}

/**
 * @brief 获取节点显示名称
 * @return 返回节点名称
 */
QString TreeItem::name() const 
{
    return m_name;
}

/**
 * @brief 设置节点显示名称
 * @param[in] name 新名称
 */
void TreeItem::setName(const QString &name) 
{
    m_name = name;
}

/**
 * @brief 获取节点类型
 * @return 返回节点类型（0=文件夹, 1=文件）
 */
int TreeItem::type() const 
{
    return m_type;
}

/**
 * @brief 查询子节点是否已从数据库加载
 * @return 已加载返回 true，未加载返回 false
 */
bool TreeItem::isLoaded() const 
{
    return m_loaded;
}

/**
 * @brief 设置子节点加载状态
 * @param[in] loaded 加载状态
 */
void TreeItem::setLoaded(bool loaded) 
{
    m_loaded = loaded;
}

/**
 * @brief 构造树模型
 * @param[in] parent 父 QObject 指针
 * @details 创建不可见的虚拟根节点 m_rootItem，从数据库加载顶级节点作为其子节点。
 *          m_rootItem 不显示在视图中，仅作为所有顶级节点的逻辑父节点。
 */
TreeModel::TreeModel(QObject *parent)
    : QAbstractItemModel(parent) 
{
    m_rootItem = new TreeItem(0, "", 0, nullptr);
    // 从数据库加载顶级节点（parent_id IS NULL）
    auto list = DataManager::instance().queryChildren(0);
    for (auto &dto : list) {
        auto *child = new TreeItem(dto.id, dto.name, dto.type, m_rootItem);
        m_rootItem->appendChild(child);
    }

    m_rootItem->setLoaded(true);
}

/**
 * @brief 析构树模型，释放 m_rootItem 及其所有子孙节点
 */
TreeModel::~TreeModel() 
{
    if (m_rootItem) {
        delete m_rootItem;
        m_rootItem = nullptr;
    }
}

/**
 * @brief 根据行号、列号和父索引创建 QModelIndex
 * @param[in] row 行号
 * @param[in] column 列号
 * @param[in] parent 父索引，无效时使用 m_rootItem 作为父节点
 * @return 返回创建的模型索引，越界时返回无效索引
 * @details 通过 internalPointer 存储对应 TreeItem 指针，供后续 data/setData 等方法取回
 */
QModelIndex TreeModel::index(int row, int column, const QModelIndex &parent) const 
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    TreeItem *child = parentItem->child(row);
    if (child) {
        return createIndex(row, column, child);
    }

    return QModelIndex();
}

/**
 * @brief 获取指定索引的父索引
 * @param[in] index 子节点索引
 * @return 返回父节点的 QModelIndex，根节点或无效索引返回无效 QModelIndex
 * @details 从 internalPointer 取出 TreeItem，再通过其 parent() 获取父节点，
 *          若父节点为 m_rootItem 则返回无效索引（m_rootItem 对视图不可见）
 */
QModelIndex TreeModel::parent(const QModelIndex &index) const 
{
    if (!index.isValid()) {
        return QModelIndex();
    }

    TreeItem *child = static_cast<TreeItem *>(index.internalPointer());
    TreeItem *parentItem = child->parent();
    if (!parentItem || parentItem == m_rootItem) {
        return QModelIndex();
    }

    return createIndex(parentItem->row(), 0, parentItem);
}

/**
 * @brief 获取指定父节点下的子节点行数
 * @param[in] parent 父索引，无效时返回根级别行数
 * @return 返回子节点数量
 * @details 视图通过此方法判断节点是否有子节点，从而决定是否显示展开箭头
 */
int TreeModel::rowCount(const QModelIndex &parent) const 
{
    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    return parentItem->childCount();
}

/**
 * @brief 获取列数
 * @return 始终返回 1（树形控件仅显示一列名称）
 */
int TreeModel::columnCount(const QModelIndex &) const 
{
    return 1;
}

/**
 * @brief 获取指定索引和角色的显示数据
 * @param[in] index 节点索引
 * @param[in] role 数据角色（Qt::DisplayRole 返回名称，Qt::DecorationRole 返回图标）
 * @return 返回对应的 QVariant 数据，无效索引返回空 QVariant
 * @details DisplayRole 返回节点名称供视图显示；DecorationRole 根据 type 返回文件夹或文件图标
 */
QVariant TreeModel::data(const QModelIndex &index, int role) const 
{
    if (!index.isValid()) {
        return QVariant();
    }

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    if (role == Qt::DisplayRole) {
        if (item == m_rootItem) {
            return QVariant();
        }
        return item->name();
    }

    // 用于装饰显示的属性
    if (role == Qt::DecorationRole) {
        if (item->type() == 0)
            return QIcon(":/icons/folder.png");
        else
            return QIcon(":/icons/file.png");
    }

    return QVariant();
}

/**
 * @brief 获取节点标志位
 * @param[in] index 节点索引
 * @return 有效节点返回 启用|可选|可编辑，无效索引返回 NoItemFlags
 * @details 返回 Qt::ItemIsEditable 使节点支持双击重命名
 */
Qt::ItemFlags TreeModel::flags(const QModelIndex &index) const 
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

/**
 * @brief 修改节点数据（重命名）
 * @param[in] index 节点索引
 * @param[in] value 新值
 * @param[in] role 数据角色，仅处理 Qt::EditRole
 * @return 成功返回 true，失败返回 false
 * @details 先更新数据库，成功后再更新内存中的 TreeItem，最后发射 dataChanged 信号通知视图刷新
 */
bool TreeModel::setData(const QModelIndex &index, const QVariant &value, int role) 
{
    if (role != Qt::EditRole) {
        return false;
    }

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    // 更新数据库
    if (!DataManager::instance().updateName(item->id(), value.toString())) {
        return false;
    }

    item->setName(value.toString());
    emit dataChanged(index, index);

    return true;
}

/**
 * @brief 判断节点是否还有更多子节点可加载
 * @param[in] parent 父索引
 * @return 未加载返回 true（驱动视图显示展开箭头），已加载返回 false
 * @details 懒加载核心：视图展开节点前调用此方法，返回 true 则调用 fetchMore 触发加载
 */
bool TreeModel::canFetchMore(const QModelIndex &parent) const 
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    return !item->isLoaded();
}

/**
 * @brief 触发懒加载，从数据库加载子节点数据
 * @param[in] parent 父索引
 * @details 由视图在展开节点时自动调用，内部委托给 loadChildren 执行实际加载
 */
void TreeModel::fetchMore(const QModelIndex &parent) 
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    loadChildren(item);
}

/**
 * @brief 从数据库加载指定节点的子节点（懒加载核心实现）
 * @param[in] parentItem 父节点指针
 * @details 若已加载则直接返回；否则查询数据库，通过 beginInsertRows/endInsertRows
 *          通知视图批量插入新行，最后标记 parentItem 为已加载
 */
void TreeModel::loadChildren(TreeItem *parentItem) 
{
    if (parentItem->isLoaded()) {
        return;
    }

    auto list = DataManager::instance().queryChildren(parentItem->id());
    int begin = 0;
    int end = list.size() - 1;
    if (end >= begin) {
        beginInsertRows(indexFromItem(parentItem), begin, end);
        for (auto &dto : list) {
            auto *child = new TreeItem(dto.id, dto.name, dto.type, parentItem);
            parentItem->appendChild(child);
        }

        endInsertRows();
    }

    parentItem->setLoaded(true);
}

/**
 * @brief 将 TreeItem 指针转换为 QModelIndex
 * @param[in] item 树节点指针，若为 m_rootItem 则返回无效索引
 * @return 返回对应的模型索引
 */
QModelIndex TreeModel::indexFromItem(TreeItem *item) const 
{
    if (item == m_rootItem) {
        return QModelIndex();
    }

    return createIndex(item->row(), 0, item);
}

/**
 * @brief 向指定父节点下添加新节点
 * @param[in] parentIdx 父节点索引，无效时添加到根级别
 * @param[in] name 新节点名称
 * @param[in] type 新节点类型（0=文件夹, 1=文件）
 * @details 先写入数据库获取自增 ID，再通过 beginInsertRows/endInsertRows 通知视图插入新行
 */
void TreeModel::addNode(const QModelIndex &parentIdx, const QString &name, int type) 
{
    TreeItem *parentItem = parentIdx.isValid()
        ? static_cast<TreeItem *>(parentIdx.internalPointer())
        : m_rootItem;

    qlonglong newId = -1;
    if (!DataManager::instance().insertNode(name, type, parentItem->id(), newId)) {
        return;
    }
    if (newId < 0) {
        return;
    }

    int row = parentItem->childCount();

    beginInsertRows(parentIdx, row, row);

    parentItem->appendChild(new TreeItem(newId, name, type, parentItem));

    endInsertRows();
}

/**
 * @brief 删除指定节点
 * @param[in] index 待删除节点的索引
 * @details 先从父节点内存中取出子节点并通知视图，再删除数据库记录。
 *          若 DB 删除失败则将节点重新插回父节点，保证内存与数据库状态一致。
 */
void TreeModel::removeNode(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    TreeItem *parentItem = item->parent();

    int row = item->row();

    // 先通知视图并取出子节点
    beginRemoveRows(index.parent(), row, row);

    TreeItem *taken = parentItem->takeChild(row);

    endRemoveRows();

    // 再删除数据库记录
    if (!DataManager::instance().deleteNode(item->id())) {
        // DB 删除失败，将节点重新插回父节点
        beginInsertRows(index.parent(), row, row);
        parentItem->appendChild(taken);
        endInsertRows();
        return;
    }

    // DB 删除成功，释放内存
    delete taken;
}

/**
 * @brief 判断节点是否有子节点
 * @param[in] parent 父索引
 * @return 有子节点返回 true
 * @details 优先使用内存缓存（已加载时直接看 childCount），
 *          未加载时查询数据库确认（用于控制右键菜单等场景）
 */
bool TreeModel::hasChildren(const QModelIndex &parent) const 
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    // 如果已经加载过，直接看缓存
    if (item->isLoaded()) {
        return item->childCount() > 0;
    }

    // ⭐ 没加载 → 去数据库判断
    return DataManager::instance().hasChildren(item->id());
}

