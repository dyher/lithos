#include "http_client_bridge.h"
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>

static std::mutex g_http_mutex;
static int g_next_id = 1;

struct HttpResult {
    bool ready;
    int status_code;
    std::string body;
};
static std::map<int, HttpResult> g_results;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static void do_request(int id, std::string url, std::string method, std::string body) {
    CURL *curl = curl_easy_init();
    HttpResult res;
    res.ready = false;
    res.status_code = 0;
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());
        }
        
        CURLcode r = curl_easy_perform(curl);
        if (r == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            res.status_code = (int)http_code;
        } else {
            res.body = std::string("CURL_ERROR: ") + curl_easy_strerror(r);
            res.status_code = -1;
        }
        curl_easy_cleanup(curl);
    }
    res.ready = true;
    
    std::lock_guard<std::mutex> lock(g_http_mutex);
    g_results[id] = res;
}

extern "C" int bridge_start_request(const char *url, const char *method, const char *body) {
    int id;
    {
        std::lock_guard<std::mutex> lock(g_http_mutex);
        id = g_next_id++;
    }
    std::thread t(do_request, id, std::string(url), std::string(method), std::string(body ? body : ""));
    t.detach();
    return id;
}

extern "C" int bridge_poll_result(int id, int *out_status, char **out_body) {
    std::lock_guard<std::mutex> lock(g_http_mutex);
    auto it = g_results.find(id);
    if (it != g_results.end() && it->second.ready) {
        HttpResult res = it->second;
        g_results.erase(it);
        
        *out_status = res.status_code;
        *out_body = (char*)malloc(res.body.length() + 1);
        strcpy(*out_body, res.body.c_str());
        return 1;
    }
    return 0;
}
