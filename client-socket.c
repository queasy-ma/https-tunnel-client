#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <curl/curl.h>
#include "rest_client_pool.h"
#include "dns.h"
#include "conn-hash-tabale.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#define sleep(x) Sleep(1000 * (x))
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <getopt.h>
#endif

void PRINTLASTERROR() {
#ifdef _WIN32
    fprintf(stderr, "Error: %d\n", WSAGetLastError());
#else
    perror("Error");
#endif
}

#define PRINTLASTERROR PRINTLASTERROR()

#define BUFFER_SIZE 1024
#define MAX_RETRIES 5
#define ENCODE_SIZE 2048
void close_connection(const char *base_url, const char *uuid);

//#define SERVER_IP "114.116.239.178"  // The IP address of the server to connect with socket
//#define SERVER_PORT 22          // The port of the server to connect with socket

//#define SERVER_IP "198.18.0.102"
//#define SERVER_PORT 80

//#define SERVER_IP "127.0.0.1"
//#define SERVER_PORT 8888
#define MAX_URL_LENGTH 130
char *SERVER_ADDRESS = "127.0.0.1";
int SERVER_PORT = 8089;
char CLOSE_URL[MAX_URL_LENGTH];



void initialize_curl() {
    curl_global_init(CURL_GLOBAL_ALL);
}

void cleanup_curl() {
    curl_global_cleanup();
}

typedef struct {
    char  *buffer;
    size_t size;
} ResponseData;

typedef struct {
    char *uuid;
    CurlPool *pool; // 替换为正确的类型，如果它是一个结构体
    char *target;
    unsigned short target_port;
    bool isDomain;
} ConnectionArgs;


void kill_current_thread() {
#ifdef _WIN32
    ExitThread(0);
#else
    pthread_exit(NULL);
#endif
}
//base64解码版本
//size_t write_data(void *ptr, size_t size, size_t nmemb, void *userp) {
//    size_t real_size = size * nmemb;
//    ResponseData *resp = (ResponseData *)userp;
//    if (resp->size + real_size > ENCODE_SIZE ) { // 检查缓冲区是否有足够的空间
//        fprintf(stderr,"buffer erro\n");
//        return 0; // 返回0通知libcurl停止传输并返回错误
//    }
//    memcpy(resp->buffer + resp->size, ptr, real_size);
//    resp->size += real_size;
//    return real_size;
//}
//
//int http_get(CURL *curl, const char *url, char *response) {
//    if (curl) {
//        ResponseData resp_data = {response, 0};
//        curl_easy_setopt(curl, CURLOPT_URL, url);
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
//        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_data);
//        //curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
//
//        CURLcode res = curl_easy_perform(curl);
//        if (res != CURLE_OK) {
//            fprintf(stderr, "GET request failed: %s\n", curl_easy_strerror(res));
//            //curl_easy_cleanup(curl);
//            return -1;
//        }
//        if (res == CURLE_OK) {
//            size_t decoded_size;
//            unsigned char *decoded_data = base64_decode(response, resp_data.size, &decoded_size);
//            if (decoded_data == NULL) {
//                fprintf(stderr, "Failed to decode base64 data.\n");
//                return -1;  // 或处理错误的其他方式
//            }
//
//            // 确保你有足够的空间来存储解码后的数据
//            if (decoded_size > BUFFER_SIZE) {
//                free(decoded_data);
//                return -1;  // 缓冲区太小
//            }
//
//            memcpy(response, decoded_data, decoded_size);
//            free(decoded_data);
//            //curl_easy_cleanup(curl);
//            return (int)decoded_size;  // 返回解码后的实际大小
//        }
////        return_curl(pool, curl);
//    }
//    return -1;
//}

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
    long http_code = 0;
    if (curl) {
        HeaderData headerData = {0}; // 初始化为0
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
        //curl_easy_setopt(curl, CURLOPT_PROXY, "http://10.5.1.115:8080");
        //curl_easy_setopt(curl, CURLOPT_PROXY, "socks5://65.20.69.106:1081");
//        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, constat_header_callback);
//        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerData);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "GET request failed: %s\n", curl_easy_strerror(res));
            return -1;
        }
        if(http_code == 404){
            return -9;
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
//            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
//            curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
//            curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
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


void close_connection(const char *base_url, const char *uuid) {
    deleteConnection(uuid);
    CURL *curl;
    CURLcode res;
    char *full_url;
    int url_length;

    // 初始化 CURL
    curl = curl_easy_init();
    if (curl) {
        // 计算完整 URL 的长度（包括 '?' 和 '=' 等字符）
        url_length = snprintf(NULL, 0, "%s?client_id=%s", base_url, uuid) + 1;
        // 分配内存以构建完整的 URL
        full_url = malloc(url_length);
        if (full_url == NULL) {
            fprintf(stderr, "Failed to allocate memory for URL.\n");
            return;
        }
        snprintf(full_url, url_length, "%s?client_id=%s", base_url, uuid);

        // 设置 CURL 选项
        curl_easy_setopt(curl, CURLOPT_URL, full_url);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // 执行 GET 请求
        res = curl_easy_perform(curl);
        printf("Send close sing to server...\n");
        if (res != CURLE_OK) {
            fprintf(stderr, "close_connection() - curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        // 清理
        curl_easy_cleanup(curl);
        free(full_url);
    } else {
        fprintf(stderr, "Failed to initialize CURL.\n");
    }
}


void check_connection(int sockfd) {
    int error = 0;
    socklen_t len = sizeof(error);
    int retval = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void *)&error, &len);

    if (retval != 0) {
#ifdef _WIN32
        fprintf(stderr, "getsockopt failed: %d\n", WSAGetLastError());
#else
        fprintf(stderr, "getsockopt failed: %s\n", strerror(retval));
#endif
        return;
    }

    if (error != 0) {
#ifdef _WIN32
        fprintf(stderr, "socket error: %d\n", error);
#else
        fprintf(stderr, "socket error: %s\n", strerror(error));
#endif
    } else {
        printf("No socket error, but connection may be closed by peer\n");
    }
}


int create_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        PRINTLASTERROR;
    }
    return sock;
}

