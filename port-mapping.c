#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <curl/curl.h>
#include "rest_client_pool.h"
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/wait.h>
#endif
#define PRINTLASTERROR printf("WSAGetLastError: %d\n", WSAGetLastError())

#define BUFFER_SIZE 1024
#define ENCODE_SIZE 2048
#define MAX_URL_LENGTH 130


char *SERVER_ADDRESS = "127.0.0.1";
int SERVER_PORT = 8080;



// 默认缓冲区大小
#define MAX_STATIC_SIZE 1024

struct MemoryStruct {
    char static_memory[MAX_STATIC_SIZE];
    char *dynamic_memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    if (mem->size + realsize <= MAX_STATIC_SIZE) {
        // 使用静态缓冲区存储数据
        memcpy(&(mem->static_memory[mem->size]), contents, realsize);
    } else {
        if (!mem->dynamic_memory) {
            // 需要首次分配动态内存
            mem->dynamic_memory = malloc(mem->size + realsize + 1);
            if (mem->dynamic_memory == NULL) {
                fprintf(stderr, "Failed to allocate memory.\n");
                return 0;  // 内存不足时中止操作
            }
            memcpy(mem->dynamic_memory, mem->static_memory, mem->size);  // 复制已有的静态数据到动态内存中
        } else {
            // 重新分配更多内存
            char *ptr = realloc(mem->dynamic_memory, mem->size + realsize + 1);
            if (ptr == NULL) {
                fprintf(stderr, "Failed to reallocate memory.\n");
                return 0;  // 内存不足时中止操作
            }
            mem->dynamic_memory = ptr;
        }
        memcpy(&(mem->dynamic_memory[mem->size]), contents, realsize);
    }

    mem->size += realsize;
    if (mem->dynamic_memory) {
        mem->dynamic_memory[mem->size] = 0;
    } else {
        mem->static_memory[mem->size] = 0;
    }

    return realsize;
}

typedef struct {
    int connectionClosed; // 1 if "ConnectionStatus: close", 0 otherwise
} HeaderData;

static size_t constat_header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t numbytes = size * nitems;
    HeaderData *headerData = (HeaderData *)userdata;

    // 查找并解析 "ConnectionStatus" 头
    if (strncmp(buffer, "Connectionstatus: ", 18) == 0) {
        // 提取这一行头部的值部分
        const char *value = buffer + 18; // 跳过头部名称
        if (strncmp(value, "close", 5) == 0) {
            headerData->connectionClosed = 1;
        }
    }

    return numbytes; // 必须返回这个值，告诉 curl 成功处理了多少字节
}
// 执行HTTP GET请求并获取响应
int http_get(CURL *curl, const char *url, struct MemoryStruct *chunk) {
    if (curl) {
        HeaderData headerData = {0}; // 初始化为0
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
        //curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
        //curl_easy_setopt(curl, CURLOPT_PROXY, "socks5://65.20.69.106:1081");
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, constat_header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerData);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "GET request failed: %s\n", curl_easy_strerror(res));
            return -1;
        }
        if(headerData.connectionClosed){
            return -9;//原始连接已关闭
        }
        //printf("url:%s\n",url);
        return chunk->size;
    }
    return -1;
}

// 修改 POST 函数来传递二进制数据的长度
void http_post(CURL *curl, const char *url, const char *data, int len) {
    if (curl) {
//        char *encoded_data = base64_encode((const unsigned char *)data, len);
//        if (encoded_data == NULL) {
//            fprintf(stderr, "Failed to encode data to Base64.\n");
//        } else {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);
        //curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
        //curl_easy_setopt(curl, CURLOPT_PROXY, "socks5://65.20.69.106:1081");
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        // 设置HTTP头部，指明内容类型为二进制流
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "POST request failed: %s\n", curl_easy_strerror(res));
        }
        //curl_easy_cleanup(curl);
        //           free(encoded_data);  // Clean up the encoded data
//        }
//        return_curl(pool, curl);
    }
}



