#编译执行指令 cmake -B build 生成makefile 在第一次和修改了CMakeLists.txt后跑
#            cmake --build build 按makefile调用g++链接 每次修改.cpp后跑
cmake_minimum_required(VERSION 3.16) #配置cmake版本
project(imgsvc CXX) #项目名和语言

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)#bug报警

find_package(OpenCV REQUIRED) #添加opencv库

add_executable(imgsvc src/main.cpp)#添加执行文件

target_link_libraries(imgsvc PRIVATE ${OpenCV_LIBS})#给imgsvc链接opencv库