int connect_with_retries(int sock, struct sockaddr_in *server_addr, int retries) {
    for (int i = 0; i < retries; ++i) {
        if (connect(sock, (struct sockaddr *)server_addr, sizeof(*server_addr)) == 0) {
            return 0;
        }
        PRINTLASTERROR;
        sleep(1); // 等待1秒后重试
    }
    return -1;
}

//void configure_keepalive(int sock) {
//    int optval = 1;
//    socklen_t optlen = sizeof(optval);
//
//    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optval, optlen) < 0) {
//        PRINTLASTERROR;
//    }
//
//#ifdef _WIN32
//    DWORD bytesReturned = 0;
//    struct tcp_keepalive keepaliveSettings;
//    keepaliveSettings.onoff = 1;
//    keepaliveSettings.keepalivetime = 60000;  // 60秒无数据开始探测
//    keepaliveSettings.keepaliveinterval = 10000; // 每10秒探测一次
//
//    if (WSAIoctl(sock, SIO_KEEPALIVE_VALS, &keepaliveSettings, sizeof(keepaliveSettings), NULL, 0, &bytesReturned, NULL, NULL) != 0) {
//        PRINTLASTERROR;
//    }
//#else
//    int keepidle = 60;  // 60秒无数据开始探测
//    int keepintvl = 10; // 每10秒探测一次
//    int keepcnt = 5;    // 探测5次无响应后认为连接失效
//
//    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, optlen) < 0) {
//        PRINTLASTERROR();
//    }
//
//    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, optlen) < 0) {
//        PRINTLASTERROR();
//    }
//
//    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, optlen) < 0) {
//        PRINTLASTERROR();
//    }
//#endif
//}

