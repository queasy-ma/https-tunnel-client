cmake_minimum_required(VERSION 3.26)
project(https_tunnel_client C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXE_LINKER_FLAGS "-static")
set(CMAKE_CXX_FLAGS "-static")
# 添加编译器优化标志以减小输出文件的大小
add_compile_options(-O3 -s -g0)

add_executable(https_tunnel_client
        rest_client_pool.h
        rest_client_pool.c
        client-socket.c
        base64.c
        base64.h
        dns.c
        dns.h
        conn-hash-tabale.c
        conn-hash-tabale.h
        #        client-alive.c
)
set(VCPKG_TARGET_TRIPLET "x64-mingw-static" CACHE STRING "" FORCE)
find_package(CURL REQUIRED)
target_link_libraries(https_tunnel_client PRIVATE CURL::libcurl ws2_32)