//
// Created by admin on 2024/4/26.
//

#ifndef HTTPS_TUNNEL_CLIENT_REST_CLIENT_POOL_H
#define HTTPS_TUNNEL_CLIENT_REST_CLIENT_POOL_H
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>

#define MAX_CURL_HANDLES 10

typedef struct {
    CURL *handles[MAX_CURL_HANDLES];
    int in_use[MAX_CURL_HANDLES];
    pthread_mutex_t lock;
} CurlPool;

CurlPool* curl_pool_init();
void curl_pool_cleanup(CurlPool *pool);
CURL* get_curl(CurlPool *pool);
void return_curl(CurlPool *pool, CURL *curl);
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
//size_t write_callback(void *buffer, size_t size, size_t nmemb, void *userp);
void perform_post_request(CurlPool *pool, const char *url, const char *post_data);
void perform_get_request(CurlPool *pool,const char *url);
#endif //HTTPS_TUNNEL_CLIENT_REST_CLIENT_POOL_H
