#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

typedef struct {
    char* body;
    size_t size;
} HttpResponse;

typedef struct {
    const char* key;
    const char* value;
} HttpFormField;

void http_response_init(HttpResponse* response);
void http_response_free(HttpResponse* response);
int http_get(const char* url, const char* action, HttpResponse* response);
int http_post_form(const char* url,
                   const HttpFormField* fields,
                   size_t field_count,
                   const char* action,
                   HttpResponse* response);

#endif
