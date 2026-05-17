#include "Tree.h"

static zch::Logger::ptr g_logger = LOG_NAME("default");

Tree::Tree(QWidget *parent)
	: QWidget(parent)
{
	LOG_INFO(g_logger) << "Tree 开始构造";

	ui.setupUi(this);

	TreeModel *model = new TreeModel(this);

	ui.treeView->setModel(model);
	ui.treeView->setHeaderHidden(true);
}

Tree::~Tree()
{

}

DataManager &DataManager::instance() {
	static DataManager inst;
	return inst;
}

QList<Node> DataManager::queryChildren(int parentId) {
	QList<Node> list;

	try {
		ScopedConn conn;

		QString sql = (parentId == 0)
			? "SELECT id,name,type FROM tree_nodes WHERE parent_id IS NULL"
			: "SELECT id,name,type FROM tree_nodes WHERE parent_id=?";

		QSqlQuery query = conn->prepare(sql);

		if (parentId != 0) {
			query.addBindValue(parentId);
		}

		conn->execPrepared(query, {});

		while (query.next()) {
			list.append({
				query.value(0).toInt(),
				query.value(1).toString(),
				query.value(2).toInt()
				});
		}

	} catch (...) {
		LOG_ERROR(g_logger) << "queryChildren error";
	}

	return list;
}

