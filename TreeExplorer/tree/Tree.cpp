#include "Tree.h"
#include "log/log.h"
#include "db/QDBConn.h"
#include <functional>
#include <QPushButton>

static zch::Logger::ptr g_logger = LOG_NAME("default");

// ============================================================
// TreeItem 实现
// ============================================================

/**
 * @brief 构造树节点
 * @param[in] id 节点在数据库中的主键 ID
 * @param[in] name 节点显示名称
 * @param[in] nodeType 节点类型（NODE_TYPE_DEPT / NODE_TYPE_CATEGORY / NODE_TYPE_CONTENT）
 * @param[in] parent 父节点指针，根节点传入 nullptr
 * @details 初始化成员变量，m_loaded 默认 false 以支持懒加载，
 *          m_checkState 默认 Qt::Unchecked。
 *          m_children 在初始化列表中已默认构造为空，这里显式 clear 保证一致性。
 */
TreeItem::TreeItem(qlonglong id, const QString &name, int nodeType, TreeItem *parent)
    : m_id(id)
    , m_name(name)
    , m_nodeType(nodeType)
    , m_parent(parent)
    , m_loaded(false)
    , m_checkState(Qt::Unchecked)
{
    m_children.clear();
}

/**
 * @brief 析构树节点，递归释放所有子节点
 */
TreeItem::~TreeItem()
{
    qDeleteAll(m_children);
}

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
        LOG_WARN(g_logger) << row << " out of range: 0 <= x < " << m_children.size();
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
 * @details 遍历父节点子列表查找 this 指针位置，避免 const_cast
 */
