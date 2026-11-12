#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QTimer>
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE
class VideoDecodeThread;

// 主窗口负责显示视频帧，并管理解码线程的启动、停止和重连。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    // 解码线程对象由主窗口创建，视频处理在该线程的 run() 中执行。
    VideoDecodeThread *m_decoder;
    // 单次重连定时器到期后，主窗口尝试重新启动解码线程。
    QTimer m_reconnectTimer;
    // 手动停止时，解码线程退出后直接恢复按钮状态。
    bool m_manualStop = false;
    // 等待重连期间，开始按钮用于取消本次重连。
    bool m_waitingReconnect = false;
};
#endif // MAINWINDOW_H
