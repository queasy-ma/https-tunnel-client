cmake_minimum_required(VERSION 3.26)
project(https_tunnel_client C)

set(CMAKE_C_STANDARD 11)

add_executable(https_tunnel_client
        rest_client_pool.h
        rest_client_pool.c
#        client-socket.c
#        base64.c
#        base64.h
        client-alive.c
)
find_package(CURL REQUIRED)
target_link_libraries(https_tunnel_client PRIVATE CURL::libcurl ws2_32)