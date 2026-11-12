#include "mainwindow.h"

#include <QApplication>

// 创建 Qt 应用和主窗口，再进入事件循环处理界面操作。
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
