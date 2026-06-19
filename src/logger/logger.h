#ifndef LOGGER_H
#define LOGGER_H

int loggerAppend(const char* log_path,
                 const char* event,
                 int house,
                 int outside_air,
                 int power,
                 const char* recommendation,
                 const char* detail);
void loggerTrim(const char* log_path);

#endif
