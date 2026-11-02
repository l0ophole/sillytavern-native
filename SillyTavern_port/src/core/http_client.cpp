#include "core/http_client.h"

#include <mutex>
#include <string>

#include <curl/curl.h>

namespace st {
namespace {

std::once_flag curl_init_flag;
CURLcode curl_init_result = CURLE_OK;

void ensure_curl_initialized() {
    std::call_once(curl_init_flag, []() { curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct WriteContext {
    HttpResponse* response = nullptr;
    const HttpDataCallback* on_data = nullptr;
};

std::size_t write_callback(char* data, std::size_t size, std::size_t count, void* userdata) {
    const std::size_t total = size * count;
    auto* context = static_cast<WriteContext*>(userdata);
    context->response->body.append(data, total);
    if (context->on_data != nullptr && *context->on_data) {
        if (!(*context->on_data)(data, total)) {
            return 0;
        }
    }
    return total;
}

struct ProgressContext {
    std::atomic<bool>* cancelled = nullptr;
};

int progress_callback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* context = static_cast<ProgressContext*>(clientp);
    if (context->cancelled->load()) {
        return 1;
    }
    return 0;
}

bool has_http_scheme(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

}  // namespace

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::post(const HttpRequest& request, const HttpDataCallback& on_data) {
    HttpResponse response;
    if (!has_http_scheme(request.url)) {
        response.error = "invalid URL: " + request.url;
        return response;
    }

    ensure_curl_initialized();
    if (curl_init_result != CURLE_OK) {
        response.error = std::string("cannot initialize libcurl: ") + curl_easy_strerror(curl_init_result);
        return response;
    }

    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        response.error = "cannot create libcurl handle";
        return response;
    }

    curl_slist* header_list = nullptr;
    for (const auto& header : request.headers) {
        header_list = curl_slist_append(header_list, header.c_str());
    }
    header_list = curl_slist_append(header_list, "Expect:");

    WriteContext write_context{&response, &on_data};
    ProgressContext progress_context{&cancelled_};
    char error_buffer[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &write_context);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, request.connect_timeout_seconds);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, request.timeout_seconds);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progress_context);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer);

    const CURLcode result = curl_easy_perform(handle);

    if (result == CURLE_OK) {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);
    } else if (result == CURLE_ABORTED_BY_CALLBACK && cancelled_.load()) {
        response.cancelled = true;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);
    } else {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);
        response.error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(result);
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(handle);
    return response;
}

void HttpClient::cancel() {
    cancelled_.store(true);
}

void HttpClient::reset_cancel() {
    cancelled_.store(false);
}

}  // namespace st
