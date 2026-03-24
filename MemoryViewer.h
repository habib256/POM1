#ifndef MEMORYVIEWER_H
#define MEMORYVIEWER_H

#include <QWidget>
#include <QTableWidget>

#include "Memory.h"

class MemoryViewer : public QWidget
{
    Q_OBJECT
public:
    explicit MemoryViewer(QWidget *parent = 0);

    void populateTable(quint16 address,quint8 value);

private:

     QTableWidget *table;

     void createTable(void);

signals:

public slots:

};

#endif // MEMORYVIEWER_H
