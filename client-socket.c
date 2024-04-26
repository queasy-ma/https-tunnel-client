#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <curl/curl.h>
#include "base64.h"
#include "rest_client_pool.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
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
#endif
#define PRINTLASTERROR printf("WSAGetLastError: %d\n", WSAGetLastError())

#define BUFFER_SIZE 1024
#define ENCODE_SIZE 2048
#define SERVER_IP "114.116.239.178"  // The IP address of the server to connect with socket
#define SERVER_PORT 22          // The port of the server to connect with socket

//#define SERVER_IP "198.18.0.63"
//#define SERVER_PORT 80

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

size_t write_data(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    ResponseData *resp = (ResponseData *)userp;
    if (resp->size + real_size > ENCODE_SIZE ) { // 检查缓冲区是否有足够的空间
        fprintf(stderr,"buffer erro\n");
        return 0; // 返回0通知libcurl停止传输并返回错误
    }
    memcpy(resp->buffer + resp->size, ptr, real_size);
    resp->size += real_size;
    return real_size;
}

int http_get(CURL *curl, const char *url, char *response) {
    if (curl) {
        ResponseData resp_data = {response, 0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_data);
        curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "GET request failed: %s\n", curl_easy_strerror(res));
            //curl_easy_cleanup(curl);
            return -1;
        }
        if (res == CURLE_OK) {
            size_t decoded_size;
            unsigned char *decoded_data = base64_decode(response, resp_data.size, &decoded_size);
            if (decoded_data == NULL) {
                fprintf(stderr, "Failed to decode base64 data.\n");
                return -1;  // 或处理错误的其他方式
            }

            // 确保你有足够的空间来存储解码后的数据
            if (decoded_size > BUFFER_SIZE) {
                free(decoded_data);
                return -1;  // 缓冲区太小
            }

            memcpy(response, decoded_data, decoded_size);
            free(decoded_data);
            //curl_easy_cleanup(curl);
            return (int)decoded_size;  // 返回解码后的实际大小
        }
//        return_curl(pool, curl);
    }
    return -1;
}

// 修改 POST 函数来传递二进制数据的长度
void http_post(CURL *curl, const char *url, const char *data, int len) {
    if (curl) {
        char *encoded_data = base64_encode((const unsigned char *)data, len);
        if (encoded_data == NULL) {
            fprintf(stderr, "Failed to encode data to Base64.\n");
        } else {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, encoded_data);
            curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                fprintf(stderr, "POST request failed: %s\n", curl_easy_strerror(res));
            }
            //curl_easy_cleanup(curl);
            free(encoded_data);  // Clean up the encoded data
        }
//        return_curl(pool, curl);
    }
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char  encode_buffer[ENCODE_SIZE];
    unsigned char  buffer[BUFFER_SIZE] ;

#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    initialize_curl();

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        PRINTLASTERROR;
        exit(EXIT_FAILURE);
    }

    // Configure the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        PRINTLASTERROR;
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Get connection success,%d\n", sock);
    fd_set read_fds;
    int max_fd = sock;
    CurlPool *pool = curl_pool_init();
    if (pool == NULL) {
        fprintf(stderr, "Failed to create curl pool\n");
        return 0;
    }
    CURL *recvcurl = get_curl(pool);
    CURL *sendcurl = get_curl(pool);


    // Main loop: Get data via HTTP GET, send via socket, and POST response
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        struct timeval timeout;
        timeout.tv_sec = 0;      // 秒数
        timeout.tv_usec = 2000; // 微秒数（2毫秒）
        select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        // HTTP GET
        int bytes_read = http_get(recvcurl, "http://127.0.0.1:8089/recv?client_id=8688bb89-5ace-48b1-a158-dfe154429b27", encode_buffer);

        // Send to server via socket
        if(bytes_read > 0){
//            unsigned char u_encode_buffer[ENCODE_SIZE];
//            for (int i = 0; i < ENCODE_SIZE; i++) {
//                u_encode_buffer[i] = (unsigned char)encode_buffer[i];
//            }
            if (send(sock, encode_buffer, bytes_read, 0) < 0) {
                perror("Failed to send data");
                PRINTLASTERROR;
                break;
            }
            printf("\n\nrecv from client %d bytes, send to remote\nraw:\n", bytes_read);
            for(int i = 0; i < bytes_read; i++) {
                printf("%02X ", (unsigned char)encode_buffer[i]);
            }
        }

        // Receive from remote
        if (FD_ISSET(sock, &read_fds)) {
            int len = recv(sock, buffer, BUFFER_SIZE, 0);
            if (len <= 0) {
                perror("Failed to receive data");
                PRINTLASTERROR;
                break;
            }
            // HTTP POST
            http_post(sendcurl, "http://127.0.0.1:8089/send?client_id=8688bb89-5ace-48b1-a158-dfe154429b27", buffer, len);
            printf("\n\nrecv from remote %d bytes, send to client\nraw:\n", len);
            for(int i = 0; i < len; i++) {
                printf("%02X ", buffer[i]);
            }

        }

    }

    close(sock);
    cleanup_curl();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}