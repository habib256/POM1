#include <QPainter>
#include <QLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QString>

#include "MemoryViewer.h"

MemoryViewer::MemoryViewer(QWidget *parent) :
    QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout;

    createTable();

    layout->addWidget(table);
    setLayout(layout);
}
void MemoryViewer::populateTable(quint16 address,quint8 value)
{
    QTableWidgetItem *item = new QTableWidgetItem;
    item->setText(QString("%1").arg(value, 0, 16).toUpper());
    table->setItem(address/8,address%8,item);
}

void MemoryViewer::createTable(void)
{
    int memSize = 64;
    int fontSize = 8;
    table = new QTableWidget(this);
    table->setRowCount(0);
    table->setColumnCount(0);
    table->setRowCount(memSize*1024/8);
    table->setColumnCount(8);
    table->setFont(QFont("Times",fontSize));

    for (unsigned int i=0; i < memSize*1024/8; ++i) {
        table->setColumnWidth(i, 23);
    }


    for ( int i=0; i < 8; ++i) {
        QTableWidgetItem *item = new QTableWidgetItem;
        item->setFont(QFont("Times",fontSize));
        table->setHorizontalHeaderItem(i,item);
    }

    for ( int i=0; i < memSize*1024/8; ++i) {
        QTableWidgetItem *item = new QTableWidgetItem;
        item->setText(QString("%1").arg((i*8), 0, 16).toUpper());
        table->setVerticalHeaderItem(i,item);
    }
}
