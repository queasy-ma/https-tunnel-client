#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

int main(void) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://your-go-server:8080");

        // Set HTTP/2
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

        // Set custom headers
        headers = curl_slist_append(headers, "Upgrade: h2c");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set the CONNECT method
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");

        // Set the proxy tunnel
        curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);

        // Set the target host
        curl_easy_setopt(curl, CURLOPT_PROXY, "target-host:target-port");

        // Perform the request
        res = curl_easy_perform(curl);

        if(res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return 0;
}
