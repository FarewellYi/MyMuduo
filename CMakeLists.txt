cmake_minimum_required(VERSION 2.5)
project(muduoDIY)

# cmake -> makefile -> make
# muduoDIY 最终编译生成.so动态库，设置动态库的路径，放在根目录下的lib文件夹中
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置调试信息, 设置C++11语言标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11")

# 定义参与编译的源代码文件
aux_source_directory(. SRC_LIST)

# 编译生成动态库 muduoDIY
add_library(muduoDIY SHARED ${SRC_LIST})

