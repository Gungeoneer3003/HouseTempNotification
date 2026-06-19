#ifndef PORTABLE_H
#define PORTABLE_H

#include <time.h>

int portableSetenv(const char* name, const char* value, int overwrite);
int portableLocaltime(const time_t* value, struct tm* out);
void portableSleepSeconds(unsigned int seconds);
long long portableProcessId(void);

#endif
