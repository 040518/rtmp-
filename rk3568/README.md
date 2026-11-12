# RK3568 Edge Vision

这是从旧工作区独立出来的 RK3568 V0 重建工程。

## 在 Ubuntu 交叉编译

```bash
cd /home/hhh/rk3568-edge-vision
make
```

默认生成 `build/bin/main.aarch64`。`make ffmpeg-rtmp-lib` 只构建 `build/lib/libffmpeg_rtmp_publisher.a`。`make help` 列出目标，`make info` 显示 SDK、sysroot 和库参数。

FFmpeg 程序入口为 `apps/main.cpp`，推流模块在 `modules/study_ffmpeg`。编译器通过 `-Imodules/study_ffmpeg/include` 查找 `ffmpeg_rtmp_publisher.h`。如果编译通过但 VS Code 仍标红，可在 IntelliSense 的 `includePath` 中配置该目录。

## 原有目标

- `make v4l2-rtmp-stream` 对应 `apps/v4l2_rtmp_stream.cpp`，需要恢复 `modules/gst_rtmp_publisher` 的源码和头文件。
- `make build/bin/v4l2_mplane_probe.aarch64` 构建原 V4L2 采集探针，需要恢复 `apps/v4l2_mplane_probe.cpp`。探针采集 300 帧，板端保存 `/tmp/rk3568_v0_probe_frame299.png`。