int TreeItem::row() const
{
    if (!m_parent) {
        return 0;
    }
    for (int i = 0; i < m_parent->childCount(); ++i) {
        if (m_parent->child(i) == this) {
            return i;
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
 * @return 返回节点类型
 */
int TreeItem::nodeType() const
{
    return m_nodeType;
}

/**
 * @brief 设置节点类型
 * @param[in] type 新类型
 */
void TreeItem::setNodeType(int type)
{
    m_nodeType = type;
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
 * @brief 获取节点勾选状态
 * @return 返回勾选状态
 */
Qt::CheckState TreeItem::checkState() const
{
    return m_checkState;
}

/**
 * @brief 设置节点勾选状态
 * @param[in] state 新勾选状态
 */
void TreeItem::setCheckState(Qt::CheckState state)
{
    m_checkState = state;
}

// ============================================================
// DataManager 实现
// ============================================================

/**
 * @brief 获取 DataManager 单例实例
 * @return 返回 DataManager 的单例引用
 * @details 使用局部静态变量实现线程安全的懒加载单例（C++11 保证）
 */
DataManager &DataManager::instance()
{
    static DataManager inst;
    return inst;
}

/**
 * @brief 查询指定节点的子节点列表
 * @param[in] parentId 父节点 ID，传入 0 表示查询根节点（parent_id IS NULL）
 * @param[in] nodeTypes 节点类型过滤列表，空列表表示查询所有类型
 * @return 返回子节点列表，按 sort_order, id 排序；查询失败时返回空列表
 * @details 使用预处理查询防止 SQL 注入。
 *          parentId==0 时 WHERE 条件必须使用 IS NULL 而非 =0，
 *          因为数据库中根节点的 parent_id 存的是 NULL 而非 0。
 *          nodeTypes 非空时追加 AND node_type IN (...) 条件，
 *          placeholder 数量与参数数量严格对应。 */
QList<Node> DataManager::queryChildren(qlonglong parentId, const QList<int> &nodeTypes)
{
    QList<Node> list;
    try {
        ScopedConn conn;

        // SQL 字符串使用运行时拼接而非 QStringLiteral 是刻意的：
        // WHERE 子句结构根据参数动态变化（parentId 是否为 0、nodeTypes 是否为空），
        // 拼接条件字符串比预编译多个模板分支更清晰易维护。
        QString sql = (parentId == 0)
            ? "SELECT id, name, node_type, sort_order FROM org_tree WHERE parent_id IS NULL"
            : "SELECT id, name, node_type, sort_order FROM org_tree WHERE parent_id=?";

        if (!nodeTypes.isEmpty()) {
            QStringList placeholders;
            for (int i = 0; i < nodeTypes.size(); ++i) {
                placeholders << "?";
            }
            sql += " AND node_type IN (" + placeholders.join(",") + ")";
        }
        sql += " ORDER BY sort_order, id";

        QSqlQuery query = conn->prepare(sql);

        // 绑参顺序必须与 SQL 中 ? 的出现顺序一致：先 parentId（如果 !=0），再 nodeTypes
        if (parentId != 0) {
            query.addBindValue(parentId);
        }
        for (int t : nodeTypes) {
            query.addBindValue(t);
        }

        conn->execPrepared(query, {});
        while (query.next()) {
            list.append({
                query.value(0).toLongLong(),
                query.value(1).toString(),
                query.value(2).toInt(),
                query.value(3).toInt()
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
 * @param[in] nodeType 节点类型
 * @param[in] parentId 父节点 ID
 * @param[out] newId 输出参数，返回新插入节点的自增 ID
 * @return 成功返回 true，失败返回 false
 * @details 使用预处理插入防止 SQL 注入，通过 lastInsertId 获取自增主键
 */
bool DataManager::insertNode(const QString &name, int nodeType, qlonglong parentId, qlonglong &newId)
{
    try {
        ScopedConn conn;
        QSqlQuery query = conn->prepare(
            "INSERT INTO org_tree(name, node_type, parent_id) VALUES(?,?,?)"
        );
        conn->execPrepared(query, {name, nodeType, parentId});
        newId = query.lastInsertId().toLongLong();
        return true;
    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "insertNode failed: " << e.what();
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
        conn->execPrepared("UPDATE org_tree SET name=? WHERE id=?", {name, id});
        return true;
    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "updateName failed: " << e.what();
        return false;
    }
}

/**
 * @brief 删除指定节点及其所有子孙节点
 * @param[in] id 目标节点 ID
 * @return 成功返回 true，失败返回 false
 * @details 通过广度优先遍历收集该节点下所有子孙节点 ID，
 *          在单个连接内从叶子向根逐层删除，避免外键约束问题
 */
bool DataManager::deleteNode(qlonglong id)
{
    try {
        ScopedConn conn;
        QList<qlonglong> idsToDelete;
        QList<qlonglong> pending = {id};

        while (!pending.isEmpty()) {
            qlonglong currentId = pending.takeFirst();
            idsToDelete.append(currentId);

            QSqlQuery childQuery = conn->prepare(
                "SELECT id FROM org_tree WHERE parent_id=?"
            );
            conn->execPrepared(childQuery, {currentId});
            while (childQuery.next()) {
                pending.append(childQuery.value(0).toLongLong());
            }
        }

        // 从叶子向根逐层删除（逆序），因为外键约束要求先删子节点再删父节点。
        // BFS 收集时子节点必然在父节点之后加入列表，逆序删除保证子先父后
        for (int i = idsToDelete.size() - 1; i >= 0; --i) {
            conn->execPrepared("DELETE FROM org_tree WHERE id=?", {idsToDelete[i]});
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
            "SELECT 1 FROM org_tree WHERE parent_id=? LIMIT 1"
        );
        conn->execPrepared(query, {parentId});
        return query.next();
    } catch (const DBException &e) {
        LOG_ERROR(g_logger) << "hasChildren failed: " << e.what();
        return false;
    }
}

// ============================================================
// ClassModel 实现
// ============================================================

/**
 * @brief 构造部门分类树模型
 * @param[in] parent 父 QObject 指针
 * @details 创建不可见的虚拟根节点（id=0），从数据库加载顶级部门
 *          （NODE_TYPE_DEPT 且 parent_id IS NULL）作为根节点的子节点。
 *          m_rootItem 不显示在视图中，仅作为所有顶级节点的逻辑父节点。
 */
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

/**
 * @brief 析构树模型，释放 m_rootItem 及其所有子孙节点
 */
ClassModel::~ClassModel()
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
 * @details 通过 internalPointer 存储对应 TreeItem 指针，供后续 data/flags 等方法取回
 */
QModelIndex ClassModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    TreeItem *child = parentItem->child(row);
    return child ? createIndex(row, column, child) : QModelIndex();
}

/**
 * @brief 获取指定索引的父索引
 * @param[in] index 子节点索引
 * @return 返回父节点的 QModelIndex，根节点或无效索引返回无效 QModelIndex
 * @details 从 internalPointer 取出 TreeItem，再通过其 parent() 获取父节点，
 *          若父节点为 m_rootItem 则返回无效索引（m_rootItem 对视图不可见）
 */
QModelIndex ClassModel::parent(const QModelIndex &index) const
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
 */
int ClassModel::rowCount(const QModelIndex &parent) const
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
int ClassModel::columnCount(const QModelIndex &) const
{
    return 1;
}

/**
 * @brief 获取指定索引和角色的显示数据
 * @param[in] index 节点索引
 * @param[in] role 数据角色（Qt::DisplayRole 返回名称，Qt::DecorationRole 根据 nodeType 返回图标）
 * @return 返回对应的 QVariant 数据，无效索引或根节点返回空 QVariant
 * @details 部门节点使用文件夹图标，分类节点使用文件图标
 */
QVariant ClassModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    if (item == m_rootItem) {
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        return item->name();
    }
    if (role == Qt::DecorationRole) {
        if (item->nodeType() == NODE_TYPE_DEPT) {
            return QIcon(":/icons/folder.png");
        } else {
            return QIcon(":/icons/file.png");
        }
    }
    return QVariant();
}

/**
 * @brief 获取节点标志位
 * @param[in] index 节点索引
 * @return 有效节点返回 启用|可选；无效索引返回 NoItemFlags
 * @details 部门分类树不包含勾选框，因此不加 Qt::ItemIsUserCheckable
 */
Qt::ItemFlags ClassModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

/**
 * @brief 判断节点是否还有更多子节点可加载
 * @param[in] parent 父索引
 * @return 未加载且非分类节点返回 true，否则返回 false
 * @details 分类节点（NODE_TYPE_CATEGORY）始终返回 false，
 *          使其在视图中不显示展开箭头，因为分类下没有子部门/子分类
 */
bool ClassModel::canFetchMore(const QModelIndex &parent) const
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    if (item->nodeType() == NODE_TYPE_CATEGORY) {
        // 禁止展开箭头：因为分类下的内容数据量可能很大，且应当由 InfoModel 管理
        return false;
    }
    return !item->isLoaded();
}

/**
 * @brief 触发懒加载，从数据库加载子节点数据
 * @param[in] parent 父索引
 * @details 由视图在展开节点时自动调用，内部委托给 loadChildren 执行实际加载
 */
void ClassModel::fetchMore(const QModelIndex &parent)
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    loadChildren(item);
}

/**
 * @brief 判断节点是否有子节点
 * @param[in] parent 父索引
 * @return 有子节点返回 true
 * @details 分类节点始终返回 false；已加载节点看缓存 childCount；
 *          未加载节点查询数据库确认
 */
bool ClassModel::hasChildren(const QModelIndex &parent) const
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    if (item->nodeType() == NODE_TYPE_CATEGORY) {
        return false;
    }
    if (item->isLoaded()) {
        // 已加载则直接查看 item 下加载的孩子数量
        return item->childCount() > 0;
    }

    // 未加载，则查询数据库
    return DataManager::instance().hasChildren(item->id());
}

/**
 * @brief 从数据库加载指定节点的子节点（懒加载核心实现）
 * @param[in] parentItem 父节点指针
 * @details 若已加载则直接返回；否则查询数据库（不限制 nodeType，
 *          因为在 ClassModel 中部门节点下只有子部门和分类，不含内容节点），
 *          通过 beginInsertRows/endInsertRows 通知视图批量插入新行
 */
void ClassModel::loadChildren(TreeItem *parentItem)
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
            auto *child = new TreeItem(dto.id, dto.name, dto.nodeType, parentItem);
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
QModelIndex ClassModel::indexFromItem(TreeItem *item) const
{
    if (item == m_rootItem) {
        return QModelIndex();
    }
    return createIndex(item->row(), 0, item);
}

// ============================================================
// InfoModel 实现
// ============================================================

/**
 * @brief 构造内容树模型
 * @param[in] parent 父 QObject 指针
 * @details 创建空的内容树（仅含虚拟根节点），
 *          需调用 loadCategory 加载具体分类下的内容数据
 */
InfoModel::InfoModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_rootItem(nullptr)
    , m_categoryId(0)
    , m_updatingCheckState(false)
{
    // 因为 InfoTree 初始化时不需要显示什么，在点击对应的分类时，这个树才需要进行显示内容，
    // 所以在刚开始，这个树先弄一个空的树，啥都不显示，直接设置为已加载，实际啥都没有，在点击
    // 对应的分类节点后，再把这个空树删掉，且释放，视图刷新为最新点击的内容
    m_rootItem = new TreeItem(0, "", NODE_TYPE_CONTENT, nullptr);
    m_rootItem->setLoaded(true);
}

/**
 * @brief 析构内容树模型，释放 m_rootItem 及其所有子孙节点
 */
InfoModel::~InfoModel()
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
 */
QModelIndex InfoModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    TreeItem *child = parentItem->child(row);
    return child ? createIndex(row, column, child) : QModelIndex();
}

/**
 * @brief 获取指定索引的父索引
 * @param[in] index 子节点索引
 * @return 返回父节点的 QModelIndex，根节点或无效索引返回无效 QModelIndex
 */
QModelIndex InfoModel::parent(const QModelIndex &index) const
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
 */
int InfoModel::rowCount(const QModelIndex &parent) const
{
    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;
    return parentItem->childCount();
}

/**
 * @brief 获取列数
 * @return 始终返回 1
 */
int InfoModel::columnCount(const QModelIndex &) const
{
    return 1;
}

/**
 * @brief 获取指定索引和角色的显示数据
 * @param[in] index 节点索引
 * @param[in] role 数据角色（Qt::DisplayRole 返回名称，Qt::CheckStateRole 返回勾选状态）
 * @return 返回对应的 QVariant 数据，无效索引或根节点返回空 QVariant
 */
QVariant InfoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
    if (item == m_rootItem) {
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        return item->name();
    }
    if (role == Qt::CheckStateRole) {
        return item->checkState();
    }
    return QVariant();
}

/**
 * @brief 获取节点标志位
 * @param[in] index 节点索引
 * @return 有效节点返回 启用|可选|可勾选；无效索引返回 NoItemFlags
 * @details 内容树需要勾选框，因此加入 Qt::ItemIsUserCheckable
 */
Qt::ItemFlags InfoModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

/**
 * @brief 修改节点数据（处理勾选状态变更）
 * @param[in] index 节点索引
 * @param[in] value 新值
 * @param[in] role 数据角色，仅处理 Qt::CheckStateRole
 * @return 成功返回 true，失败返回 false
 * @details 核心勾选联动逻辑：
 *          - m_updatingCheckState 标志位防止递归中重复触发联动；
 *            批量更新（selectAll/deselectAll/递归同步）时走快速通道，
 *            仅更新状态+发射信号，不触发新一轮递归。
 *          - 用户手动点击时走完整流程：更新自身 → 向下同步所有子孙 →
 *            向上重算祖先半选状态。 */
bool InfoModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::CheckStateRole) {
        return false;
    }

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
    setChildrenCheckState(item, state);
    updateAncestorCheckStates(item);

    m_updatingCheckState = false;

    emit dataChanged(index, index, {Qt::CheckStateRole});
    return true;
}

