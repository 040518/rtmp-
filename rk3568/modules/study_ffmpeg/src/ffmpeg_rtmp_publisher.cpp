#include "ffmpeg_rtmp_publisher.h"
#include "v4l2_capture.h"
#include <cstdio>
#include <opencv2/core.hpp>
#include <cstring>
#include <cerrno>
extern "C"{
    #include <libavutil/time.h>
    #include <libavutil/mem.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/error.h>
    #include <libavutil/frame.h>
    #include <libavutil/pixfmt.h>
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
}
using namespace std;
namespace{
bool report_error(const char* operation,int error)
{
    char message[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, message, sizeof(message));
    fprintf(stderr, "ERROR: %s: %s (%d)\n",operation, message, error);
    return false;
}
int init_frame(AVFrame* frame,AVPixelFormat format,int width,int height)
{
    frame->format = format;
    frame->width = width;
    frame->height = height;
    return av_frame_get_buffer(frame,32);
}
}
FfmpegRtmpPublisher::FfmpegRtmpPublisher(const volatile sig_atomic_t* stop_flag)
{
    stop_flag_ = stop_flag;
}
bool FfmpegRtmpPublisher::open(int width, int height, int fps, string& url)
{
    int ret=0;
    close();
    if(width<=0||height<=0||fps<=0||url.empty()||width%2!=0||height%2!=0)
    {
        fprintf(stderr, "ERROR: invalid parameters\n");
        return false;       
    }
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (codec == nullptr)
    {
        fprintf(stderr, "ERROR: libx264 not found\n");
        return false;
    }
    //创建 FLV 输出上下文
    ret = avformat_alloc_output_context2(&format_,nullptr,"flv",url.c_str());
    if (ret < 0 || format_ == nullptr)
    {
        fprintf(stderr, "ERROR: create FLV output failed\n");
        close();
        return false;
    }
    //注册 FFmpeg 网络中断回调
    format_->interrupt_callback.callback = &FfmpegRtmpPublisher::interrupt_io;
    format_->interrupt_callback.opaque = this;
    //FLV 输出容器中创建一路新的视频流
    stream_ = avformat_new_stream(format_,nullptr);
    if (stream_ == nullptr)
    {
        fprintf(stderr, "ERROR: create video stream failed\n");
        close();
        return false;
    }
    //创建编码器上下文
    encoder_ = avcodec_alloc_context3(codec);
    if (encoder_ == nullptr)
    {
        fprintf(stderr, "ERROR: create encoder context failed\n");
        close();
        return false;
    }
    // 设置视频分辨率
    encoder_->width = width;
    encoder_->height = height;
    // 设置编码器输入像素格式
    encoder_->pix_fmt = AV_PIX_FMT_YUV420P;
    //设置视频帧率
    encoder_->framerate = {fps, 1};
    //设置时间基
    encoder_->time_base = {1, 1000};    //时间基表示一个时间戳单位对应多少秒
    // 设置目标码率：3 Mbps
    encoder_->bit_rate = 3000000;
    // 设置 GOP 长度
    encoder_->gop_size = fps * 2;
    // 不使用 B 帧
    encoder_->max_b_frames = 0;
    // 根据封装格式要求设置全局头
    if(format_->oformat->flags & AVFMT_GLOBALHEADER)
    {
        encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    //设置编码速度
    ret = av_opt_set(encoder_->priv_data,"preset","ultrafast",0);
    if (ret < 0)
    {
        report_error("set preset", ret);
        close();
        return false;
    }
    // 设置低延迟编码
    ret = av_opt_set(encoder_->priv_data, "tune", "zerolatency", 0);
    if (ret < 0)
    {
        report_error("set tune", ret);
        close();
        return false;
    }
    ret = avcodec_open2(encoder_,codec,nullptr);
    if (ret < 0)
    {
        report_error("avcodec_open2", ret);
        close();
        return false;
    }
    // 将编码器参数复制到视频流
    ret = avcodec_parameters_from_context(stream_->codecpar, encoder_);
    if (ret < 0)
    {
        report_error("avcodec_parameters_from_context", ret);
        close();
        return false;
    }
    // 设置视频流时间基和帧率
    stream_->time_base = encoder_->time_base;
    stream_->avg_frame_rate = encoder_->framerate;
    // 创建两个视频帧
    bgr_buffer_ = av_frame_alloc();
    yuv_frame_ = av_frame_alloc();
    //创建编码数据包
    packet_ = av_packet_alloc();
    //检查是否创建成功
    if(bgr_buffer_ == nullptr || yuv_frame_ == nullptr ||packet_ == nullptr)
    {
        fprintf(stderr, "ERROR: allocate frame or packet failed\n");
        close();
        return false;
    }
    //初始化BGR视频帧
    ret = init_frame(bgr_buffer_,AV_PIX_FMT_BGR24,width,height);
    if (ret < 0)
    {
        report_error("init BGR frame", ret);
        close();
        return false;
    }
    ret = init_frame(yuv_frame_, AV_PIX_FMT_YUV420P, width, height);

    if (ret < 0)
    {
        report_error("init YUV frame", ret);
        close();
        return false;
    }
    // 创建 BGR24 转 YUV420P 的转换器
    scaler_ = sws_getContext(
        width,
        height,
        AV_PIX_FMT_BGR24,

        width,
        height,
        AV_PIX_FMT_YUV420P,

        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );

    // 检查转换器是否创建成功
    if (scaler_ == nullptr)
    {
        fprintf(stderr, "ERROR: create scaler failed\n");
        close();
        return false;
    }
    //设置截止时间
    io_deadline_us_ = av_gettime_relative()+ 5000000;

    //建立RTMP网络连接
    ret = avio_open2(
        &format_->pb,
        url.c_str(),
        AVIO_FLAG_WRITE,
        &format_->interrupt_callback,   //传入中断回调
        nullptr
    );
    if(ret<0)
    {
        report_error("avio_open2", ret);
        close();
        return false;   
    }
    // 当前 SDK 的 librtmp 不接受被拆开的 FLV 标签；扩大缓冲并逐包刷新。
    rtmp_output_ = url.rfind("rtmp://",0) == 0 || url.rfind("rtmps://",0) == 0;
    if (rtmp_output_)
    {
        const int buffer_size = 4 * 1024 * 1024;
        uint8_t* buffer = static_cast<uint8_t*>(av_malloc(buffer_size));
        if (buffer == nullptr)
        {
            report_error("allocate RTMP write buffer", AVERROR(ENOMEM));
            close();
            return false;
        }
        // 此时尚未写 FLV 文件头，原 AVIO 缓冲为空；保留连接和原回调。
        av_freep(&format_->pb->buffer);
        format_->pb->buffer = buffer;
        format_->pb->buffer_size = buffer_size;
        format_->pb->orig_buffer_size = buffer_size;
        format_->pb->buf_ptr = buffer;
        format_->pb->buf_ptr_max = buffer;
        format_->pb->buf_end = buffer + buffer_size;
        format_->pb->seekable = 0;
        format_->flags |= AVFMT_FLAG_FLUSH_PACKETS;
        format_->flush_packets = 1;
        ret = av_opt_set(format_->priv_data,"flvflags","no_duration_filesize",0);
        if (ret < 0)
        {
            report_error("set live FLV flags", ret);
            close();
            return false;
        }
    }
    // 重新设置文件头写入的超时截止时间
    io_deadline_us_ = av_gettime_relative() + 5000000;
    ret = avformat_write_header(format_,nullptr);
    if (ret < 0)
    {
        report_error("avformat_write_header", ret);
        close();
        return false;
    }
    //当前网络操作已经完成
    io_deadline_us_=0;

    header_written_ = true;
    return true;
}
bool FfmpegRtmpPublisher::write(cv::Mat& bgr_frame)
{
    return write_frame(bgr_frame, std::nullopt);
}
bool FfmpegRtmpPublisher::write(cv::Mat& bgr_frame,V4l2FrameMetadata& metadata)
{
    optional<int64_t> capture_us = nullopt;
        // 检查时间戳是否在基本合法范围内
    if (metadata.timestamp_seconds >= 0 &&
        metadata.timestamp_microseconds >= 0 &&
        metadata.timestamp_microseconds < 1000000 &&
        (metadata.timestamp_seconds != 0 ||
         metadata.timestamp_microseconds != 0))
    {
        // 秒和微妙合并，统一转化为微妙
        capture_us = static_cast<int64_t>(metadata.timestamp_seconds)*1000000 //将long转化为int64_t
        + metadata.timestamp_microseconds;
    }
    // 将图像和采集时间交给内部函数
    return write_frame(bgr_frame,capture_us);
}
bool FfmpegRtmpPublisher::write_frame(cv::Mat& bgr_frame,std::optional<int64_t> capture_us)
{
if (encoder_ == nullptr || bgr_buffer_ == nullptr || yuv_frame_ == nullptr ||
    scaler_ == nullptr || packet_ == nullptr || format_ == nullptr ||
    !header_written_ || failed_)
    {
        fprintf(stderr, "ERROR: publisher not ready\n");
        return false;
    }
    // 检查图像格式与分辨率
    if (bgr_frame.empty() ||
        bgr_frame.type() != CV_8UC3 ||
        bgr_frame.cols != encoder_->width ||
        bgr_frame.rows != encoder_->height)
    {
        fprintf(stderr, "ERROR: invalid BGR frame\n");
        return false;
    }
    // 确保 AVFrame 可以写入
    int ret = av_frame_make_writable(bgr_buffer_);
    if(ret<0)
    {
        return report_error("av_frame_make_writable", ret);
    }
    // 每行需要复制的字节数
    int row_bytes = bgr_frame.cols*3;
    // 按行复制 BGR 数据
    for (int y = 0; y < bgr_frame.rows; y++)
    {
        memcpy(
            bgr_buffer_->data[0] + y * bgr_buffer_->linesize[0],
            bgr_frame.ptr(y),
            row_bytes
        );
    }
    ret = av_frame_make_writable(yuv_frame_);
    if (ret < 0)
    {
        return report_error("YUV frame writable", ret);
    }
    //准备输入图像地址
    const uint8_t* src_data[4] = {
        bgr_buffer_->data[0],
        nullptr,
        nullptr,
        nullptr
    };
    // BGR24 转 YUV420P
    ret = sws_scale(
        scaler_,
        src_data,
        bgr_buffer_->linesize,
        0,
        bgr_frame.rows,
        yuv_frame_->data,
        yuv_frame_->linesize
    );
    if (ret != bgr_frame.rows)
    {
        fprintf(stderr, "ERROR: sws_scale failed\n");
        return false;
    }
    int64_t arrival_us = av_gettime_relative();
    // 设置当前视频帧的 PTS
    if (!set_frame_pts(capture_us, arrival_us))
    {
        return false;
    }
    // 把 YUV420P 视频帧送给 H.264 编码器
    ret = send_frame(yuv_frame_);

    if (ret < 0)
    {
        return report_error("send_frame", ret);
    }

    // 从编码器取出编码包并发送
    bool wrote_packet = false;

    ret = drain_packets(wrote_packet);

    if (ret < 0)
    {
        return report_error("drain_packets", ret);
    }

    return true;
}
int FfmpegRtmpPublisher::send_frame(const AVFrame* frame)
{
    int ret = avcodec_send_frame(encoder_,frame);
    if(ret == AVERROR(EAGAIN))  //编码器现在暂时无法接收新的一帧，需要先取走它已经生成的数据包。
    {
        bool wrote_packet = false;
        ret = drain_packets(wrote_packet);   //取出数据，然后重新尝试发送当前帧。
        if(ret<0)
        {
            return ret;
        }
        ret = avcodec_send_frame(encoder_,frame);
    }
    return ret;
}
int FfmpegRtmpPublisher::drain_packets(bool& wrote_packet)
{
    wrote_packet = false;
    while(true)
    {
        //从编码器接收 H.264 数据
        int ret = avcodec_receive_packet(encoder_,packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return 0;
        }
        if (ret < 0)
        {
            return ret;
        }
        wrote_packet = true;
        ret = write_packet();
        av_packet_unref(packet_);
        if(ret<0)
        {
            return ret;
        }
    }
}
int FfmpegRtmpPublisher::write_packet()
{
    if (format_ == nullptr ||
    encoder_ == nullptr ||
    stream_ == nullptr ||
    packet_ == nullptr)
    {
        return AVERROR(EINVAL);
    }
    // 为 Annex B 转 AVCC 和 FLV 头尾留出余量，禁止超大包再次拆开标签。
    if (rtmp_output_ && static_cast<int64_t>(packet_->size) * 2 + 1024 > format_->pb->buffer_size)
    {
        failed_ = true;
        report_error("encoded packet exceeds RTMP write buffer", AVERROR(ENOSPC));
        return AVERROR(ENOSPC);
    }
    //转化时间戳
    av_packet_rescale_ts(packet_,encoder_->time_base,stream_->time_base);
    //设置视频流编号
    packet_->stream_index = stream_->index;
    // 为本次编码包写入设置截止时间
    io_deadline_us_ = av_gettime_relative() + 5000000;
    //将h.264数据包交给 FLV 封装器并写入输出
    int ret = av_interleaved_write_frame(format_,packet_);
    if (ret >= 0 && format_->pb->error < 0)
    {
        ret = format_->pb->error;
    }
    // 本次操作结束
    io_deadline_us_ = 0;
    if(ret<0)
    {
        failed_ = true;
        report_error("av_interleaved_write_frame", ret);
    }
    return ret;
}
bool FfmpegRtmpPublisher::set_frame_pts(
    std::optional<int64_t> capture_us,
    int64_t arrival_us)
{
    int64_t delta = 0;
    if(previous_arrival_us_ !=0)
    {
        //使用V4L2采集时间
        if (capture_us.has_value() &&
            previous_capture_us_.has_value() &&
            capture_us.value() > previous_capture_us_.value()) 
        {
            delta = capture_us.value()-previous_capture_us_.value();
        }
        //如果采集时间戳不可用，使用到达时间
        else if(arrival_us > previous_arrival_us_)
        {
            delta = arrival_us - previous_arrival_us_;
        }
    }
    //保存当前时间戳
    previous_capture_us_ = capture_us;
    previous_arrival_us_ = arrival_us;
    //累加时间间隔
    elapsed_us_ += delta;
    // 将微秒转化为编码器的时间基
    int64_t pts = av_rescale_q(
        elapsed_us_,
        AVRational{1,1000000},
        encoder_->time_base
    );
    // 保证 PTS 严格递增
    if(pts<=last_pts_)
    {
        pts = last_pts_+1;
    }
    // 设置当前视频帧的 PTS
    yuv_frame_->pts = pts;
    last_pts_ = pts;
    return true;
}
int FfmpegRtmpPublisher::interrupt_io(void* opaque)
{
    // 取回当前推流器对象
    FfmpegRtmpPublisher* publisher = static_cast<FfmpegRtmpPublisher*>(opaque);
    if(publisher == nullptr)
    {
        return 0;
    }
    //检查主程序是否请求停止
    if(publisher->stop_flag_!= nullptr && *publisher->stop_flag_ !=0)
    {
        return 1;
    }
    //检查网络请求是否超时
    if(publisher->io_deadline_us_>0 && 
        av_gettime_relative()>=publisher->io_deadline_us_)
    {
        return 1;
    }
    return 0;
}
FfmpegRtmpPublisher::~FfmpegRtmpPublisher()
{
    close();
}
bool FfmpegRtmpPublisher::close()
{
    bool success = true;
    //判断主程序是否请求停止
    bool stopped = false;
    if(stop_flag_!=nullptr)
    {
        if(*stop_flag_!=0)
        {
            stopped = true;
        }
    }
    //正常关闭时，尽量取出编码器剩余数据
    if(header_written_ && !stopped && !failed_ && encoder_ != nullptr && packet_ != nullptr)
    {
        int ret = send_frame(nullptr);
        if(ret<0 && ret != AVERROR_EOF)
        {
            report_error("flush encoder",ret);
            success = false;
        }
        else
        {
            bool wrote_packet = false;
            ret = drain_packets(wrote_packet);
            if (ret < 0)
            {
                report_error("drain remaining packets", ret);
                success = false;
            }
        }
    }
        // 正常关闭时，写入 FLV 结束信息
    if (format_ != nullptr && header_written_ && !stopped && !failed_)
    {
        io_deadline_us_ = av_gettime_relative() + 2000000;

        int ret = av_write_trailer(format_);

        if (ret < 0)
        {
            report_error("av_write_trailer", ret);
            success = false;
        }
    }
    // 关闭 RTMP 网络连接
    if (format_ != nullptr && format_->pb != nullptr)
    {
        io_deadline_us_ = av_gettime_relative() + 2000000;
        int ret = avio_closep(&format_->pb);
        if (ret < 0)
        {
            report_error("avio_closep", ret);
            success = false;
        }
    }
        // 释放编码数据包
    av_packet_free(&packet_);

    // 释放两个视频帧
    av_frame_free(&bgr_buffer_);
    av_frame_free(&yuv_frame_);

    // 释放图像转换器
    sws_freeContext(scaler_);
    scaler_ = nullptr;

    // 释放 H.264 编码器
    avcodec_free_context(&encoder_);

    // 释放 FLV 输出上下文
    if (format_ != nullptr)
    {
        avformat_free_context(format_);
        format_ = nullptr;
    }

    stream_ = nullptr;
     // 重置时间戳
    previous_capture_us_ = std::nullopt;
    previous_arrival_us_ = 0;
    elapsed_us_ = 0;
    last_pts_ = -1;

    // 重置运行状态
    header_written_ = false;
    rtmp_output_ = false;
    failed_ = false;
    closing_ = false;
    io_deadline_us_ = 0;
    return success;
    
}