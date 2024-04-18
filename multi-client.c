#include <stdio.h>
#include <curl/curl.h>

static size_t write_callback(void *buffer, size_t size, size_t nmemb, void *userp) {
    printf("Received: %.*s\n", (int)(size * nmemb), (char *)buffer);
    return size * nmemb;
}

int main(void) {
    CURL *curl;
    CURLM *multi_handle;
    int still_running = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    multi_handle = curl_multi_init();

    // Create several easy handles to perform multiple requests simultaneously
    for (int i = 0; i < 3; i++) {
        curl = curl_easy_init();
        if(curl) {
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2_0);
            curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:8443/");
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_multi_add_handle(multi_handle, curl);
        }
    }

    curl_multi_perform(multi_handle, &still_running);

    do {
        int numfds;
        curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
        curl_multi_perform(multi_handle, &still_running);
    } while(still_running);

    curl_multi_cleanup(multi_handle);
    curl_global_cleanup();

    return 0;
}