/**
 * @brief 判断节点是否还有更多子节点可加载
 * @param[in] parent 父索引
 * @return 未加载返回 true，已加载返回 false
 */
bool InfoModel::canFetchMore(const QModelIndex &parent) const
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    return !item->isLoaded();
}

/**
 * @brief 触发懒加载，从数据库加载子节点数据
 * @param[in] parent 父索引
 */
void InfoModel::fetchMore(const QModelIndex &parent)
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    loadChildren(item);
}

/**
 * @brief 判断节点是否有子节点
 * @param[in] parent 父索引
 * @return 有子节点返回 true
 * @details 已加载节点看缓存 childCount，未加载节点查询数据库
 */
bool InfoModel::hasChildren(const QModelIndex &parent) const
{
    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : m_rootItem;

    if (item->isLoaded()) {
        return item->childCount() > 0;
    }
    return DataManager::instance().hasChildren(item->id());
}

/**
 * @brief 加载指定分类下的内容树
 * @param[in] categoryId 分类节点 ID
 * @param[in] categoryName 分类名称
 * @details 通过 beginResetModel/endResetModel 清空旧数据并重建内容树。
 *          查询该分类下所有 NODE_TYPE_CONTENT 子节点构建顶层内容，
 *          更深层级内容在用户展开时通过 fetchMore 懒加载
 */
