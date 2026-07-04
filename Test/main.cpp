#include "test/Test.h"
#include <QtWidgets/QApplication>

#include <QSplitter>
#include <QFileSystemModel>
#include <QTreeView>
#include <QDir>
#include <QListView>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    //Test window;
    //window.show();

    QSplitter *splitter = new QSplitter(Qt::Horizontal);

    QFileSystemModel *model = new QFileSystemModel();
    model->setRootPath(QDir::currentPath());

    QTreeView *tree = new QTreeView(splitter);
    tree->setModel(model);
    tree->setRootIndex(model->index(QDir::currentPath()));

    QListView *list = new QListView(splitter);
    list->setModel(model);
    list->setRootIndex(model->index(QDir::currentPath()));

    splitter->show();

    return app.exec();
}
