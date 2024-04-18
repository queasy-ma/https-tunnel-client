cmake_minimum_required(VERSION 3.26)
project(https_tunnel_client C)

set(CMAKE_C_STANDARD 11)

add_executable(https_tunnel_client
        conn-pool.c
)
find_package(CURL REQUIRED)
target_link_libraries(https_tunnel_client PRIVATE CURL::libcurl ws2_32)