#编译执行指令 cmake -B build 生成makefile 在第一次和修改了CMakeLists.txt后跑
#            cmake --build build 按makefile调用g++链接 每次修改.cpp后跑
#            ./build/imgsvc执行代码
cmake_minimum_required(VERSION 3.16) #配置cmake版本
project(imgsvc CXX) #项目名和语言

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)#bug报警

list(APPEND CMAKE_PREFIX_PATH /opt/onnxruntime)
find_package(OpenCV REQUIRED) #添加opencv库
find_library(ONNXRUNTIME_LIB NAMES onnxruntime PATHS /opt/onnxruntime/lib REQUIRED)

add_executable(imgsvc src/main.cpp)#添加执行文件

target_include_directories(imgsvc PRIVATE /opt/onnxruntime/include)
target_link_libraries(imgsvc PRIVATE ${OpenCV_LIBS})#给imgsvc链接opencv库
target_link_libraries(imgsvc PRIVATE ${ONNXRUNTIME_LIB})