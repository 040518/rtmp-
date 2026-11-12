QT += widgets
CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    videodecodethread.cpp

HEADERS += \
    mainwindow.h \
    videodecodethread.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
win32 {
    CONFIG -= windows
    CONFIG += console

    # Keep the Qt MinGW spec from adding /include when VULKAN_SDK is unset.
    QMAKE_INCDIR_VULKAN =

    FFMPEG_ROOT = D:/Qt/FFmpeg/ffmpeg-9.0.2-full_build-shared
    !exists($$FFMPEG_ROOT/include/libavformat/avformat.h): error(FFmpeg headers not found at $$FFMPEG_ROOT)
    !exists($$FFMPEG_ROOT/lib/libavformat.dll.a): error(FFmpeg MinGW import libraries not found at $$FFMPEG_ROOT)
    INCLUDEPATH += $$FFMPEG_ROOT/include
    LIBS += -L$$FFMPEG_ROOT/lib -lavformat -lavcodec -lavutil -lswscale -lswresample
}