void InfoModel::loadCategory(qlonglong categoryId, const QString &categoryName)
{
    LOG_INFO(g_logger) << "loadCategory: id=" << categoryId << " name=" << categoryName;

    // 使用 beginResetModel/endResetModel 完全重建模型，
    // 确保旧的 TreeItem 树被释放、勾选状态归零、视图完全刷新
    beginResetModel();
    delete m_rootItem;

    m_rootItem = new TreeItem(0, "", NODE_TYPE_CONTENT, nullptr);
    m_categoryId = categoryId;
    m_categoryName = categoryName;

    // 查询该分类下的顶层内容节点（更深层级在用户展开时懒加载）
    auto list = DataManager::instance().queryChildren(categoryId, {NODE_TYPE_CONTENT});
    LOG_INFO(g_logger) << "loadCategory: found " << list.size() << " content items";

    for (auto &dto : list) {
        auto *child = new TreeItem(dto.id, dto.name, dto.nodeType, m_rootItem);
        m_rootItem->appendChild(child);
    }
    m_rootItem->setLoaded(true);
    endResetModel();
}

/**
 * @brief 获取当前加载的分类 ID
 * @return 返回分类 ID，未加载时返回 0
 */
qlonglong InfoModel::categoryId() const
{
    return m_categoryId;
}

