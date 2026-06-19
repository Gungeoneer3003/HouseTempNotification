#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "portable.h"

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

int portableSetenv(const char* name, const char* value, int overwrite) {
#ifdef _WIN32
    if (!overwrite && getenv(name)) {
        return 0;
    }

    return _putenv_s(name, value) == 0 ? 0 : -1;
#else
    return setenv(name, value, overwrite);
#endif
}

int portableLocaltime(const time_t* value, struct tm* out) {
    if (!value || !out) {
        return 0;
    }

#ifdef _WIN32
    return localtime_s(out, value) == 0;
#else
    return localtime_r(value, out) != NULL;
#endif
}

void portableSleepSeconds(unsigned int seconds) {
#ifdef _WIN32
    while (seconds > 0) {
        DWORD chunk = seconds > 86400U ? 86400U : (DWORD)seconds;
        Sleep(chunk * 1000U);
        seconds -= chunk;
    }
#else
    sleep(seconds);
#endif
}

long long portableProcessId(void) {
#ifdef _WIN32
    return (long long)_getpid();
#else
    return (long long)getpid();
#endif
}
