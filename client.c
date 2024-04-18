#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>

// 写数据的回调函数
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t real_size = size * nmemb;
    printf("%.*s", (int)real_size, ptr);  // 输出数据
    return real_size;
}

int main(void) {
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://127.0.0.1/tunnel"); // 设置你的隧道 URL
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
       // curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8080");
        // 设置写数据回调
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

        // 保持连接打开，以便连续使用
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);

        // 自动重定向
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // 执行 HTTP 请求，持续监听数据
        res = curl_easy_perform(curl);

        // 检查错误
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        // 清理
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return 0;
}