bool DataManager::insertNode(QString name, int type, int parentId, int &newId) {
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

bool DataManager::updateName(int id, QString name) {
	
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

bool DataManager::deleteNode(int id) {

	try {
		ScopedConn conn;

		QString sql = "DELETE FROM tree_nodes WHERE id=?";
		conn->execPrepared(sql, {id});

		return true;
	} catch (const DBException &e) {
		LOG_ERROR(g_logger) << "delete id failed:" << e.what();
		return false;
	}
}

bool DataManager::hasChildren(int parentId) {
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

TreeItem::TreeItem(int id, const QString &name, int type, TreeItem *parent)
	: m_id(id),
	m_name(name),
	m_type(type),
	m_parent(parent),
	m_loaded(false) {
}

TreeItem::~TreeItem() {
	qDeleteAll(m_children);
}

//////////////////////////////////////////////////////////////
// 子节点管理
void TreeItem::appendChild(TreeItem *child) {
	m_children.append(child);
}

TreeItem *TreeItem::child(int row) const {
	return m_children.value(row);
}

TreeItem *TreeItem::takeChild(int row) {
	if (row < 0 || row >= m_children.size()) {
		LOG_WARN(g_logger) << row << " 溢出：0 <= x < " << m_children.size();
        return nullptr;
	}

	return m_children.takeAt(row);
}

int TreeItem::childCount() const {
	return m_children.size();
}

//////////////////////////////////////////////////////////////
// 行号（在父节点中的位置）
int TreeItem::row() const {
	if (m_parent) {
		return m_parent->m_children.indexOf(const_cast<TreeItem *>(this));
	}

	return 0;
}

TreeItem *TreeItem::parent() const {
	return m_parent;
}

//////////////////////////////////////////////////////////////
// 数据访问
int TreeItem::id() const {
	return m_id;
}

QString TreeItem::name() const {
	return m_name;
}

void TreeItem::setName(const QString &name) {
	m_name = name;
}

int TreeItem::type() const {
	return m_type;
}

//////////////////////////////////////////////////////////////
// 懒加载标记
bool TreeItem::isLoaded() const {
	return m_loaded;
}

void TreeItem::setLoaded(bool loaded) {
	m_loaded = loaded;
}

TreeModel::TreeModel(QObject *parent)
	: QAbstractItemModel(parent) {

	// ⭐ 虚拟 root（不显示）
	rootItem = new TreeItem(0, "", 0, nullptr);

	// ⭐ 直接加载数据库 root（公司项目总览）
	auto list = DataManager::instance().queryChildren(0); // parent_id IS NULL

	for (auto &dto : list) {
		auto *child = new TreeItem(dto.id, dto.name, dto.type, rootItem);
		rootItem->appendChild(child);
	}

	rootItem->setLoaded(true);
}

TreeModel::~TreeModel() {
	if (rootItem) {
		delete rootItem;
		rootItem = nullptr;
	}
}

//////////////////////////////////////////////////////////////
// index —— 创建索引（核心）
QModelIndex TreeModel::index(int row, int column, const QModelIndex &parent) const {
	if (!hasIndex(row, column, parent)) {
		return QModelIndex();
	}

	TreeItem *parentItem = parent.isValid()
		? static_cast<TreeItem *>(parent.internalPointer())
		: rootItem;

	TreeItem *child = parentItem->child(row);

	if (child) {
		return createIndex(row, column, child);
	}

	return QModelIndex();
}

//////////////////////////////////////////////////////////////
// parent —— 构建树关系（核心）
QModelIndex TreeModel::parent(const QModelIndex &index) const {
	if (!index.isValid()) {
		return QModelIndex();
	}

	TreeItem *child = static_cast<TreeItem *>(index.internalPointer());
	TreeItem *parentItem = child->parent();

	if (!parentItem || parentItem == rootItem) {
		return QModelIndex();
	}

	return createIndex(parentItem->row(), 0, parentItem);
}

//////////////////////////////////////////////////////////////
// 行数 —— 驱动加载（关键）
int TreeModel::rowCount(const QModelIndex &parent) const {
	TreeItem *parentItem = parent.isValid()
		? static_cast<TreeItem *>(parent.internalPointer())
		: rootItem;

	return parentItem->childCount();
}

//////////////////////////////////////////////////////////////
// 列数
int TreeModel::columnCount(const QModelIndex &) const {
	return 1;
}

//////////////////////////////////////////////////////////////
// 数据 —— UI 显示内容
QVariant TreeModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid()) {
		return QVariant();
	}

	TreeItem *item = static_cast<TreeItem *>(index.internalPointer());

	if (role == Qt::DisplayRole) {
		if (item == rootItem) {
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

//////////////////////////////////////////////////////////////
// 是否可编辑
Qt::ItemFlags TreeModel::flags(const QModelIndex &index) const {
	if (!index.isValid()) {
		return Qt::NoItemFlags;
	}

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

//////////////////////////////////////////////////////////////
// 修改数据（重命名）
bool TreeModel::setData(const QModelIndex &index, const QVariant &value, int role) {
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

//////////////////////////////////////////////////////////////
// ⭐ 是否还能加载（懒加载关键）
bool TreeModel::canFetchMore(const QModelIndex &parent) const {
	TreeItem *item = parent.isValid()
		? static_cast<TreeItem *>(parent.internalPointer())
		: rootItem;

	return !item->isLoaded();
}

//////////////////////////////////////////////////////////////
// ⭐ 加载数据（真正访问数据库）
void TreeModel::fetchMore(const QModelIndex &parent) {
	TreeItem *item = parent.isValid()
		? static_cast<TreeItem *>(parent.internalPointer())
		: rootItem;

	loadChildren(item);
}

//////////////////////////////////////////////////////////////
// ⭐ 从数据库加载子节点（核心）
void TreeModel::loadChildren(TreeItem *parentItem) {
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

//////////////////////////////////////////////////////////////
// TreeItem → QModelIndex
QModelIndex TreeModel::indexFromItem(TreeItem *item) const {
	if (item == rootItem) {
		return QModelIndex();
	}

	return createIndex(item->row(), 0, item);
}

//////////////////////////////////////////////////////////////
// 添加节点
void TreeModel::addNode(const QModelIndex &parentIdx, const QString &name, int type) {
	TreeItem *parentItem = parentIdx.isValid()
		? static_cast<TreeItem *>(parentIdx.internalPointer())
		: rootItem;

	int newId = -1;
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

//////////////////////////////////////////////////////////////
// 删除节点
void TreeModel::removeNode(const QModelIndex &index) {
	if (!index.isValid()) {
		return;
	}

	TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
	TreeItem *parentItem = item->parent();

	int row = item->row();

	if (!DataManager::instance().deleteNode(item->id())) {
		return;
	}

	beginRemoveRows(index.parent(), row, row);

	delete parentItem->takeChild(row); // 你在 TreeItem 里实现

	endRemoveRows();
}

bool TreeModel::hasChildren(const QModelIndex &parent) const {
	TreeItem *item = parent.isValid()
		? static_cast<TreeItem *>(parent.internalPointer())
		: rootItem;

	// 如果已经加载过，直接看缓存
	if (item->isLoaded()) {
		return item->childCount() > 0;
	}

	// ⭐ 没加载 → 去数据库判断
	return DataManager::instance().hasChildren(item->id());
}

