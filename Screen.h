#ifndef SCREEN_H
#define SCREEN_H

#include <QWidget>

class Screen : public QWidget
{
    Q_OBJECT
public:
    explicit Screen(QWidget *parent = 0);

private:
    void paintEvent(QPaintEvent *event);

signals:

public slots:

};

#endif // SCREEN_H
