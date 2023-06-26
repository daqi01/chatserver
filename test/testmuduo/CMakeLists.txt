
#配置头文件搜索路径
#include_directorirs()
#配置库文件搜索路径
#link_directories()

#设置需要编译的源文件列表
set(SRC_LIST ./muduo_server.cpp)

#设置目标文件最终存储的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

#将指定目录.下的所有源文件名字放入变量名SRC_LIST
#aux_source_directory(. SRC_LIST)

#生成目标文件server   ---->  参数1 目标文件  参数2 源文件
add_executable(server ${SRC_LIST})
#server这个目标文件，需要链接的库名
target_link_libraries(server muduo_net muduo_base pthread)