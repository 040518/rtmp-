#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "videodecodethread.h"
#include <QInputDialog>
#include <QPixmap>
#include <QTimer>
// 保存当前播放地址，自动重连时复用该地址。
QString url;
bool ok = false;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_decoder = new VideoDecodeThread(this);
    // 通过队列连接把图像交给 GUI 线程缩放和显示。
    connect(m_decoder, &VideoDecodeThread::frameReady,
        this, [this](const QImage &image)
    {
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap = pixmap.scaled(
            ui->statusLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
        ui->statusLabel->setPixmap(pixmap);
        // 界面处理完这一帧后，通知解码线程可以投递新帧。
        m_decoder->frameConsumed();
        static quint64 monitorDisplayed = 0;
        if (qEnvironmentVariableIsSet("VIDEOMONITOR_MONITOR") && ++monitorDisplayed % 250 == 0)
            qDebug() << "MONITOR_GUI" << monitorDisplayed;
    }, Qt::QueuedConnection
    );
    // 解码结束后，根据退出原因恢复按钮或安排重连。
    connect(m_decoder, &QThread::finished,
        this, [this]()
    {
        qDebug() << "视频解码线程已结束";
        if (m_manualStop)
        {
            ui->startButton->setText("开始监控");
            ui->startButton->setEnabled(true);
            return;
        }
        qDebug() << "RTSP 连接中断，2 秒后重连";
        // 非手动退出时，等待两秒后尝试重连。
        m_waitingReconnect = true;
        ui->startButton->setText("取消重连");
        ui->startButton->setEnabled(true);
        m_reconnectTimer.start(2000);
    }, Qt::QueuedConnection);
    m_reconnectTimer.setSingleShot(true);

    connect(&m_reconnectTimer, &QTimer::timeout,
            this, [this]()
    {
        if (m_manualStop)
        {
            return;
        }

        if (m_waitingReconnect == false)
        {
            return;
        }

        // 定时器到期时若线程已在运行，就跳过这次启动。
        if (m_decoder->isRunning())
        {
            return;
        }

        m_waitingReconnect = false;

        qDebug() << "正在重新连接 RTSP";

        m_decoder->prepare(url);
        m_decoder->start();

        ui->startButton->setText("停止播放");
        ui->startButton->setEnabled(true);
    });
    connect(ui->startButton,
            &QPushButton::clicked,
            [this]()
            {
                // 判断当前是否正在等待自动重连
                if (m_waitingReconnect)
                {
                    m_reconnectTimer.stop();
                    m_waitingReconnect = false;
                    m_manualStop = true;

                    ui->startButton->setText("开始监控");

                    qDebug() << "用户取消自动重连";
                    return;
                }
                // 播放中点击按钮时，发出中断请求并等待线程自行退出。
                if (m_decoder->isRunning())
                {
                    m_manualStop = true;
                    m_decoder->requestInterruption();
                    ui->startButton->setText("正在停止...");
                    ui->startButton->setEnabled(false);
                    return;
                }
                // 线程空闲时读取用户输入的地址，确认后再启动播放。
                url = QInputDialog::getText(
                    this,
                    "输入视频地址",
                    "视频地址：",
                    QLineEdit::Normal,
                    "",
                    &ok
                );
                if (ok==false||url.trimmed().isEmpty())
                {
                    return;
                }
                ui->statusLabel->setText(url);
                m_manualStop = false;
                m_decoder->prepare(url);    //保存视频地址
                m_decoder->start();         //启动工作线程
                ui->startButton->setText("停止播放");
            }
            );

}

MainWindow::~MainWindow()
{
    m_decoder->requestInterruption();
    // 等解码线程结束并释放资源后，再销毁界面。
    m_decoder->wait();
    delete ui;
}