void set_socket_timeout(int sock, int timeout_sec) {
    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        PRINTLASTERROR;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        PRINTLASTERROR;
    }
}

void close_socket(int sock, const char* url, const char* uuid) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    close_connection(url, uuid);
}


void P2PCONNECTION(char *uuid, CurlPool *pool, char *tartget_ip, unsigned short target_port){
    int sock;
    struct sockaddr_in server_addr;
    unsigned char  buffer[BUFFER_SIZE] ;


    // Create socket
    sock = create_socket();
    if (sock < 0) {
        close_connection(CLOSE_URL, uuid);
    }

    // Configure the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(target_port);
    server_addr.sin_addr.s_addr = inet_addr(tartget_ip);

    // Connect to the target with retries
    if (connect_with_retries(sock, &server_addr, MAX_RETRIES) < 0) {
        close_socket(sock, CLOSE_URL, uuid);
    }
   // Configure keepalive
    //configure_keepalive(sock);
    printf("Get connection success,%d\n", sock);
    // Set socket timeouts
    set_socket_timeout(sock, 30); // 30秒超时

    fd_set read_fds;
    int max_fd = sock;
    if (pool == NULL) {
        fprintf(stderr, "Failed to create curl pool\n");
        close_socket(sock, CLOSE_URL, uuid);
        return;
    }
    CURL *recvcurl = get_curl(pool);
    CURL *sendcurl = get_curl(pool);
    char RECV_URL[MAX_URL_LENGTH];
    char SEND_URL[MAX_URL_LENGTH];
//    snprintf(RECV_URL, sizeof(RECV_URL), "https://%s:%d/maprecv?client_id=%s", SERVER_ADDRESS, SERVER_PORT, uuid);
//    snprintf(SEND_URL, sizeof(SEND_URL), "https://%s:%d/mapsend?client_id=%s", SERVER_ADDRESS, SERVER_PORT, uuid);

    snprintf(RECV_URL, sizeof(RECV_URL), "https://%s:%d/recv?client_id=%s", SERVER_ADDRESS, SERVER_PORT, uuid);
    snprintf(SEND_URL, sizeof(SEND_URL), "https://%s:%d/send?client_id=%s", SERVER_ADDRESS, SERVER_PORT, uuid);
    //printf("revc:  %s\nsend:   %s\n", RECV_URL, SEND_URL);
    struct MemoryStruct chunk;
    chunk.dynamic_memory = NULL;
    chunk.size = 0;
    if(!changeConnection(uuid, sock)){
        printf("[%s]Add socks error! kill current thread\n",uuid);
        deleteConnection(uuid);
        close_socket(sock, CLOSE_URL, uuid);
        return_curl(pool, recvcurl);
        return_curl(pool, sendcurl);
        return;
    }
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

//                printf("\n\n[%s]recv from client %d bytes, send to remote\nraw:\n",uuid,  bytes_read);
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
//                printf("\n\n[%s]recv from client %d bytes, send to remote\nraw:\n",uuid,  bytes_read);
//                for (int i = 0; i < bytes_read; i++) {
//                    printf("%02X ", (unsigned char) chunk.static_memory[i]);
//                }
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
            deleteConnection(uuid);
            close_socket(sock, CLOSE_URL, uuid);
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
                        close_socket(sock, CLOSE_URL, uuid);
                        deleteConnection(uuid);
                        return_curl(pool, recvcurl);
                        return_curl(pool, sendcurl);
                        return;
                    }
                    data_buffer = new_buffer;
                }
                memcpy(data_buffer + data_size, buffer, recv_total);
                data_size += recv_total;
            } else if (recv_total == 0) {
                printf("\n[%s]Connection closed by the remote host.\n",uuid);
                PRINTLASTERROR;
                check_connection(sock);
                close_socket(sock, CLOSE_URL, uuid);
                deleteConnection(uuid);
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
                    close_socket(sock, CLOSE_URL, uuid);
                    deleteConnection(uuid);
                    return_curl(pool, recvcurl);
                    return_curl(pool, sendcurl);
                    return;
                }
            }
        }

        if (data_size > 0 && data_buffer != NULL) {
            http_post(sendcurl, SEND_URL, data_buffer, data_size);
            printf("\n[%s]recv from remote %d bytes, send to client", uuid,data_size);

//                printf("\n\n[%s]recv from remote %d bytes, send to client\nraw:\n",uuid, data_size);
//                for (int i = 0; i < data_size; i++) {
//                    printf("%02X ", (unsigned char)data_buffer[i]);
//                }

            free(data_buffer);
        }
    }

    close_socket(sock, CLOSE_URL, uuid);
    deleteConnection(uuid);
    return_curl(pool, recvcurl);
    return_curl(pool, sendcurl);

}


