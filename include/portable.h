#ifndef PORTABLE_H
#define PORTABLE_H

#include <time.h>

int portable_setenv(const char* name, const char* value, int overwrite);
int portable_localtime(const time_t* value, struct tm* out);
void portable_sleep_seconds(unsigned int seconds);
long long portable_process_id(void);

#endif
