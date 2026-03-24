#include <QPainter>

#include "Screen.h"

Screen::Screen(QWidget *parent) :
    QWidget(parent)
{
}


void Screen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::black, 12, Qt::DashDotLine, Qt::RoundCap));
    painter.setBrush(QBrush(Qt::green, Qt::SolidPattern));
    painter.drawEllipse(80,80,400,240);
}

