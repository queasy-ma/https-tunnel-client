#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <unistd.h>
#include "rest_client_pool.h"

// 写回调函数处理从服务器接收到的数据
//size_t write_callback(void *buffer, size_t size, size_t nmemb, void *userp) {
//    size_t real_size = size * nmemb;
//    printf("Received: %.*s", (int)real_size, (char *)buffer);
//    return real_size;
//}
//
//
//void http_get() {
//    CURL *curl;
//    CURLcode res;
//    struct curl_slist *headers = NULL;
//
//
//    curl_global_init(CURL_GLOBAL_ALL);
//    curl = curl_easy_init();
//
//    if (curl) {
//        curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:8081/recv");
//        curl_easy_setopt(curl, CURLOPT_POST, 1L);
//        // 设置自定义的写回调函数
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
//
//            //curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
//
//            // 发送 POST 请求并接收响应
//            res = curl_easy_perform(curl);
//            if (res != CURLE_OK) {
//                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
//            }
//        curl_easy_cleanup(curl);
//    }
//}

void http_post(){
    char data[128];
    CurlPool *pool = curl_pool_init();
    if (pool == NULL) {
        fprintf(stderr, "Failed to create curl pool\n");
        return ;
    }



    int count;
    for (count = 1;; count++) {
        // 每次循环修改data内容
        snprintf(data, sizeof(data), "Data chunk %d from client..", count);
        perform_post_request(pool, "http://127.0.0.1:8081/send", data);
        usleep(30000);
    }

    curl_pool_cleanup(pool);
    return ;
}


int main(void) {
    //http_get();
    http_post();
    return 0;
}