#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <curl/curl.h>
//#include <uuid/uuid.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define CLOSESOCKET closesocket
#define PRINTLASTERROR printf("WSAGetLastError: %d\n", WSAGetLastError())
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define CLOSESOCKET close
#define PRINTLASTERROR perror("Error")
#endif

#include <pthread.h>

#define MAX_CONNECTIONS 10

#define BUFFER_SIZE 1024

// 请求数据结构，包含socket和请求ID
struct request_data {
    int sock;        // SOCKS 代理socket
    char request_id[37]; // UUID string size
};



#define MAX_CONNECTIONS 10

struct connection_info {
    int sock;
    char request_id[37];
    int in_use;
};

struct connection_info connection_pool[MAX_CONNECTIONS];
pthread_mutex_t lock;
pthread_cond_t cond;

void init_network() {
#ifdef _WIN32
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if (result != 0) {
        fprintf(stderr, "Winsock initialization failed with error: %d\n", result);
        exit(EXIT_FAILURE);
    }
#else
    // No initialization needed for POSIX
#endif
}

void cleanup_network() {
#ifdef _WIN32
    WSACleanup();
#endif
}

int create_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in proxy_addr;

    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_port = htons(1080);
    proxy_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
        PRINTLASTERROR;
        return -1;
    }
    return sock;
}

void initialize_connection_pool() {
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        connection_pool[i].sock = create_socket();
        connection_pool[i].request_id[0] = '\0';
        connection_pool[i].in_use = 0;
    }
}

struct connection_info* get_connection(char* request_id) {
    pthread_mutex_lock(&lock);
    while (1) {
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (!connection_pool[i].in_use) {
                connection_pool[i].in_use = 1;
                strncpy(connection_pool[i].request_id, request_id, sizeof(connection_pool[i].request_id) - 1);
                pthread_mutex_unlock(&lock);
                return &connection_pool[i];
            }
        }
        pthread_cond_wait(&cond, &lock);
    }
}

void release_connection(struct connection_info* connection) {
    pthread_mutex_lock(&lock);
    connection->in_use = 0;
    connection->request_id[0] = '\0';
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
}


// 回调函数用于处理响应头
size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    struct request_data *req_data = (struct request_data *)userdata;
    size_t numbytes = size * nitems;

    // 检查是否为X-Request-ID头
    if (strncasecmp(buffer, "X-Request-ID:", 13) == 0) {
        // 跳过"X-Request-ID:"和前面的空格
        char *id_start = buffer + 13;
        while (*id_start == ' ') id_start++;

        // 查找行尾，确定ID结束
        char *id_end = strstr(id_start, "\r\n");
        if (id_end) {
            size_t id_length = id_end - id_start;
            if (id_length < sizeof(req_data->request_id)) {
                strncpy(req_data->request_id, id_start, id_length);
                req_data->request_id[id_length] = '\0'; // 确保字符串结束
            }
        }
    }

    return numbytes; // 总是返回这个值，表示处理了全部头部内容
}

// 数据写入回调，将数据转发到SOCKS代理
size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp) {
    struct request_data *req_data = (struct request_data *)userp;
    size_t total_size = size * nmemb;
    char response_buffer[BUFFER_SIZE];
    int bytes_received;

    // 发送数据到 SOCKS 代理
    printf("Sending data to SOCKS proxy...\n");
    if (send(req_data->sock, buffer, total_size, 0) != total_size) {
        fprintf(stderr, "Failed to send data to the SOCKS proxy\n");
        return 0; // 返回0通知libcurl发生了错误
    }

    // 接收来自 SOCKS 代理的回复
    printf("Waiting for reply from SOCKS proxy...\n");
    bytes_received = recv(req_data->sock, response_buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive data from the SOCKS proxy\n");
        return 0;
    }

    // 打印或处理回复
    response_buffer[bytes_received] = '\0';  // 确保字符串正确终止
    printf("Received from proxy: %s\n", response_buffer);

    return total_size;
}


int main() {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char uuid_str[37];
    struct request_data req_data;

//    uuid_t binuuid;
//    uuid_generate_random(binuuid);
//    uuid_unparse_lower(binuuid, uuid_str);

    curl_global_init(CURL_GLOBAL_ALL);

//    // Initialize SOCKS proxy connection
//    sock = socket(AF_INET, SOCK_STREAM, 0);
//    if (sock < 0) {
//        perror("Failed to create socket");
//        return EXIT_FAILURE;
//    }
//
//    memset(&proxy_addr, 0, sizeof(proxy_addr));
//    proxy_addr.sin_family = AF_INET;
//    proxy_addr.sin_port = htons(1080);
//    proxy_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
//
//    if (connect(sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
//        perror("Failed to connect to the proxy");
//        close(sock);
//        return EXIT_FAILURE;
//    }
//
//    req_data.sock = sock; // Set SOCKS proxy socket

    // Initialize CURL for HTTP/2 requests to tunnel server
    curl = curl_easy_init();
    if (curl) {
        char header_buffer[128];
        snprintf(header_buffer, sizeof(header_buffer), "X-Request-ID: %s", uuid_str);
        headers = curl_slist_append(headers, header_buffer);
        headers = curl_slist_append(headers, "Connection: keep-alive");

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, "https://127.0.0.1/tunnel");
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &req_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); // No timeout, wait indefinitely

        while (1) {
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                break;
            }
        }

        curl_easy_cleanup(curl);
    }

    close(sock);
    curl_global_cleanup();
    return 0;
}
