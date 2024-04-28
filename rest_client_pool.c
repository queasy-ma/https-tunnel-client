#include "rest_client_pool.h"

CurlPool* curl_pool_init() {
    CurlPool *pool = malloc(sizeof(CurlPool));
    if (!pool) return NULL;

    pthread_mutex_init(&pool->lock, NULL);
    for (int i = 0; i < MAX_CURL_HANDLES; i++) {
        pool->handles[i] = curl_easy_init();
        pool->in_use[i] = 0;
    }
    return pool;
}

void curl_pool_cleanup(CurlPool *pool) {
    for (int i = 0; i < MAX_CURL_HANDLES; i++) {
        if (pool->handles[i]) {
            curl_easy_cleanup(pool->handles[i]);
        }
    }
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

CURL* get_curl(CurlPool *pool) {
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < MAX_CURL_HANDLES; i++) {
        if (!pool->in_use[i]) {
            pool->in_use[i] = 1;
            pthread_mutex_unlock(&pool->lock);
            return pool->handles[i];
        }
    }
    pthread_mutex_unlock(&pool->lock);
    return NULL; // 所有句柄都在使用中
}

void return_curl(CurlPool *pool, CURL *curl) {
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < MAX_CURL_HANDLES; i++) {
        if (pool->handles[i] == curl) {
            pool->in_use[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&pool->lock);
}

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb;
    strncat(userdata, ptr, total_size);
    return total_size;
}

// 函数用于执行POST请求
void perform_post_request(CurlPool *pool, const char *url, const char *post_data) {
    CURL *curl = get_curl(pool);
    if (curl) {
        CURLcode res;
        char response[4096] = {0};

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
        //curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
//            printf("Response: %s\n", response);
        } else {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        return_curl(pool, curl);
    } else {
        fprintf(stderr, "Unable to get curl handle\n");
    }
}

// 写回调函数处理从服务器接收到的数据
//size_t write_callback(void *buffer, size_t size, size_t nmemb, void *userp) {
//    size_t real_size = size * nmemb;
//    printf("Received from server: %.*s", (int)real_size, (char *)buffer);
//    return real_size;
//}

//void perform_get_request(CurlPool *pool,const char *url) {
//    CURL *curl = get_curl(pool);
//    CURLcode res;
//    struct curl_slist *headers = NULL;
//
//    if (curl) {
//        curl_easy_setopt(curl, CURLOPT_URL, url);
////        curl_easy_setopt(curl, CURLOPT_POST, 1L);
//        // 设置自定义的写回调函数
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
//        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
//
//        curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
//
//        // 发送 POST 请求并接收响应
//        res = curl_easy_perform(curl);
//        if (res != CURLE_OK) {
//            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
//        }
//        return_curl(pool, curl);
//    }
//}