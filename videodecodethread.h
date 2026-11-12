#ifndef VIDEODECODETHREAD_H
#define VIDEODECODETHREAD_H
#include <QThread>
#include <QImage>
#include <QString>
#include <atomic>
// 工作线程负责读取媒体、解码视频，并把 RGB 图像交给界面。
class VideoDecodeThread : public QThread
{
    Q_OBJECT
public:
    VideoDecodeThread(QObject *parent = nullptr);
    // 在启动线程前保存本次播放使用的地址。
    void prepare(const QString &url);
    // 界面收到并处理图像后，清除待显示标志。
    void frameConsumed();
protected:
    void run();
signals:
    // 通过信号把解码后的图像交给接收方。
    void frameReady(const QImage &image);
private:
   QString m_url;
   // 由界面线程和解码线程共享，限制正常解码时待显示的帧数。
   std::atomic<bool> m_framePending{false};
};

#endif