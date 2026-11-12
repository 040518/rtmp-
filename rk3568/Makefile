
# ============================================================
# RK3568 V4L2 + FFmpeg RTMP 推流项目
# 编译环境：Ubuntu + Buildroot SDK
# 运行设备：RK3568（AArch64）
# ============================================================

# 1. Buildroot 交叉编译工具链
SDK_ROOT := /home/hhh/rk3568/buildroot/output/rockchip_atk_dlrk3568/host
SYSROOT := $(SDK_ROOT)/aarch64-buildroot-linux-gnu/sysroot

CXX := $(SDK_ROOT)/bin/aarch64-buildroot-linux-gnu-g++
AR  := $(SDK_ROOT)/bin/aarch64-buildroot-linux-gnu-ar

# 2. pkg-config 配置
PKG_CONFIG := PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) \
              PKG_CONFIG_LIBDIR=$(SYSROOT)/usr/lib/pkgconfig \
              $(SDK_ROOT)/bin/pkg-config

# OpenCV
OPENCV_CFLAGS := $(shell $(PKG_CONFIG) --cflags opencv4)
OPENCV_LIBS   := $(shell $(PKG_CONFIG) --libs opencv4)

# FFmpeg
FFMPEG_PACKAGES := libavformat libavcodec libswscale libavutil

FFMPEG_CFLAGS = $(shell $(PKG_CONFIG) --print-errors --cflags $(FFMPEG_PACKAGES))
FFMPEG_LIBS   = $(shell $(PKG_CONFIG) --print-errors --libs $(FFMPEG_PACKAGES))

# 3. 构建输出目录
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
LIB_DIR   := $(BUILD_DIR)/lib
BIN_DIR   := $(BUILD_DIR)/bin

# 4. V4L2 摄像头采集模块
V4L2_DIR     := modules/v4l2_capture
V4L2_INCLUDE := $(V4L2_DIR)/include
V4L2_SOURCE  := $(V4L2_DIR)/src/v4l2_capture.cpp
V4L2_OBJECT  := $(OBJ_DIR)/v4l2_capture.o
V4L2_LIBRARY := $(LIB_DIR)/libv4l2_capture.a

# 5. FFmpeg RTMP 推流模块
FFMPEG_DIR     := modules/study_ffmpeg
FFMPEG_INCLUDE := $(FFMPEG_DIR)/include
FFMPEG_SOURCE  := $(FFMPEG_DIR)/src/ffmpeg_rtmp_publisher.cpp
FFMPEG_OBJECT  := $(OBJ_DIR)/ffmpeg_rtmp_publisher.o
FFMPEG_LIBRARY := $(LIB_DIR)/libffmpeg_rtmp_publisher.a

# 6. 主程序
MAIN_SOURCE := apps/main.cpp
MAIN_TARGET := $(BIN_DIR)/main.aarch64

# 7. 编译参数
# 调试阶段使用 -O0 -g，保留调试信息
CXXFLAGS := -std=c++17 -g -O0 -Wall -Wextra -Wpedantic

# 头文件搜索路径
CPPFLAGS := $(OPENCV_CFLAGS) \
            -I$(V4L2_INCLUDE) \
            -I$(FFMPEG_INCLUDE) \
            $(FFMPEG_CFLAGS)

# 链接参数
LDFLAGS := --sysroot=$(SYSROOT) -pthread
LDLIBS  := $(FFMPEG_LIBS) $(OPENCV_LIBS)

# 8. 伪目标
.PHONY: all clean help info \
        v4l2-lib ffmpeg-rtmp-lib \
        v4l2-ffmpeg-rtmp-stream

# 默认构建完整程序
all: $(MAIN_TARGET)

# 9. 自动创建输出目录
$(OBJ_DIR) $(LIB_DIR) $(BIN_DIR):
	mkdir -p $@

# ============================================================
# V4L2 模块
# ============================================================

# 编译 v4l2_capture.cpp
$(V4L2_OBJECT): $(V4L2_SOURCE) \
                $(V4L2_INCLUDE)/v4l2_capture.h | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) \
		-c $< -o $@

# 打包 V4L2 静态库
$(V4L2_LIBRARY): $(V4L2_OBJECT) | $(LIB_DIR)
	$(AR) rcs $@ $<

v4l2-lib: $(V4L2_LIBRARY)

# ============================================================
# FFmpeg RTMP 模块
# ============================================================

# 编译 ffmpeg_rtmp_publisher.cpp
$(FFMPEG_OBJECT): $(FFMPEG_SOURCE) \
                  $(FFMPEG_INCLUDE)/ffmpeg_rtmp_publisher.h \
                  $(V4L2_INCLUDE)/v4l2_capture.h | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) \
		-c $< -o $@

# 打包 FFmpeg 静态库
$(FFMPEG_LIBRARY): $(FFMPEG_OBJECT) | $(LIB_DIR)
	$(AR) rcs $@ $<

ffmpeg-rtmp-lib: $(FFMPEG_LIBRARY)

# ============================================================
# 主程序：V4L2 + FFmpeg + RTMP
# ============================================================

v4l2-ffmpeg-rtmp-stream: $(MAIN_TARGET)

$(MAIN_TARGET): $(MAIN_SOURCE) \
                $(V4L2_LIBRARY) \
                $(FFMPEG_LIBRARY) \
                $(V4L2_INCLUDE)/v4l2_capture.h \
                $(FFMPEG_INCLUDE)/ffmpeg_rtmp_publisher.h | $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) \
		$(MAIN_SOURCE) \
		$(V4L2_LIBRARY) \
		$(FFMPEG_LIBRARY) \
		-o $@ \
		$(LDLIBS)

# ============================================================
# 辅助命令
# ============================================================

info:
	@echo "========== Build Configuration =========="
	@echo "CXX=$(CXX)"
	@echo "AR=$(AR)"
	@echo "SYSROOT=$(SYSROOT)"
	@echo "MAIN_SOURCE=$(MAIN_SOURCE)"
	@echo "MAIN_TARGET=$(MAIN_TARGET)"
	@echo "V4L2_LIBRARY=$(V4L2_LIBRARY)"
	@echo "FFMPEG_LIBRARY=$(FFMPEG_LIBRARY)"
	@echo "OPENCV_CFLAGS=$(OPENCV_CFLAGS)"
	@echo "FFMPEG_CFLAGS=$(FFMPEG_CFLAGS)"
	@echo "FFMPEG_LIBS=$(FFMPEG_LIBS)"
	@echo "========================================="

help:
	@echo "Available commands:"
	@echo "  make                          Build complete application"
	@echo "  make v4l2-lib                 Build V4L2 static library"
	@echo "  make ffmpeg-rtmp-lib          Build FFmpeg static library"
	@echo "  make v4l2-ffmpeg-rtmp-stream  Build complete application"
	@echo "  make info                     Show build configuration"
	@echo "  make clean                    Remove generated project outputs"
	@echo "  make help                     Show this message"

# 只清理当前工程生成的文件
clean:
	rm -f \
		$(V4L2_OBJECT) \
		$(FFMPEG_OBJECT) \
		$(V4L2_LIBRARY) \
		$(FFMPEG_LIBRARY) \
		$(MAIN_TARGET)
