#pragma once

#include <QtWidgets/QWidget>

#include "ui_Tree.h"
#include "../log/log.h"
#include "../db/QDBConn.h"

class Tree : public QWidget
{
    Q_OBJECT

public:
    Tree(QWidget *parent = nullptr);
    ~Tree();

private:
    Ui::TreeClass ui;
};

struct Node {
	int id;
	QString name;
	int type;
};

class DataManager {
public:
	static DataManager &instance();

	QList<Node> queryChildren(int parentId);
	bool insertNode(QString name, int type, int parentId, int &newId);
	bool updateName(int id, QString name);
	bool deleteNode(int id);
	bool hasChildren(int parentId);
};

class TreeItem {
public:
	TreeItem(int id, const QString &name, int type, TreeItem *parent = nullptr);
	~TreeItem();

	// 树结构
	void appendChild(TreeItem *child);
	TreeItem *child(int row) const;
	TreeItem *takeChild(int row);   // ⭐ 删除用
	int childCount() const;
	int row() const;

	TreeItem *parent() const;

	// 数据
	int id() const;
	QString name() const;
	void setName(const QString &name);
	int type() const;

	// 懒加载
	bool isLoaded() const;
	void setLoaded(bool loaded);

private:
	int m_id;
	QString m_name;
	int m_type; // 0=folder, 1=file

	QList<TreeItem *> m_children;
	TreeItem *m_parent;

	bool m_loaded;
};

class TreeModel : public QAbstractItemModel {
	Q_OBJECT

public:
	explicit TreeModel(QObject *parent = nullptr);
	~TreeModel();

	// ⭐ 必须实现的接口
	QModelIndex index(int row, int column, const QModelIndex &parent) const override;
	QModelIndex parent(const QModelIndex &index) const override;

	int rowCount(const QModelIndex &parent) const override;
	int columnCount(const QModelIndex &parent) const override;

	QVariant data(const QModelIndex &index, int role) const override;

	// ⭐ 编辑支持
	Qt::ItemFlags flags(const QModelIndex &index) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role) override;

	// ⭐ 懒加载
	bool canFetchMore(const QModelIndex &parent) const override;
	void fetchMore(const QModelIndex &parent) override;

	// ⭐ 增删接口
	void addNode(const QModelIndex &parent, const QString &name, int type);
	void removeNode(const QModelIndex &index);

	bool hasChildren(const QModelIndex &parent) const override;

private:
	TreeItem *rootItem;

	void loadChildren(TreeItem *parentItem);
	QModelIndex indexFromItem(TreeItem *item) const;
};

