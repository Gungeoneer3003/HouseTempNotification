#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    const char* api_token;
    const char* user_key;
    const char* house_link;
    const char* cgi_part;
    const char* power_part;
    const char* log_path;
    const char* lock_path;
    char cgi_url[256];
    char shutoff_url[256];
} AppConfig;

void configInitDefaults(AppConfig* config);
int configLoad(AppConfig* config, const char* env_file);

#endif