/**
 * @brief 获取当前加载的分类名称
 * @return 返回分类名称
 */
QString InfoModel::categoryName() const
{
    return m_categoryName;
}

/**
 * @brief 将内容树中所有节点全选
 * @details 先递归展开所有层级（确保所有节点已加载），
 *          再通过 setChildrenCheckState 将所有节点设为 Qt::Checked，
 *          最后通过 beginResetModel/endResetModel 通知视图全面刷新
 */
void InfoModel::selectAll()
{
    // 先递归展开所有层级，因为 setChildrenCheckState 只遍历已加载节点。
    // 若不展开，折叠层级的子节点不会被设置勾选状态
    std::function<void(TreeItem *)> expandAll = [&](TreeItem *item) {
        loadChildren(item);
        for (int i = 0; i < item->childCount(); ++i) {
            expandAll(item->child(i));
        }
    };
    expandAll(m_rootItem);

    // 设置 m_updatingCheckState 标志，使 setChildrenCheckState 内部
    // 的 dataChanged 发射走快速通道，不会递归触发 setData 联动
    m_updatingCheckState = true;
    setChildrenCheckState(m_rootItem, Qt::Checked);
    m_updatingCheckState = false;
    // setChildrenCheckState 内部已逐节点 emit dataChanged，无需 resetModel 刷新
}

/**
 * @brief 将内容树中所有节点取消全选
 * @details 不需要像 selectAll 那样先 expandAll：未加载节点的默认状态就是 Unchecked，
 *          未来被展开时会自动呈现未勾选状态，因此只需处理已加载节点
 */
void InfoModel::deselectAll()
{
    m_updatingCheckState = true;
    setChildrenCheckState(m_rootItem, Qt::Unchecked);
    m_updatingCheckState = false;
    // setChildrenCheckState 内部已逐节点 emit dataChanged，无需 resetModel 刷新
}

/**
 * @brief 收集已勾选的内容节点路径
 * @return 返回路径字符串列表，格式为 "节点名/子节点名/..."
 * @details 仅遍历已加载的节点，未展开的子孙节点不会被收集
 */
QStringList InfoModel::checkedPaths()
{
    QStringList result;
    collectCheckedPaths(m_rootItem, "", result);
    return result;
}

/**
 * @brief 从数据库加载指定内容节点的子节点
 * @param[in] parentItem 父节点指针
 * @details 仅查询 NODE_TYPE_CONTENT 类型的子节点（内容节点下只有内容子节点），
 *          通过 beginInsertRows/endInsertRows 通知视图
 */
