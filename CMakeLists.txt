cmake_minimum_required(VERSION 3.13)
project( http-server )
set(CMAKE_C_STANDARD 23)

include_directories(include)
file(GLOB_RECURSE SOURCE_FILES src/*.c)
add_executable(http-server ${SOURCE_FILES}) 
find_package(ZLIB REQUIRED)
target_link_libraries(http-server PRIVATE ZLIB::ZLIB)