void PORTMAP(char *uuid, CurlPool *pool, char *tartget_ip, unsigned short target_port){
    int sock;
    struct sockaddr_in server_addr;
    unsigned char  buffer[BUFFER_SIZE];


    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        PRINTLASTERROR;
        return;
    }

    // Configure the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(target_port);
    server_addr.sin_addr.s_addr = inet_addr(tartget_ip);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        PRINTLASTERROR;
        close(sock);
        return;
    }

    printf("Get connection success,%d\n", sock);
    fd_set read_fds;
    int max_fd = sock;
    if (pool == NULL) {
        fprintf(stderr, "Failed to create curl pool\n");
        return;
    }
    CURL *recvcurl = get_curl(pool);
    CURL *sendcurl = get_curl(pool);
    char RECV_URL[MAX_URL_LENGTH];
    char SEND_URL[MAX_URL_LENGTH];
    snprintf(RECV_URL, sizeof(RECV_URL), "https://%s:%d/maprecv?client_id=%s", SERVER_ADDRESS, SERVER_PORT, uuid);
    snprintf(SEND_URL, sizeof(SEND_URL), "https://%s:%d/mapsend?client_id=%s", SERVER_ADDRESS, SERVER_PORT, uuid);
    //printf("revc:  %s\nsend:   %s\n", RECV_URL, SEND_URL);
    struct MemoryStruct chunk;
    chunk.dynamic_memory = NULL;
    chunk.size = 0;
    // Main loop: Get data via HTTP GET, send via socket, and POST response
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        struct timeval timeout;
        timeout.tv_sec = 0;      // 秒数
        timeout.tv_usec = 2000; // 微秒数（2毫秒）
        //select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        // HTTP GET
        int bytes_read = http_get(recvcurl, RECV_URL,
                                  &chunk);
        // Send to server via socket
        if (bytes_read > 0) {
            if (chunk.dynamic_memory) {
                if (send(sock, chunk.dynamic_memory, bytes_read, 0) < 0) {
                    perror("Failed to send data");
                    PRINTLASTERROR;
                    break;
                }
//                printf("\n\nrecv from client %d bytes, send to remote\nraw:\n", bytes_read);
//                for (int i = 0; i < bytes_read; i++) {
//                    printf("%02X ", (unsigned char) chunk.dynamic_memory[i]);
//                }
                free(chunk.dynamic_memory);
                chunk.size = 0;
                chunk.dynamic_memory = NULL;
            } else {
                if (send(sock, chunk.static_memory, bytes_read, 0) < 0) {
                    perror("Failed to send data");
                    PRINTLASTERROR;
                    break;
                }
                chunk.size = 0;
                chunk.dynamic_memory = NULL;
            }

            printf("\n[%s]recv from client %d bytes, send to remote", uuid,bytes_read);

//            printf("\n\nrecv from client %d bytes, send to remote\nraw:\n", bytes_read);
//            for (int i = 0; i < bytes_read; i++) {
//                printf("%02X ", (unsigned char) encode_buffer[i]);
//            }
        } else if(bytes_read == -9){
            printf("\n[%s]Original client has closed connection, kill current thread\n",uuid);

            return_curl(pool, recvcurl);
            return_curl(pool, sendcurl);
            return; //远程连接已判断为关闭，结束线程
        }

        // Receive from remote
//        if (FD_ISSET(sock, &read_fds)) {
//            int len = recv(sock, buffer, BUFFER_SIZE, 0);
//            if (len <= 0) {
//                perror("Failed to receive data");
//                PRINTLASTERROR;
//                break;
//            }
//            // HTTP POST
//            http_post(sendcurl, "http://127.0.0.1:8089/send?client_id=8688bb89-5ace-48b1-a158-dfe154429b27", buffer,
//                      len);
//            printf("\n\nrecv from remote %d bytes, send to client\nraw:\n", len);
//            for (int i = 0; i < len; i++) {
//                printf("%02X ", buffer[i]);
//            }
//        }
        int data_capacity = 0;
        int data_size = 0;
        char* data_buffer = NULL;

        while (select(max_fd + 1, &read_fds, NULL, NULL, &timeout)) {
            int recv_total = recv(sock, buffer, BUFFER_SIZE, 0);
            if (recv_total > 0) {
                if (data_size + recv_total > data_capacity) {
                    data_capacity = (data_size + recv_total) * 2;
                    char *new_buffer = realloc(data_buffer, data_capacity);
                    if (new_buffer == NULL) {
                        perror("Failed to allocate buffer");
                        free(data_buffer);

                        return_curl(pool, recvcurl);
                        return_curl(pool, sendcurl);
                        return;
                    }
                    data_buffer = new_buffer;
                }
                memcpy(data_buffer + data_size, buffer, recv_total);
                data_size += recv_total;
            } else if (recv_total == 0) {
                printf("[%s]Connection closed by the remote host.\n",uuid);
                PRINTLASTERROR;

                return_curl(pool, recvcurl);
                return_curl(pool, sendcurl);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No more data available at the moment
                    break;
                } else {
                    perror("recv failed");
                    free(data_buffer);

                    return_curl(pool, recvcurl);
                    return_curl(pool, sendcurl);
                    return;
                }
            }
        }

        if (data_size > 0 && data_buffer != NULL) {
            http_post(sendcurl, SEND_URL, data_buffer, data_size);
            printf("\n[%s]recv from remote %d bytes, send to client", uuid,data_size);

//                printf("\n\nrecv from remote %d bytes, send to client\nraw:\n", data_size);
//                for (int i = 0; i < data_size; i++) {
//                    printf("%02X ", (unsigned char)data_buffer[i]);
//                }
            free(data_buffer);
        }
    }

    close(sock);

    return_curl(pool, recvcurl);
    return_curl(pool, sendcurl);

}