void InfoModel::loadChildren(TreeItem *parentItem)
{
    if (parentItem->isLoaded()) {
        return;
    }

    auto list = DataManager::instance().queryChildren(parentItem->id(), {NODE_TYPE_CONTENT});
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

/**
 * @brief 将 TreeItem 指针转换为 QModelIndex
 * @param[in] item 树节点指针，若为 m_rootItem 则返回无效索引
 * @return 返回对应的模型索引
 */
QModelIndex InfoModel::indexFromItem(TreeItem *item) const
{
    if (item == m_rootItem) {
        return QModelIndex();
    }
    return createIndex(item->row(), 0, item);
}

/**
 * @brief 递归设置所有子孙节点的勾选状态
 * @param[in] parent 父节点指针
 * @param[in] state 目标勾选状态
 * @details 遍历 parent 的所有子节点：若未加载则先 loadChildren；
 *          设置 checkState 后立即 emit dataChanged 通知视图刷新该节点；
 *          然后递归处理子节点的子节点。
 *          调用方需先设置 m_updatingCheckState=true，防止 dataChanged 触发 setData 递归
 */
void InfoModel::setChildrenCheckState(TreeItem *parent, Qt::CheckState state)
{
    for (int i = 0; i < parent->childCount(); ++i) {
        TreeItem *child = parent->child(i);
        // 若节点未展开则先加载——保证无论是否可见，子孙状态都被同步
        if (!child->isLoaded()) {
            loadChildren(child);
        }

        child->setCheckState(state);

        // 逐个 emit dataChanged，视图才能刷新该节点的勾选框
        QModelIndex childIdx = indexFromItem(child);
        if (childIdx.isValid()) {
            emit dataChanged(childIdx, childIdx, {Qt::CheckStateRole});
        }

        setChildrenCheckState(child, state);
    }
}

/**
 * @brief 向上递归更新祖先节点的勾选状态
 * @param[in] child 子节点指针
 * @details 从 child.parent() 开始向上遍历到 m_rootItem：
 *          - 统计各祖先的所有子节点勾选状态
 *          - 全部 Checked → 祖先 = Checked
 *          - 全部 Unchecked → 祖先 = Unchecked
 *          - 混合 → 祖先 = PartiallyChecked
 *          状态变化时 emit dataChanged 通知视图
 */
void InfoModel::updateAncestorCheckStates(TreeItem *child)
{
    TreeItem *ancestor = child->parent();
    while (ancestor && ancestor != m_rootItem) {
        int checkedCount = 0;
        int uncheckedCount = 0;

        // 遍历祖先的所有直接子节点，统计勾选状态分布
        for (int i = 0; i < ancestor->childCount(); ++i) {
            TreeItem *sibling = ancestor->child(i);
            switch (sibling->checkState()) {
            case Qt::Checked:
                checkedCount++;
                break;
            case Qt::Unchecked:
                uncheckedCount++;
                break;
            default:
                // PartiallyChecked 不计入任何计数，最终会归为混合状态
                break;
            }
        }

        // 三态判定：全 Checked→Checked，全 Unchecked→Unchecked，否则 PartiallyChecked
        Qt::CheckState newState;
        if (checkedCount == ancestor->childCount()) {
            newState = Qt::Checked;
        } else if (uncheckedCount == ancestor->childCount()) {
            newState = Qt::Unchecked;
        } else {
            newState = Qt::PartiallyChecked;
        }

        // 仅在状态变化时更新并通知，避免不必要的视图刷新
        if (ancestor->checkState() != newState) {
            ancestor->setCheckState(newState);
            QModelIndex idx = indexFromItem(ancestor);
            if (idx.isValid()) {
                emit dataChanged(idx, idx, {Qt::CheckStateRole});
            }
        }

        ancestor = ancestor->parent();
    }
}

/**
 * @brief 递归收集已勾选节点的路径
 * @param[in] item 当前遍历的节点
 * @param[in] parentPath 父路径前缀（以 "/" 分隔上级节点名）
 * @param[out] result 输出结果列表
 * @details 仅收集 Checked 或 PartiallyChecked 的节点，不递归未加载的子树
 */
void InfoModel::collectCheckedPaths(TreeItem *item, const QString &parentPath, QStringList &result)
{
    for (int i = 0; i < item->childCount(); ++i) {
        TreeItem *child = item->child(i);
        QString curPath = parentPath.isEmpty()
            ? child->name()
            : parentPath + "/" + child->name();

        if (child->checkState() == Qt::Checked || child->checkState() == Qt::PartiallyChecked) {
            result << curPath;
        }
        // 若节点未展开则先加载——确保未展开子树中的勾选节点也被收集
        if (!child->isLoaded()) {
            loadChildren(child);
        }
        if (child->childCount() > 0) {
            collectCheckedPaths(child, curPath, result);
        }
    }
}

// ============================================================
// Tree 主窗口实现
// ============================================================

/**
 * @brief 构造 Tree 窗口
 * @param[in] parent 父窗口指针
 * @details 初始化 UI 布局，创建 ClassModel 绑定到 treeView_Class，
 *          创建 InfoModel 绑定到 treeView_Info，隐藏表头使树形控件呈现简洁外观，
 *          连接所有按钮的 clicked 信号到对应槽函数
 */
Tree::Tree(QWidget *parent)
    : QWidget(parent)
    , m_classModel(nullptr)
    , m_infoModel(nullptr)
{
    LOG_INFO(g_logger) << "Tree 开始构造";

    m_ui.setupUi(this);

    // 左侧：部门分类树 — 显示部门层级 + 最底层分类叶子节点
    m_classModel = new ClassModel(this);
    m_ui.treeView_Class->setModel(m_classModel);
    m_ui.treeView_Class->setHeaderHidden(true);

    // 右侧：内容树 — 初始为空，点击分类后加载对应内容
    m_infoModel = new InfoModel(this);
    m_ui.treeView_Info->setModel(m_infoModel);
    m_ui.treeView_Info->setHeaderHidden(true);

    // 信号连接：树点击 + 6 个按钮全部使用新式 connect 语法
    connect(m_ui.treeView_Class, &QTreeView::clicked,
            this, &Tree::onCategoryClicked);
    connect(m_ui.pushButton_Add, &QPushButton::clicked,
            this, &Tree::onAdd);
    connect(m_ui.pushButton_AllSelect, &QPushButton::clicked,
            this, &Tree::onSelectAll);
    connect(m_ui.pushButton_CancelSelect, &QPushButton::clicked,
            this, &Tree::onDeselectAll);
    connect(m_ui.pushButton_Reset, &QPushButton::clicked,
            this, &Tree::onReset);
    connect(m_ui.pushButton_OK, &QPushButton::clicked,
            this, &Tree::onOK);
    connect(m_ui.pushButton_Cancel, &QPushButton::clicked,
            this, &Tree::onCancel);
}

/**
 * @brief 析构 Tree 窗口
 * @details Qt 父子对象树自动回收子控件及 Model，无需手动释放
 */
Tree::~Tree()
{
}

// ---- 槽函数 ----

/**
 * @brief 响应 treeView_Class 中节点的点击
 * @param[in] index 被点击节点的模型索引
 * @details 仅处理分类节点（NODE_TYPE_CATEGORY）；
 *          点击部门节点时不触发任何操作；
 *          点击分类节点后调用 InfoModel::loadCategory 加载内容树
 */
void Tree::onCategoryClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        LOG_INFO(g_logger) << "onCategoryClicked: invalid index";
        return;
    }

    TreeItem *item = static_cast<TreeItem *>(index.internalPointer());

    if (!item || item->nodeType() != NODE_TYPE_CATEGORY) {
        LOG_DEBUG(g_logger) << "onCategoryClicked: skipped (nodeType != NODE_TYPE_CATEGORY)";
        return;
    }

    m_infoModel->loadCategory(item->id(), item->name());
    LOG_INFO(g_logger) << "加载分类: " << item->name() << " (id=" << item->id() << ")";

    // 内容树默认全部展开：expandAll 会触发 canFetchMore/fetchMore 链式懒加载
    m_ui.treeView_Info->expandAll();
}

