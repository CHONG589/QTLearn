#pragma once

#include <QAbstractItemModel>
#include <QStringList>
#include <QStringListModel>
#include <QListView>
#include <QTableView>

#include <QtWidgets/QWidget>
#include "ui_Test.h"

class StringListModel : public QAbstractListModel {
    Q_OBJECT

public:
    StringListModel(const QStringList &strings, QObject *parent = nullptr)
        : QAbstractListModel(parent), stringList(strings) {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    bool insertRows(int position, int rows, const QModelIndex &index = QModelIndex()) override;
    bool removeRows(int position, int rows, const QModelIndex &index = QModelIndex()) override;

private:
    QStringList stringList;
};