// 线程启动函数
#ifdef _WIN32
unsigned __stdcall thread_start(void *arg) {
#else
    void* thread_start(void *arg) {
#endif
    ConnectionArgs *conn_args = (ConnectionArgs*) arg;
        if(conn_args->isDomain) {
            char ipAddr[1024];
            printf("Domain name: %s\n",conn_args->target);
            if (gethostbydomain(conn_args->target, ipAddr, 1024) == 0){
                printf("Starting thread ... uuid-%s target-%s port-%d\n",conn_args->uuid, ipAddr, conn_args->target_port);
                P2PCONNECTION(conn_args->uuid, conn_args->pool, ipAddr, conn_args->target_port);
                free(conn_args->uuid);
                free(conn_args->target);
                free(conn_args);
            } else{
                close_connection(CLOSE_URL, conn_args->uuid);
            }
        } else{
            printf("Starting thread ... uuid-%s target-%s port-%d\n",conn_args->uuid, conn_args->target, conn_args->target_port);
            P2PCONNECTION(conn_args->uuid, conn_args->pool, conn_args->target, conn_args->target_port);
            free(conn_args->uuid);
            free(conn_args->target);
            free(conn_args);
        }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

void create_connection_thread(char *uuid, CurlPool *pool, char *target, unsigned short target_port, bool isDomain) {
    ConnectionArgs *args = malloc(sizeof(ConnectionArgs));
    if (args == NULL) {
        fprintf(stderr, "[%s]Failed to allocate memory for thread arguments.\n", uuid);
        return;
    }

    args->pool = pool;
    args->uuid = strdup(uuid);  // 深拷贝字符串
    args->target = strdup(target);
    args->target_port = target_port;
    args->isDomain = isDomain;
#ifdef _WIN32
    uintptr_t thrd = _beginthreadex(NULL, 0, thread_start, args, 0, NULL);
    if (thrd == 0) {
        fprintf(stderr, "Failed to create thread.\n");
        deleteConnection(uuid);
        free(args); // 在创建线程失败时释放内存
    } else {
        CloseHandle((HANDLE)thrd); // 不需要等待这个线程
    }
#else
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, thread_start, args) != 0) {
        fprintf(stderr, "Failed to create thread.\n");
        deleteConnection(uuid);
        free(args); // 在创建线程失败时释放内存
    } else {
        pthread_detach(thread_id); // 不需要等待这个线程
    }
#endif
}

