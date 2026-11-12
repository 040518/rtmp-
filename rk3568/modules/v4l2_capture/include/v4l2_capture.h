#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <opencv2/core.hpp>
using namespace std;
struct V4l2FrameMetadata
{
    uint32_t sequence = 0;
    long timestamp_seconds = 0;
    long timestamp_microseconds = 0;
    size_t bytes_used = 0;
};
class V4l2Capture
{
public:
    V4l2Capture();
    //~V4l2Capture()：对象结束时自动调用 close()
    ~V4l2Capture();
    V4l2Capture(const V4l2Capture&) = delete;
    V4l2Capture& operator=(const V4l2Capture&) = delete;
    bool open(const string& device,unsigned int width,unsigned int height,unsigned int buffer_count = 4);
    bool read(cv::Mat& bgr_frame,V4l2FrameMetadata& metadata);
    void close();
private:
    struct MappedBuffer
    {
        void* address = MAP_FAILED;
        size_t length = 0;
    };
    int fd_ = -1;
    string device_;
    unsigned int width_ = 0;
    unsigned int height_ = 0;
    unsigned int stride_ = 0;
    unsigned int plane_count_ = 0;
    vector<MappedBuffer> mapped_buffers_;
    bool buffers_allocated_ = false;
    bool streaming_ = false;
};