/**
 * @brief 响应 pushButton_Add：将 treeView_Info 中勾选的内容以树状文本追加到 textEdit
 * @details 若未加载任何分类则直接返回；
 *          调用 formatCheckedTree 构建 ASCII 树形文本，
 *          再通过 appendToTextEdit 追加（同一分类自动去重替换）
 */
void Tree::onAdd()
{
    if (m_infoModel->categoryId() == 0) {
        return;
    }

    QString treeText = formatCheckedTree();
    if (treeText.isEmpty()) {
        return;
    }

    appendToTextEdit(m_infoModel->categoryName(), treeText);
}

/**
 * @brief 响应 pushButton_AllSelect：将 treeView_Info 中所有节点全选
 */
void Tree::onSelectAll()
{
    m_infoModel->selectAll();
}

/**
 * @brief 响应 pushButton_CancelSelect：将 treeView_Info 中所有节点取消全选
 */
void Tree::onDeselectAll()
{
    m_infoModel->deselectAll();
}

/**
 * @brief 响应 pushButton_Reset：清空 textEdit 内容
 */
void Tree::onReset()
{
    m_ui.textEdit->clear();
}

/**
 * @brief 响应 pushButton_OK：通过日志系统输出 textEdit 内容到控制台
 * @details 使用 LOG_INFO 将 textEdit 的纯文本内容输出，
 *          格式为 "=== 选用的信息 ===\n" + 内容
 */
void Tree::onOK()
{
    QString content = m_ui.textEdit->toPlainText();
    LOG_INFO(g_logger) << "=== 选用的信息 ===\n" << content;
}

/**
 * @brief 响应 pushButton_Cancel：关闭窗口
 */
void Tree::onCancel()
{
    this->close();
}

// ---- 私有辅助方法 ----

