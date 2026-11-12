#include "v4l2_capture.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <opencv2/imgproc.hpp>
using namespace cv;
V4l2Capture::V4l2Capture() = default;
V4l2Capture::~V4l2Capture()
{
    close();
}
void V4l2Capture::close()
{
    // 从未成功打开设备时，什么也不需要释放。
    if (fd_ < 0) 
    {
        return;
    }
    // 只有已成功 STREAMON 才尝试 STREAMOFF。
    //停止数据流采集
    if (streaming_) 
    {
        enum v4l2_buf_type stream_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        //停止采集
        if (ioctl(fd_, VIDIOC_STREAMOFF, &stream_type) < 0) 
        {
            fprintf(stderr,"ERROR: VIDIOC_STREAMOFF failed: %s\n",strerror(errno));
        } 
        else 
        {
            printf("stream_stopped\n");
        }
        // 无论 STREAMOFF 是否报错，都不再把当前对象当作正在采集。
        streaming_ = false;
    }
    for(size_t buffer_index = 0;buffer_index < mapped_buffers_.size();buffer_index++)
    {
        MappedBuffer& mapped = mapped_buffers_[buffer_index];
        // 该块从未 mmap 成功，或已经解除映射。
        if (mapped.address == MAP_FAILED) 
        {
            continue;
        }
        //解除地址映射地址
        if (munmap(mapped.address, mapped.length) < 0) 
        {
            fprintf(stderr,"ERROR: munmap buffer[%zu] failed: %s\n",buffer_index,strerror(errno));
            continue;
        }
        printf("buffer[%zu].unmapped\n", buffer_index);
        // 防止后续误用已经失效的地址。
        mapped.address = MAP_FAILED;
        mapped.length = 0;
    }
    // 至此 vector 内只剩描述记录，也可以清空。
    mapped_buffers_.clear();
    if (buffers_allocated_) 
    {
        v4l2_requestbuffers release{};
        // count = 0 表示要求驱动释放此前申请的全部缓冲区。
        release.count = 0;
        release.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        release.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_REQBUFS, &release) < 0) 
        {
            fprintf(stderr,"ERROR: release buffers failed: %s\n",strerror(errno));
        } 
        else 
        {
            printf("buffers_released\n");
        }

        // close() 后对象不再可用于采集，即使释放命令报错也不保留旧状态。
        buffers_allocated_ = false;
    }
    if (::close(fd_) < 0) 
    {
        fprintf(stderr,"ERROR: close %s failed: %s\n",device_.c_str(),strerror(errno));
    }
    else
    {
        printf("device_closed\n");
    }

    // 无论 close 是否报错，这个对象都不再保留旧设备状态。
    fd_ = -1;
    device_.clear();
    width_ = 0;
    height_ = 0;
    stride_ = 0;
    plane_count_ = 0;
}
/*
 * string& device   像头设备文件的路径
 * requested_width  设置的图像宽度
 * requested_height 设置的图像宽度高
 * requested_buffer_count 缓冲区数量
*/
bool V4l2Capture::open(const string& device,
    unsigned int requested_width,
    unsigned int requested_height,
    unsigned int requested_buffer_count)
{
    // 同一对象再次 open 前，先确保旧资源已清理。
    close();

    fd_ = ::open(device.c_str(), O_RDWR);

    if (fd_ < 0) 
    {
        fprintf(stderr,"ERROR: open %s failed: %s\n",device.c_str(),strerror(errno));
        return false;
    }

    device_ = device;
    v4l2_capability capability{};
    if (ioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) 
    {
        fprintf(stderr,"ERROR: VIDIOC_QUERYCAP failed: %s\n",strerror(errno));
        close();
        return false;
    }

    unsigned int caps = capability.capabilities;
    if ((capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0) 
    {
        caps = capability.device_caps;
    }
    //查看负责这个视频节点的驱动名称
    printf("driver=%s\n", reinterpret_cast<const char*>(capability.driver));
    //查看视频设备或采集节点的名称
    printf("card=%s\n", reinterpret_cast<const char*>(capability.card));
    if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) 
    {
        fprintf(stderr, "ERROR: device does not support MPlane capture\n");
        close();
        return false;
    }
    if ((caps & V4L2_CAP_STREAMING) == 0) 
    {
        fprintf(stderr,"ERROR: device does not support streaming I/O\n");
        close();
        return false;
    }
    //设置摄像头的图像宽度，高度，NV12格式
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.width = requested_width;
    format.fmt.pix_mp.height = requested_height;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (ioctl(fd_, VIDIOC_S_FMT, &format) < 0) 
    {
        fprintf(stderr,"ERROR: VIDIOC_S_FMT failed: %s\n",strerror(errno));
        close();
        return false;
    }
    if (format.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12) 
    {
        fprintf(stderr,"ERROR: driver did not accept NV12 format\n");
        close();
        return false;
    }
    if (format.fmt.pix_mp.num_planes != 1) 
    {
        fprintf(stderr,"ERROR: expected one plane, got %u\n",format.fmt.pix_mp.num_planes);
        close();
        return false;
    }
    width_ = format.fmt.pix_mp.width;
    height_ = format.fmt.pix_mp.height;
    stride_ = format.fmt.pix_mp.plane_fmt[0].bytesperline;
    plane_count_ = format.fmt.pix_mp.num_planes;

    printf("width=%u\n", width_);
    printf("height=%u\n", height_);
    printf("pixel_format=NV12\n");
    printf("num_planes=%u\n", plane_count_);
    printf("stride=%u\n", stride_);
    printf("sizeimage=%u\n",format.fmt.pix_mp.plane_fmt[0].sizeimage);
    //申请缓冲区
    v4l2_requestbuffers request{};
    request.count = requested_buffer_count;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    request.memory = V4L2_MEMORY_MMAP;
    printf("requested_buffers=%u\n", request.count);
    if (ioctl(fd_, VIDIOC_REQBUFS, &request) < 0) 
    {
        fprintf(stderr,"ERROR: VIDIOC_REQBUFS failed: %s\n",strerror(errno));
        close();
        return false;
    }
    printf("allocated_buffers=%u\n", request.count);
    if (request.count == 0) 
    {
        fprintf(stderr,"ERROR: driver allocated zero buffers\n");
        close();
        return false;
    }
    // 只有驱动实际成功给出至少一块缓冲区后，才标记为已分配。
    buffers_allocated_ = true;
    // vector 长度按驱动实际返回数量，而不是用户请求值创建。
    //mapped地址映射
    mapped_buffers_.resize(request.count);
    for (unsigned int buffer_index = 0;buffer_index < request.count; buffer_index++)
    {
        v4l2_plane planes[VIDEO_MAX_PLANES]{};
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = buffer_index;
        // MPlane API 必须由调用者提供平面描述数组。
        buffer.length = plane_count_;
        buffer.m.planes = planes;
        if (ioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0)
        {
            fprintf(stderr,"ERROR: VIDIOC_QUERYBUF index=%u failed: %s\n",buffer_index, strerror(errno));
            close();
            return false;
        }

        if (buffer.length != plane_count_ ||planes[0].length == 0)
        {
            fprintf(stderr,"ERROR: invalid buffer[%u] plane information\n",buffer_index);
            close();
            return false;
        }

        void* address = mmap(
            nullptr,
            planes[0].length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd_,
            static_cast<off_t>(planes[0].m.mem_offset));

        if (address == MAP_FAILED)
        {
            fprintf(stderr,"ERROR: mmap buffer[%u] failed: %s\n",buffer_index,strerror(errno));
            close();
            return false;
        }

        mapped_buffers_[buffer_index].address = address;
        mapped_buffers_[buffer_index].length = planes[0].length;

        printf("buffer[%u].mapped address=%p length=%u offset=%u\n",
            buffer_index,
            address,
            planes[0].length,
            planes[0].m.mem_offset);
    }
    //把已分配的缓冲区交给驱动
    for (unsigned int buffer_index = 0;buffer_index < request.count;buffer_index++)
    {
        v4l2_plane planes[VIDEO_MAX_PLANES]{};
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = buffer_index;
        buffer.length = plane_count_;
        buffer.m.planes = planes;
        if (ioctl(fd_, VIDIOC_QBUF, &buffer) < 0)
        {
            fprintf(stderr,"ERROR: VIDIOC_QBUF index=%u failed: %s\n",buffer_index,strerror(errno));
            close();
            return false;
        }
        printf("buffer[%u].queued\n", buffer_index);
    }
    //启动采集
    enum v4l2_buf_type stream_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &stream_type) < 0)
    {
        fprintf(stderr,
                "ERROR: VIDIOC_STREAMON failed: %s\n",
                strerror(errno));
        close();
        return false;
    }
    streaming_ = true;
    printf("stream_started\n");
    return true;
}
bool V4l2Capture::read(Mat& bgr_frame, V4l2FrameMetadata& metadata)
{
    if (fd_ < 0 || !streaming_)
    {
        fprintf(stderr,"ERROR: camera is not streaming\n");
        return false;
    }

    bgr_frame.release();
    metadata = {};
    v4l2_plane dequeued_planes[VIDEO_MAX_PLANES]{};
    v4l2_buffer dequeued{};
    //描述缓冲区
    dequeued.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    dequeued.memory = V4L2_MEMORY_MMAP;
    dequeued.length = plane_count_;
    dequeued.m.planes = dequeued_planes;
    //从缓冲区取回控制权
    if (ioctl(fd_, VIDIOC_DQBUF, &dequeued) < 0)
    {
        fprintf(stderr,"ERROR: VIDIOC_DQBUF failed: %s\n",strerror(errno));
        return false;
    }
    const unsigned int returned_index = dequeued.index;
    bool frame_ok = true;
    if(dequeued.length != plane_count_ || returned_index >= mapped_buffers_.size())
    {
        fprintf(stderr,"ERROR: invalid dequeued buffer\n");
        frame_ok = false;
    }
    else
    {
        MappedBuffer& mapped = mapped_buffers_[returned_index];

        const size_t required_bytes = static_cast<size_t>(stride_) * static_cast<size_t>(height_) * 3 / 2;

        if (mapped.address == MAP_FAILED || dequeued_planes[0].bytesused < required_bytes)
        {
            fprintf(stderr,"ERROR: invalid NV12 frame buffer\n");
            frame_ok = false;
        }
        else
        {
            //把mmap()映射的原始图像内存包装成一个OpenCV Mat
            Mat padded_nv12(static_cast<int>(height_ * 3 / 2),static_cast<int>(stride_),CV_8UC1,mapped.address);
            Mat nv12_view = padded_nv12(Rect(0,0,static_cast<int>(width_),static_cast<int>(height_ * 3 / 2)));
            try
            {
                cvtColor(nv12_view,bgr_frame,COLOR_YUV2BGR_NV12);
            }
            catch (const cv::Exception& error)
            {
                fprintf(stderr,"ERROR: cvtColor failed: %s\n",error.what());
                frame_ok = false;
            }

            if (frame_ok)
            {
                metadata.sequence = dequeued.sequence;
                metadata.timestamp_seconds = dequeued.timestamp.tv_sec;
                metadata.timestamp_microseconds = dequeued.timestamp.tv_usec;
                metadata.bytes_used = dequeued_planes[0].bytesused;
            }
        }
    }

    //最后重新把缓冲区交给驱动
    v4l2_plane requeue_planes[VIDEO_MAX_PLANES]{};
    v4l2_buffer requeue{};
    requeue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    requeue.memory = V4L2_MEMORY_MMAP;
    requeue.index = returned_index;
    requeue.length = plane_count_;
    requeue.m.planes = requeue_planes;

    if (ioctl(fd_, VIDIOC_QBUF, &requeue) < 0)
    {
        fprintf(stderr,
                "ERROR: VIDIOC_QBUF index=%u failed: %s\n",
                returned_index,
                strerror(errno));
        return false;
    }

    return frame_ok;
}