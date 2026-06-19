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

void httpResponseInit(HttpResponse* response);
void httpResponseFree(HttpResponse* response);
int httpGet(const char* url, const char* action, HttpResponse* response);
int httpPostForm(const char* url,
                 const HttpFormField* fields,
                 size_t field_count,
                 const char* action,
                 HttpResponse* response);

#endif
