#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"

static void testJsonRecordParse(void) {
    char line[] = "{\"ts\":12345,\"fields\":[\"72\",\"a|b\",\"line\\nnext\"]}";
    LogRecord record;

    assert(logger_record_parse_line(line, &record));
    assert(record.has_logged_at);
    assert((long long)record.logged_at == 12345);
    assert(record.field_count == 3);
    assert(strcmp(record.fields[0], "72") == 0);
    assert(strcmp(record.fields[1], "a|b") == 0);
    assert(strcmp(record.fields[2], "line\nnext") == 0);
}

static void testLegacyPipeParse(void) {
    char line[] = "12345|2026-07-01|10:15:00 PM|72|65|open";
    LogRecord record;

    assert(logger_record_parse_line(line, &record));
    assert(record.has_logged_at);
    assert((long long)record.logged_at == 12345);
    assert(record.field_count == 3);
    assert(strcmp(record.fields[0], "72") == 0);
    assert(strcmp(record.fields[1], "65") == 0);
    assert(strcmp(record.fields[2], "open") == 0);
}

int main(void) {
    testJsonRecordParse();
    testLegacyPipeParse();
    puts("logger tests passed");
    return 0;
}
