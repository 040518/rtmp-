
# 使用 Qt 6 的 CMake 最低版本要求。
cmake_minimum_required(VERSION 3.16)

# 声明工程名称和所使用的语言。
project(VideoMonitor LANGUAGES CXX)

# 与 qmake 工程保持一致，使用 C++17。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动运行 Qt 的元对象、UI 和资源编译工具。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找 Widgets 模块，提供桌面窗口和控件。
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 定义可执行程序及其当前列出的源文件。
add_executable(VideoMonitor
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

# 将 Widgets 库链接到该可执行程序。
target_link_libraries(VideoMonitor
    PRIVATE Qt6::Widgets
)