/**
 * @brief 将 treeView_Info 中已勾选的内容格式化为 ASCII 树状文本
 * @return 返回格式化的树形文本，无勾选内容时返回空字符串
 * @details 仅通过 QAbstractItemModel 公有 API（data/rowCount/index）遍历内容树，
 *          不直接访问 InfoModel 内部指针。
 *          使用 ├── └── │ 等 ASCII 字符构建树形缩进。
 *          仅输出 Checked 或 PartiallyChecked 的节点及其勾选子孙。
 */
QString Tree::formatCheckedTree() const
{
    // 递归 lambda：全部通过 QAbstractItemModel 公有 API 遍历，不触及 InfoModel 内部指针。
    // 使用 std::function 包装才能支持 lambda 递归（auto 推导无法引用自身）。
    // 这是 C++ lambda 递归的标准写法，并非代码异味。
    std::function<QString(const QModelIndex &, const QString &, bool)> formatNode;
    formatNode = [&](const QModelIndex &idx, const QString &indent, bool isLast) -> QString {
        QString name = m_infoModel->data(idx, Qt::DisplayRole).toString();
        QString branch = isLast ? "└── " : "├── ";
        QString line = indent + branch + name + "\n";

        // 收集该节点下所有勾选或半选的子节点
        int rows = m_infoModel->rowCount(idx);
        QList<QModelIndex> checkedChildren;
        for (int i = 0; i < rows; ++i) {
            QModelIndex childIdx = m_infoModel->index(i, 0, idx);
            Qt::CheckState state = m_infoModel->data(
                childIdx, Qt::CheckStateRole).value<Qt::CheckState>();
            if (state != Qt::Unchecked) {
                checkedChildren.append(childIdx);
            }
        }

        // 递归格式化子节点，最后一个子节点用 └── 前缀
        QString childIndent = indent + (isLast ? "    " : "│   ");
        for (int i = 0; i < checkedChildren.size(); ++i) {
            bool lastChild = (i == checkedChildren.size() - 1);
            line += formatNode(checkedChildren[i], childIndent, lastChild);
        }
        return line;
    };

    // 根级别：遍历 treeView_Info 的顶层内容节点
    int rootRows = m_infoModel->rowCount(QModelIndex());
    QList<QModelIndex> topChecked;
    for (int i = 0; i < rootRows; ++i) {
        QModelIndex idx = m_infoModel->index(i, 0, QModelIndex());
        Qt::CheckState state = m_infoModel->data(
            idx, Qt::CheckStateRole).value<Qt::CheckState>();
        if (state != Qt::Unchecked) {
            topChecked.append(idx);
        }
    }
    if (topChecked.isEmpty()) {
        return QString();
    }

    QString result;
    for (int i = 0; i < topChecked.size(); ++i) {
        bool last = (i == topChecked.size() - 1);
        result += formatNode(topChecked[i], "    ", last);
    }
    return result;
}

/**
 * @brief 将分类的树状文本追加到 textEdit（同一分类去重替换）
 * @param[in] categoryName 分类名称
 * @param[in] treeText 格式化后的树状文本
 * @details textEdit 中以 "[分类名称]" 行标记每个分类块的起始位置。
 *          若已存在同名分类块，先删除旧块再在末尾追加新块。
 *          块之间以空行分隔。
 */
void Tree::appendToTextEdit(const QString &categoryName, const QString &treeText)
{
    QString fullText = m_ui.textEdit->toPlainText();
    QString header = "[" + categoryName + "]\n";

    // 去重：查找 "[categoryName]\n" 标记的旧块
    // 块边界：从 header 开始到下一个 "\n["（下一分类块）或文末
    int headerPos = fullText.indexOf(header);
    if (headerPos >= 0) {
        int blockEnd = fullText.indexOf("\n[", headerPos + header.length());
        if (blockEnd < 0) {
            blockEnd = fullText.length();
        }

        fullText.remove(headerPos, blockEnd - headerPos);
        // 移除旧块后可能留下连续空行，清理一个多余的 \n
        if (fullText.endsWith("\n\n")) {
            fullText.chop(1);
        }
    }

    // 追加新块（与前文用空行分隔）
    if (!fullText.isEmpty() && !fullText.endsWith("\n")) {
        fullText += "\n";
    }
    if (!fullText.isEmpty()) {
        fullText += "\n";
    }
    fullText += header + treeText;

    m_ui.textEdit->setPlainText(fullText);
}
