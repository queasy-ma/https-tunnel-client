#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

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

struct connection_info {
    int sock;
    int in_use;
    char request_id[37];  // 增加 request_id 以标识每个连接
    pthread_mutex_t lock;
};

struct req_data {
    struct connection_info *conn_info;  // 指向具体的连接信息
};

struct connection_info connection_pool[MAX_CONNECTIONS];
pthread_mutex_t pool_lock;

void init_network() {
#ifdef _WIN32
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if (result != 0) {
        fprintf(stderr, "Winsock initialization failed with error: %d\n", result);
        exit(EXIT_FAILURE);
    }
#else
    // POSIX系统无需特别网络初始化
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
    proxy_addr.sin_port = htons(1081);
    proxy_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
        PRINTLASTERROR;
        return -1;
    }
    return sock;
}

void initialize_connection_pool() {
    pthread_mutex_init(&pool_lock, NULL);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        connection_pool[i].sock = create_socket();
        connection_pool[i].in_use = 0;
        connection_pool[i].request_id[0] = '\0';  // 初始化请求 ID 为空字符串
        pthread_mutex_init(&connection_pool[i].lock, NULL);
    }
}

struct connection_info* get_connection(const char* request_id) {
    pthread_mutex_lock(&pool_lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!connection_pool[i].in_use && strcmp(connection_pool[i].request_id, request_id) == 0) {
            connection_pool[i].in_use = 1;
            pthread_mutex_unlock(&pool_lock);
            return &connection_pool[i];
        }
    }
    pthread_mutex_unlock(&pool_lock);
    return NULL;  // 如果所有连接都在使用，则返回NULL
}

void release_connection(struct connection_info* connection) {
    pthread_mutex_lock(&pool_lock);
    connection->in_use = 0;
    pthread_mutex_unlock(&pool_lock);
}

size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    struct req_data *req_data = (struct req_data *)userdata;
    if (req_data == NULL || req_data->conn_info == NULL) {
        printf("Invalid request data or connection info\n");
        return 0;  // 提前返回，避免后续空指针解引用
    }

    size_t numbytes = size * nitems;
    //printf("Received header: %.*s", (int)numbytes, buffer);  // 打印接收到的整个header
    //initialize_connection_pool();
    if (strncasecmp(buffer, "X-Request-Id:", 13) == 0) {
        char *id_start = buffer + 13;
        while (*id_start == ' ') id_start++;  // 跳过键名后的空格

        char *id_end = strstr(id_start, "\r\n");
        if (id_end) {
            size_t id_length = id_end - id_start;
            if (id_length < sizeof(req_data->conn_info->request_id)) {
                strncpy(req_data->conn_info->request_id, id_start, id_length);
                req_data->conn_info->request_id[id_length] = '\0'; // 确保字符串结束
                printf("Extracted request id: %s\n", req_data->conn_info->request_id);
            }
        } else {
            printf("End of request ID not found\n");
        }
    }
    return numbytes;
}


// 发送数据到HTTP服务器并设置X-Request-ID
void post_data_to_server(const char *data, size_t data_size, const char *url, const char *request_id) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char header[256];

    sprintf(header, "X-Request-ID: %s", request_id);
    headers = curl_slist_append(headers, header);

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)data_size);
        printf("sending result...\n");
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform()1 failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
}



size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp) {
    struct req_data *data = (struct req_data *)userp;
    if (data == NULL || data->conn_info == NULL) {
        fprintf(stderr, "Invalid connection data\n");
        return 0;  // 返回0会导致curl处理失败
    }

    size_t total_size = size * nmemb;
    char response_buffer[4096];  // 假设最大响应大小
    ssize_t recv_size;

    pthread_mutex_lock(&data->conn_info->lock);
    printf("Forwarding...\n");
    send(data->conn_info->sock, buffer, total_size, 0);  // 发送数据到SOCKS5服务
    recv_size = recv(data->conn_info->sock, response_buffer, sizeof(response_buffer), 0);  // 接收响应
    pthread_mutex_unlock(&data->conn_info->lock);

    if (recv_size > 0) {
        post_data_to_server(response_buffer, recv_size, "https://127.0.0.1/reserve", data->conn_info->request_id);
        return total_size;
    }
    return total_size;  // 即使没有接收到数据也返回处理的大小，避免curl误解为写入失败
}


struct connection_info* find_unused_connection() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connection_pool[i].in_use == 0) {
            connection_pool[i].in_use = 1;  // Mark as in use
            connection_pool[i].sock = create_socket();
            return &connection_pool[i];
        }
    }
    return NULL;
}

int main() {
    init_network();

    CURL *curl = curl_easy_init();

    if (curl) {
        struct connection_info* conn_info = find_unused_connection();
        if (conn_info == NULL) {
            fprintf(stderr, "No available connection.\n");
            return EXIT_FAILURE;
        }
        struct req_data data = { conn_info };

        curl_easy_setopt(curl, CURLOPT_URL, "https://127.0.0.1/tunnel");
//        curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);


            CURLcode res = curl_easy_perform(curl);
            if(res != CURLE_OK) {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            }


        curl_easy_cleanup(curl);
    }

    cleanup_network();
    return 0;
}