char* base64Decode(const char* input) {
    const char base64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";
    int T[256];
    memset(T, -1, sizeof(T));
    for (int i = 0; i < 64; i++) {
        T[(int)base64_chars[i]] = i;
    }

    char* out = (char*)malloc(strlen(input) * 3 / 4 + 1);
    if (!out) {
        return NULL; // 更改这里为返回 NULL
    }

    int val = 0, valb = -8;
    int outLen = 0;
    for (; *input; input++) {
        unsigned char c = *input;
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out[outLen++] = (char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    out[outLen] = '\0';

    return out;
}


//int resolve_domain_name(const char *domain_name, ResolvedAddress *target_addr) {
//    struct addrinfo hints, *res;
//    int status;
//
//    memset(&hints, 0, sizeof(hints));
//    hints.ai_family = AF_INET;        // Specify the address family (IPv4)
//    hints.ai_socktype = SOCK_STREAM;  // Specify the socket type
//
//    // Resolve the domain name to an IP address and service to a port number
//    if ((status = getaddrinfo(domain_name, NULL, &hints, &res)) != 0) {
//        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
//        return -1;
//    }
//
//    // Use the first result
//    struct sockaddr_in *addr = (struct sockaddr_in *) res->ai_addr;
//    inet_ntop(AF_INET, &(addr->sin_addr), target_addr->ip, INET_ADDRSTRLEN);
//    target_addr->port = ntohs(addr->sin_port);  // Convert to host byte order
//
//    freeaddrinfo(res);  // Free the linked list
//    return 0;
//}

// libcurl 的内存回调函数
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    char **response = (char **)userp;
    *response = realloc(*response, realsize + 1);
    if(*response == NULL) {
        // out of memory!
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }
    memcpy(*response, contents, realsize);
    (*response)[realsize] = '\0';
    return realsize;
}

void fetch_and_connect(CurlPool *curl,  CurlPool *pool) {
    CURLcode res;
    char *response = NULL;
    char INFO_URL[MAX_URL_LENGTH];
    snprintf(INFO_URL, sizeof(INFO_URL), "https://%s:%d/info", SERVER_ADDRESS, SERVER_PORT);
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, INFO_URL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        //curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else if (response && strlen(response) > 0) {
            // 处理 response
            //char *decoded = base64Decode(response);
            char *token = strtok(response, "|");
            while(token) {
                char *decoded = base64Decode(token);
                if (!decoded) {
                    fprintf(stderr, "Failed to decode base64 string.\n");
                    return; // 解码失败处理
                }
                printf("\nDecode connection info:%s\n",decoded);
                int isDomain;
                char *uuid, *target, *port_str;
                unsigned short target_port;

                char *subtoken = strtok(decoded, ";");
                if (subtoken == NULL) {
                    fprintf(stderr, "Decoded string format error.\n");
                    free(decoded);
                    return; // 格式错误处理
                }
                isDomain = atoi(subtoken);
                subtoken = strtok(NULL, ";");
                if (subtoken == NULL) {
                    fprintf(stderr, "Decoded string format error: Missing UUID.\n");
                    free(decoded);
                    return; // 格式错误处理
                }
                uuid = subtoken;
                if(findConnection(uuid)){
                    free(decoded);
                    return;
                } else if(!addConnection(uuid, 0)){
                    printf("[%s]Add connection info error\n",uuid);
                    free(decoded);
                    return;
                }
                subtoken = strtok(NULL, ";");
                if (subtoken == NULL) {
                    fprintf(stderr, "Decoded string format error: Missing target.\n");
                    free(decoded);
                    return; // 格式错误处理
                }
                target = subtoken;
                subtoken = strtok(NULL, ";");
                if (subtoken == NULL) {
                    fprintf(stderr, "Decoded string format error: Missing port.\n");
                    free(decoded);
                    return; // 格式错误处理
                }
                port_str = subtoken;
                target_port = (unsigned short) atoi(port_str);
                printf("Get conncet info :uuid-%s target-%s port-%d\n",uuid, target, target_port);
                if (isDomain == 0) { // IP
                    create_connection_thread(uuid, pool, target, target_port, false);
                } else { // Domain
                        create_connection_thread(uuid, pool, target, target_port, true);
                }
                token = strtok(NULL, "|");
                free(decoded); // 假设 base64Decode 返回的是动态分配的内存
            }
        }
        free(response);
        //curl_easy_cleanup(curl);
    }
}

