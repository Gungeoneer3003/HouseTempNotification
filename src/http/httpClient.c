#include "httpClient.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CURL_CONNECT_TIMEOUT_SECONDS
#define CURL_CONNECT_TIMEOUT_SECONDS 10L
#endif

#ifndef CURL_TOTAL_TIMEOUT_SECONDS
#define CURL_TOTAL_TIMEOUT_SECONDS 20L
#endif

#ifndef MAX_RESPONSE_BYTES
#define MAX_RESPONSE_BYTES ((size_t)64 * 1024)
#endif

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
static int curlOk(CURL* curl, CURLcode res, const char* action);
static void setCurlOptions(CURL* curl, HttpResponse* response);
static char* buildFormBody(CURL* curl, const HttpFormField* fields, size_t field_count);
static void freeEncodedFields(char** keys, char** values, size_t field_count);

void httpResponseInit(HttpResponse* response) {
    if (!response) {
        return;
    }

    response->body = NULL;
    response->size = 0;
}

void httpResponseFree(HttpResponse* response) {
    if (!response) {
        return;
    }

    free(response->body);
    response->body = NULL;
    response->size = 0;
}

int httpGet(const char* url, const char* action, HttpResponse* response) {
    if (!url || !response) {
        return 0;
    }

    httpResponseInit(response);

    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl for %s\n", action ? action : "HTTP GET");
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    setCurlOptions(curl, response);

    CURLcode res = curl_easy_perform(curl);
    int ok = curlOk(curl, res, action ? action : "HTTP GET");
    if (res == CURLE_WRITE_ERROR && response->size >= MAX_RESPONSE_BYTES) {
        fprintf(stderr, "%s response exceeded %zu bytes\n",
                action ? action : "HTTP GET", MAX_RESPONSE_BYTES);
        ok = 0;
    }

    curl_easy_cleanup(curl);
    return ok;
}

int httpPostForm(const char* url,
                 const HttpFormField* fields,
                 size_t field_count,
                 const char* action,
                 HttpResponse* response) {
    if (!url || !fields || !response) {
        return 0;
    }

    httpResponseInit(response);

    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl for %s\n", action ? action : "HTTP POST");
        return 0;
    }

    char* postfields = buildFormBody(curl, fields, field_count);
    if (!postfields) {
        fprintf(stderr, "Failed to build form body for %s\n", action ? action : "HTTP POST");
        curl_easy_cleanup(curl);
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
    setCurlOptions(curl, response);

    CURLcode res = curl_easy_perform(curl);
    int ok = curlOk(curl, res, action ? action : "HTTP POST");
    if (res == CURLE_WRITE_ERROR && response->size >= MAX_RESPONSE_BYTES) {
        fprintf(stderr, "%s response exceeded %zu bytes\n",
                action ? action : "HTTP POST", MAX_RESPONSE_BYTES);
        ok = 0;
    }

    free(postfields);
    curl_easy_cleanup(curl);
    return ok;
}

static void setCurlOptions(CURL* curl, HttpResponse* response) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TOTAL_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
}

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        return 0;
    }

    size_t total_size = size * nmemb;
    HttpResponse* response = (HttpResponse*)userp;
    if (!response) {
        return 0;
    }

    if (response->size > MAX_RESPONSE_BYTES ||
        total_size > MAX_RESPONSE_BYTES - response->size) {
        return 0;
    }

    char* ptr = realloc(response->body, response->size + total_size + 1);
    if (!ptr) {
        return 0;
    }

    response->body = ptr;
    memcpy(&(response->body[response->size]), contents, total_size);
    response->size += total_size;
    response->body[response->size] = '\0';

    return total_size;
}

static int curlOk(CURL* curl, CURLcode res, const char* action) {
    if (res != CURLE_OK) {
        fprintf(stderr, "%s failed: %s\n", action, curl_easy_strerror(res));
        return 0;
    }

    long response_code = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code) == CURLE_OK &&
        response_code != 0 &&
        (response_code < 200 || response_code >= 300)) {
        fprintf(stderr, "%s failed with HTTP status %ld\n", action, response_code);
        return 0;
    }

    return 1;
}

static char* buildFormBody(CURL* curl, const HttpFormField* fields, size_t field_count) {
    char** keys = calloc(field_count, sizeof(*keys));
    char** values = calloc(field_count, sizeof(*values));
    if (!keys || !values) {
        free(keys);
        free(values);
        return NULL;
    }

    size_t total = 1;
    for (size_t i = 0; i < field_count; i++) {
        keys[i] = curl_easy_escape(curl, fields[i].key ? fields[i].key : "", 0);
        values[i] = curl_easy_escape(curl, fields[i].value ? fields[i].value : "", 0);
        if (!keys[i] || !values[i]) {
            freeEncodedFields(keys, values, field_count);
            return NULL;
        }

        size_t key_len = strlen(keys[i]);
        size_t value_len = strlen(values[i]);
        size_t field_len = key_len + value_len + 1;
        if (i > 0) {
            field_len++;
        }

        if (field_len > ((size_t)-1) - total) {
            freeEncodedFields(keys, values, field_count);
            return NULL;
        }

        total += field_len;
    }

    char* body = malloc(total);
    if (!body) {
        freeEncodedFields(keys, values, field_count);
        return NULL;
    }

    size_t used = 0;
    for (size_t i = 0; i < field_count; i++) {
        int n = snprintf(body + used, total - used, "%s%s=%s",
                         i > 0 ? "&" : "", keys[i], values[i]);
        if (n < 0 || (size_t)n >= total - used) {
            free(body);
            freeEncodedFields(keys, values, field_count);
            return NULL;
        }
        used += (size_t)n;
    }

    freeEncodedFields(keys, values, field_count);
    return body;
}

static void freeEncodedFields(char** keys, char** values, size_t field_count) {
    if (keys) {
        for (size_t i = 0; i < field_count; i++) {
            if (keys[i]) {
                curl_free(keys[i]);
            }
        }
    }

    if (values) {
        for (size_t i = 0; i < field_count; i++) {
            if (values[i]) {
                curl_free(values[i]);
            }
        }
    }

    free(keys);
    free(values);
}
