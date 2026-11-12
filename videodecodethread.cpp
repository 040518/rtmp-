#include "videodecodethread.h"
#include <QDebug>
#include <cerrno>
#include <QByteArray>
#include <QImage>
#include <QElapsedTimer>
// FFmpeg 头文件声明的是 C API，在 C++ 中按 C 链接约定引用。
extern "C" {
#include <libavutil/pixdesc.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/dict.h>
}
VideoDecodeThread::VideoDecodeThread(QObject *parent):QThread(parent)
{

}
void VideoDecodeThread::run()
{
    int ret = 0;
    qDebug() << "视频地址：" << m_url;
    AVFormatContext *fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) 
    {
        qDebug() << "无法分配媒体上下文";
        return;
    }
    // FFmpeg 在阻塞 I/O 期间检查停止请求，以便退出读取。
    fmt_ctx->interrupt_callback.callback = [](void *opaque) -> int {
        return static_cast<VideoDecodeThread *>(opaque)->isInterruptionRequested() ? 1 : 0;
    };
    fmt_ctx->interrupt_callback.opaque = this;
    QByteArray urlBytes = m_url.toUtf8();
    AVDictionary *options = nullptr;
    // RTSP 使用 TCP 传输，其他输入沿用 FFmpeg 默认选项。
    if(m_url.startsWith("rtsp://"))
    {
        av_dict_set(&options,"rtsp_transport","tcp",0);
        qDebug() <<"当前使用 RTSP TCP 拉流";
    }
    ret = avformat_open_input(&fmt_ctx,urlBytes.constData(),nullptr,&options);
    av_dict_free(&options);
    if(ret<0)
    {
        qDebug() <<"打开视频失败，错误码：" << ret;
        avformat_close_input(&fmt_ctx);
        return ;
    }
    qDebug() << "打开视频成功";
    // 探测媒体中的流及其参数，供后续选择视频流使用。
    ret = avformat_find_stream_info(fmt_ctx,nullptr);
    if(ret<0)
    {
        qDebug() <<"读取视频流失败";
        avformat_close_input(&fmt_ctx);
        return ;
    }
    qDebug() << "视频文件中的流数量：" << fmt_ctx->nb_streams;
    int videoIndex,audioIndex; 
    // 选择用于播放的视频流，后面的解码循环只向解码器提交该流的包。
    videoIndex = av_find_best_stream(fmt_ctx,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    if (videoIndex < 0)
    {
        qDebug() << "没有找到视频流";
        avformat_close_input(&fmt_ctx);
        return;
    }
    qDebug() << "视频流索引：" << videoIndex; 
    AVStream *videoStream = fmt_ctx->streams[videoIndex];
    AVCodecParameters *codecParams = videoStream->codecpar;
    qDebug() << "视频宽度：" << codecParams->width;
    qDebug() << "视频高度：" << codecParams->height;
    qDebug() << "视频编码ID：" << codecParams->codec_id;
    const AVCodec *decoder = avcodec_find_decoder(codecParams->codec_id);
    if (decoder == nullptr)
    {
        qDebug() << "没有找到对应的视频解码器";
        avformat_close_input(&fmt_ctx);
        return;
    }

    qDebug() << "找到视频解码器：" << decoder->name;
    AVCodecContext *codecCtx = avcodec_alloc_context3(decoder);
    if(codecCtx == nullptr)
    {
        qDebug() << "创建解码器上下文失败";
        avformat_close_input(&fmt_ctx);
        return;
    }
    qDebug() << "创建解码器上下文成功";
    // 把流中的编码参数复制到解码器上下文，再打开解码器。
    ret = avcodec_parameters_to_context(codecCtx, codecParams);
    if (ret < 0)
    {
        qDebug() << "复制编码参数失败";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    qDebug() << "复制编码参数成功";
    ret = avcodec_open2(codecCtx,decoder,nullptr);
    if (ret < 0)
    {
        qDebug() << "打开解码器失败";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt_ctx);
        return;
    }
    qDebug() << "打开解码器成功";
    // AVPacket 保存压缩数据，AVFrame 接收解码得到的像素数据。
    AVPacket *packet = av_packet_alloc();
    if(packet==nullptr)
    {
        qDebug() << "创建 AVPacket 失败";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt_ctx);
        return;
    }
    qDebug() << "创建 AVPacket 成功";
    AVFrame *frame = av_frame_alloc();
    if (frame == nullptr)
    {
        qDebug() << "创建 AVFrame 失败";
        av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt_ctx);
        return;
    }
    qDebug() << "创建 AVFrame 成功";
    // 建立像素转换上下文，将解码帧转换为界面使用的 RGB24。
    SwsContext *swsCtx = sws_getContext(
        codecParams->width,
        codecParams->height,
        AV_PIX_FMT_YUV420P,

        codecParams->width,
        codecParams->height,
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );
    if (swsCtx == nullptr)
    {
        qDebug() << "创建图像转换器失败";

        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    qDebug() << "创建图像转换器成功";
    AVFrame *rgbFrame = av_frame_alloc();
    if (rgbFrame == nullptr)
    {
        qDebug() << "创建 RGB AVFrame 失败";

        sws_freeContext(swsCtx);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt_ctx);
        return;
    }
    rgbFrame->format = AV_PIX_FMT_RGB24;
    rgbFrame->width = codecParams->width;
    rgbFrame->height = codecParams->height;
    // 分配可复用的 RGB 像素缓冲区，后续帧会写入同一块内存。
    ret = av_frame_get_buffer(rgbFrame, 32);
    if (ret < 0)
    {
        qDebug() << "分配 RGB 图像内存失败：" << ret;

        av_frame_free(&rgbFrame);
        sws_freeContext(swsCtx);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    qDebug() << "RGB 图像缓冲区创建成功";
    int frameCount = 0;
    // 用首个有效视频时间戳和本地计时器建立播放时间基准。
    QElapsedTimer playClock;
    bool clockStarted = false;
    double firstPtsSeconds = 0.0;
    bool reachedEof = false;
    bool decodeFailed = false;
    // RTSP 实时流不按 PTS 睡眠，避免阻塞同一线程中的读包。
    const bool liveRtsp = m_url.startsWith("rtsp://", Qt::CaseInsensitive);
    // 逐包读取媒体，直到用户中断、输入结束或读取失败。
    while (!isInterruptionRequested())
    {
        ret = av_read_frame(fmt_ctx, packet);

        if (ret == AVERROR(EAGAIN))
        {
            QThread::msleep(10);
            continue;
        }
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                reachedEof = true;
                qDebug() << "视频文件读取结束";
            }
            else
            {
                qDebug() << "读取数据失败，错误码：" << ret;
            }
            break;
        }
        if (packet->stream_index == videoIndex)
        {
            bool packetSent = false;
            // 发送包遇到 EAGAIN 时，先取出可用的帧，再尝试送入同一包。
            while(!packetSent && !isInterruptionRequested())
            {
                ret = avcodec_send_packet(codecCtx, packet);
                if(ret==0)
                {
                    packetSent=true;
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    qDebug() << "解码器暂时无法接收，先取出解码帧";
                }
                else
                {
                    qDebug() <<"发送视频包失败：" <<ret;
                    decodeFailed = true;
                    break;
                }
                bool receivedFrame = false;
                bool receivedError = false;
                while (!isInterruptionRequested())
                {
                    ret = avcodec_receive_frame(codecCtx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    {
                        break;
                    }
                    if (ret < 0)
                    {
                        qDebug() << "接收解码帧失败：" << ret;
                        receivedError = true;
                        break;
                    }
                    receivedFrame = true;

                    // 1. 统计解码帧数
                    frameCount++;
                    if (qEnvironmentVariableIsSet("VIDEOMONITOR_MONITOR") && frameCount % 250 == 0)
                        qDebug() << "MONITOR_DECODE" << frameCount;
                    // 按实际解码帧的尺寸和像素格式更新转换上下文。
                    swsCtx = sws_getCachedContext
                    (
                    swsCtx,
                    frame->width,
                    frame->height,
                    static_cast<AVPixelFormat>(frame->format),
                    rgbFrame->width,
                    rgbFrame->height,
                    AV_PIX_FMT_RGB24,
                    SWS_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr
                );

                if (swsCtx == nullptr)
                {
                    qDebug() << "更新图像转换器失败";
                    av_frame_unref(frame);
                    receivedError = true;
                    break;
                }
                    // 2. YUV420P 转 RGB24
                    int convertRet = sws_scale(
                        swsCtx,
                        frame->data,
                        frame->linesize,
                        0,
                        frame->height,
                        rgbFrame->data,
                        rgbFrame->linesize
                    );

                    if (convertRet > 0)
                    {
                        // 3. 创建 QImage
                        QImage image(
                            rgbFrame->data[0],
                            rgbFrame->width,
                            rgbFrame->height,
                            rgbFrame->linesize[0],
                            QImage::Format_RGB888
                        );

                        // 深拷贝当前图像，使界面拿到的像素独立于会被复用的 RGB 缓冲。
                        QImage imageCopy = image.copy();

                        // 4. 根据 PTS 控制播放时间
                        int64_t framePts = frame->best_effort_timestamp;

                        if (!liveRtsp && framePts != AV_NOPTS_VALUE)
                        {
                            double currentSeconds =
                                framePts * av_q2d(videoStream->time_base);

                            if (!clockStarted)
                            {
                                firstPtsSeconds = currentSeconds;
                                playClock.start();
                                clockStarted = true;
                            }

                            qint64 targetMs = static_cast<qint64>(
                                (currentSeconds - firstPtsSeconds) * 1000.0
                            );

                            qint64 elapsedMs = playClock.elapsed();

                            qint64 waitMs = targetMs - elapsedMs;

                            // 将目标显示时间与已播放时间比较，提前解码的帧需要等待。
                            while (waitMs > 0 && !isInterruptionRequested())
                            {
                                QThread::msleep(static_cast<unsigned long>(qMin<qint64>(10, waitMs)));
                                waitMs = targetMs - playClock.elapsed();
                            }
                        }

                        // 5. 发送图像到 Qt 主线程
                        // 若界面还有一帧待处理，就跳过本帧的投递，避免显示队列积压。
                        if(!isInterruptionRequested() && m_framePending.exchange(true)==false)
                        {
                            emit frameReady(imageCopy);
                        }
                    }
                    else
                    {
                        qDebug() << "YUV 转 RGB 失败：" << convertRet;
                    }

                    // 6. 最后再释放当前帧引用
                    av_frame_unref(frame);
                }
                if(receivedError)
                {
                    decodeFailed = true;
                    break;
                }
                if (!packetSent && !receivedFrame)
                {
                    qDebug() << "无法通过取帧解除 EAGAIN，停止重试";
                    decodeFailed = true;
                    break;
                }
            }
        }
        av_packet_unref(packet);    //释放当前数据包引用
        if (decodeFailed)
        {
            break; // 当前包/解码失败后结束本次会话，不跳包继续。
        }
    }
    // 自然读到 EOF 时发送空包，取出解码器内部尚未输出的帧。
    if (reachedEof && !isInterruptionRequested())
    {
        ret = avcodec_send_packet(codecCtx, nullptr);

        if (ret < 0)
        {
            qDebug() << "刷新解码器失败：" << ret;
        }
        else
        {
            while (!isInterruptionRequested())
            {
                ret = avcodec_receive_frame(codecCtx, frame);

                if (ret == AVERROR_EOF)
                {
                    qDebug() << "解码器缓存已全部取出";
                    break;
                }

                if (ret == AVERROR(EAGAIN))
                {
                    break;
                }

                if (ret < 0)
                {
                    qDebug() << "刷新解码失败：" << ret;
                    break;
                }

                qDebug() << "刷新得到一帧："
                        << frame->width
                        << "x"
                        << frame->height;

                frameCount++;
                ret = sws_scale(
                    swsCtx,
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    rgbFrame->data,
                    rgbFrame->linesize
                );

                if (ret > 0)
                {
                    QImage image(
                        rgbFrame->data[0],
                        rgbFrame->width,
                        rgbFrame->height,
                        rgbFrame->linesize[0],
                        QImage::Format_RGB888
                    );

                    QImage imageCopy = image.copy();

                    // 获取当前视频帧的 PTS
                    int64_t framePts = frame->best_effort_timestamp;

                    if (!liveRtsp && framePts != AV_NOPTS_VALUE)
                    {
                        // 计算当前帧对应的时间（秒）
                        double currentSeconds = framePts * av_q2d(videoStream->time_base);
                        // 第一帧：启动播放计时器
                        if (!clockStarted)
                        {
                            firstPtsSeconds = currentSeconds;
                            playClock.start();
                            clockStarted = true;
                        }
                        // 计算当前帧应该显示的时间（毫秒）
                        qint64 targetMs = static_cast<qint64>(
                            (currentSeconds - firstPtsSeconds) * 1000.0
                        );

                        // 获取程序实际经过的时间
                        qint64 elapsedMs = playClock.elapsed();
                        // 计算还需要等待多久
                        qint64 waitMs = targetMs - elapsedMs;
                        while(waitMs>0)
                        {
                            if(isInterruptionRequested())
                            {
                                break;
                            }
                            QThread::msleep(10);
                            waitMs = targetMs - playClock.elapsed();
                        }
                    }
                    // 到达播放时间后，发送图像
                    if (!isInterruptionRequested() && !m_framePending.exchange(true))
                    {
                        emit frameReady(imageCopy);
                    }
                }
                else
                {
                    qDebug() << "刷新帧 RGB 转换失败：" << ret;
                }
                av_frame_unref(frame);
            }
        }
    }
    qDebug() << "总共解码成功：" << frameCount << "帧";
    // 音频流在这里仅用于记录流信息，播放流程处理的是视频。
    audioIndex = av_find_best_stream(fmt_ctx,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
    if (audioIndex >= 0)
    {
        qDebug() << "音频流索引：" << audioIndex;
        AVStream *audioStream = fmt_ctx->streams[audioIndex];
        qDebug() << "音频编码ID：" << audioStream->codecpar->codec_id;
    }
    else
    {
        qDebug() << "没有音频流，继续处理视频";
    }
    // 释放本轮播放持有的图像、解码器和媒体输入资源。
    av_frame_free(&rgbFrame);
    sws_freeContext(swsCtx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmt_ctx);
}
void VideoDecodeThread::prepare(const QString &url)
{
    m_url = url;
    m_framePending.store(false);
}
void VideoDecodeThread::frameConsumed()
{
    m_framePending.store(false);
}