void fetch_map_and_connect(CurlPool *curl,  CurlPool *pool) {
    CURLcode res;
    char *response = NULL;
    char INFO_URL[MAX_URL_LENGTH];
    snprintf(INFO_URL, sizeof(INFO_URL), "https://%s:%d/mapinfo", SERVER_ADDRESS, SERVER_PORT);
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, INFO_URL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        //curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else if (response && strlen(response) > 0) {
            // 处理 response
            //char *decoded = base64Decode(response);
            char *token = strtok(response, "|");
            while(token) {
                char *decoded = base64Decode(token);
                if (!decoded) {
                    fprintf(stderr, "Failed to decode base64 string.\n");
                    return; // 解码失败处理
                }
                printf("\nDecode connection info:%s\n",decoded);
                char *uuid, *target, *port_str;
                unsigned short target_port;


                char *subtoken = strtok(decoded, ";");
                if (subtoken == NULL) {
                    fprintf(stderr, "Decoded string format error: Missing UUID.\n");
                    free(decoded);
                    return; // 格式错误处理
                }
                uuid = subtoken;
                if(findConnection(uuid)){
                    free(decoded);
                    return;
                } else if(!addConnection(uuid, 0)){
                    printf("[%s]Add connection info error\n",uuid);
                    free(decoded);
                    return;
                }
                subtoken = strtok(NULL, ";");
                if (subtoken == NULL) {
                    fprintf(stderr, "Decoded string format error: Missing target.\n");
                    free(decoded);
                    return; // 格式错误处理
                }
                target = subtoken;
                subtoken = strtok(NULL, ";");
                if (subtoken == NULL) {
                    fprintf(stderr, "Decoded string format error: Missing port.\n");
                    free(decoded);
                    return; // 格式错误处理
                }
                port_str = subtoken;
                target_port = (unsigned short) atoi(port_str);
                printf("Get conncet info :uuid-%s target-%s port-%d\n",uuid, target, target_port);
                create_connection_thread(uuid, pool, target, target_port, false);
                token = strtok(NULL, "|");
                free(decoded); // 假设 base64Decode 返回的是动态分配的内存
            }
        }
        free(response);
        //curl_easy_cleanup(curl);
    }
}



void display_help() {
    printf("Usage: program [options]\n");
    printf("Options:\n");
    printf("  -s, --server-address <address>  Set the server IP address (default: 127.0.0.1)\n");
    printf("  -p, --server-port <port>        Set the server port (default: 8089)\n");
    printf("  --help                           Display this help message and exit\n");
}

#ifdef _WIN32
void parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            display_help();
            exit(0);
        } else if ((strcmp(argv[i], "--server-address") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
            SERVER_ADDRESS = argv[++i];
        } else if ((strcmp(argv[i], "--server-port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            SERVER_PORT = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(1);
        }
    }
}
#else
void parse_arguments(int argc, char *argv[]) {
    int opt;
    const char* short_opts = "s:p:";
    struct option long_options[] = {
        {"server-address", required_argument, NULL, 's'},
        {"server-port", required_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, short_opts, long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                SERVER_ADDRESS = optarg;
                break;
            case 'p':
                SERVER_PORT = atoi(optarg);
                break;
            case 'h':
                display_help();
                exit(0);
                break;
            default:
                display_help();
                exit(1);
                break;
        }
    }
}
#endif



int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);
    printf("Server Address: %s\n", SERVER_ADDRESS);
    printf("Server Port: %d\n", SERVER_PORT);
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
    initialize_curl();
    initializeTable();
    //snprintf(CLOSE_URL, sizeof(CLOSE_URL), "https://%s:%d/mapclose", SERVER_ADDRESS, SERVER_PORT);
    snprintf(CLOSE_URL, sizeof(CLOSE_URL), "https://%s:%d/close", SERVER_ADDRESS, SERVER_PORT);
    CurlPool *pool = curl_pool_init();
    CURL *infocurl = get_curl(pool);
    while (true){
        fetch_and_connect(infocurl ,pool);
        //fetch_map_and_connect(infocurl, pool);
        sleep(1);
    }
    cleanup_curl();

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;


}
