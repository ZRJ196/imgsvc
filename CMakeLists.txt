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
# ONNX Runtime：官方 tarball 自带的 CMake 配置和实际布局不一致，直接用 find_library
find_library(ONNXRUNTIME_LIB NAMES onnxruntime PATHS /opt/onnxruntime/lib REQUIRED)

# ---- 三个模块（两个可执行文件共用）----
set(IMGSVC_MODULES
    src/preprocess.cpp
    src/infer.cpp
    src/postprocess.cpp)

# ---- 命令行工具 ----
add_executable(imgsvc
    src/main.cpp
    ${IMGSVC_MODULES})

target_include_directories(imgsvc PRIVATE
    include
    /opt/onnxruntime/include)
target_link_libraries(imgsvc PRIVATE ${OpenCV_LIBS})
target_link_libraries(imgsvc PRIVATE ${ONNXRUNTIME_LIB})

# ---- HTTP 服务端 ----
add_executable(imgsvc-server
    src/server.cpp
    ${IMGSVC_MODULES})

target_include_directories(imgsvc-server PRIVATE
    include
    /opt/onnxruntime/include)
target_link_libraries(imgsvc-server PRIVATE ${OpenCV_LIBS})
target_link_libraries(imgsvc-server PRIVATE ${ONNXRUNTIME_LIB})
