#编译执行指令 cmake -B build 生成makefile 在第一次和修改了CMakeLists.txt后跑
#            cmake --build build 按makefile调用g++链接 每次修改.cpp后跑
#            ./build/imgsvc执行代码
cmake_minimum_required(VERSION 3.16) #配置cmake版本
project(imgsvc CXX) #项目名和语言

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)#bug报警

# ---- 找依赖 ----
list(APPEND CMAKE_PREFIX_PATH /opt/onnxruntime)
find_package(OpenCV REQUIRED) #添加opencv库
# ONNX Runtime：官方 tarball 自带的 CMake 配置和实际布局不一致（它假设 lib64/ 和
# include/onnxruntime/，实际是 lib/ 和 include/），所以不用 find_package，
# 直接用 find_library 指定路径
find_library(ONNXRUNTIME_LIB NAMES onnxruntime PATHS /opt/onnxruntime/lib REQUIRED)

# ---- 定义目标：四个源文件 ----
add_executable(imgsvc
    src/main.cpp
    src/preprocess.cpp
    src/infer.cpp
    src/postprocess.cpp)

add_executable(imgsvc-server src/server.cpp)

# ---- 给目标挂属性（必须在 add_executable 之后）----
target_include_directories(imgsvc PRIVATE
    include                      # 自己的头文件目录
    /opt/onnxruntime/include)    # ONNX Runtime 头文件

target_link_libraries(imgsvc PRIVATE ${OpenCV_LIBS})
target_link_libraries(imgsvc PRIVATE ${ONNXRUNTIME_LIB})
target_include_directories(imgsvc-server PRIVATE include)
