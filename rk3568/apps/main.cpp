#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include "ffmpeg_rtmp_publisher.h"
#include "v4l2_capture.h"
#include <opencv2/core.hpp>
using namespace std;
using namespace cv;
using namespace std;
volatile sig_atomic_t g_stop = 0;
void handle_signal(int)
{
    g_stop = 1;
}
int main()
{
    const string device = "/dev/video0";
    const unsigned int width = 1280;
    const unsigned int height = 720;
    const unsigned int buffer_count = 4;
    const int fps = 30;
    string rtmp_url = "rtmp://192.168.50.1:1935/rk3568/fixed-box";
    signal(SIGINT,handle_signal);
    V4l2Capture camera;
    // 打开摄像头
    if (!camera.open(device, width, height, buffer_count))
    {
        fprintf(stderr, "ERROR: camera open failed\n");
        return 1;
    }
    // 创建 FFmpeg 推流器
    FfmpegRtmpPublisher publisher(&g_stop);
    if(!publisher.open(width,height,fps,rtmp_url))
    {
        fprintf(stderr, "ERROR: publisher open failed\n");
        camera.close();
        return 1;
    }
    cv::Mat frame;
    V4l2FrameMetadata metadata;
    int result = 0;
    while(g_stop == 0)
    {
        if(!camera.read(frame,metadata))
        {
            if(g_stop == 0)
            {
                fprintf(stderr, "ERROR: camera read failed\n");
                result = 1;
            }
            break;
        }
        if(!publisher.write(frame,metadata))
        {
            if(g_stop==0)
            {
                fprintf(stderr, "ERROR: publisher write failed\n");
                result = 1;
            }
            break;
        }
    }
    if (!publisher.close())
    {
        fprintf(stderr, "WARNING: publisher close failed\n");
        if (g_stop == 0)
        {
            result = 1;
        }
    }
    camera.close();
    return result;
}
