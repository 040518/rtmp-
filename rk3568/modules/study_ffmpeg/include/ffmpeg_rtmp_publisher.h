#ifndef FFMPEG_RTMP_PUBLISHER_H
#define FFMPEG_RTMP_PUBLISHER_H

#include <csignal>
#include <cstdint>
#include <optional>
#include <string>

struct V4l2FrameMetadata;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct SwsContext;

namespace cv {
    class Mat;
}
class FfmpegRtmpPublisher
{
public:
    FfmpegRtmpPublisher(const volatile std::sig_atomic_t* stop_flag = nullptr);
    ~FfmpegRtmpPublisher();
    bool open(int width, int height, int fps, std::string& url);
    bool write(cv::Mat& bgr_frame,V4l2FrameMetadata& metadata);
    bool write(cv::Mat& bgr_frame);
    bool close();
private:
    //主程序停止信号
    const volatile std::sig_atomic_t* stop_flag_;
    //FFmpeg 输出和编码器
    AVFormatContext* format_ = nullptr;
    AVCodecContext* encoder_ = nullptr;
    AVStream* stream_ = nullptr;
    //视频帧和编码包
    AVFrame* bgr_buffer_ = nullptr;
    AVFrame* yuv_frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    //BGR 转 YUV420P
    SwsContext* scaler_ = nullptr;
    //视频时间戳
    std::optional<int64_t> previous_capture_us_;
    int64_t previous_arrival_us_ = 0;
    int64_t elapsed_us_ = 0;
    int64_t last_pts_ = -1;

    //网络操作超时
    int64_t io_deadline_us_ = 0;

    //推流状态
    bool header_written_ = false;
    bool rtmp_output_ = false;
    bool failed_ = false;
    bool closing_ = false;
    
    bool write_frame(cv::Mat& bgr_frame,std::optional<int64_t> capture_us);
    bool set_frame_pts(std::optional<int64_t> capture_us,int64_t arrival_us);
    static int interrupt_io(void* opaque);
    int send_frame(const AVFrame* frame);
    int drain_packets(bool& wrote_packet);
    int write_packet();
};

#endif 