#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "jsonUtils.h"
#include "rec.h"
#include "settings.h"

static time_t localTimeAtHour(int hour) {
    struct tm local;
    memset(&local, 0, sizeof(local));
    local.tm_year = 126;
    local.tm_mon = 6;
    local.tm_mday = 1;
    local.tm_hour = hour;
    local.tm_isdst = -1;
    return mktime(&local);
}

static void testRecommendations(void) {
    assert(getRec(75, 75 - MARGIN, 0) == REC_OPEN);
    assert(getRec(75, 75 - MARGIN + 1, 0) == REC_NONE);
    assert(getRec(70, 70 + MARGIN, 1) == REC_CLOSE);
    assert(getRec(70, 70 + MARGIN - 1, 1) == REC_NONE);

    assert(strcmp(getRecName(REC_OPEN), "open") == 0);
    assert(strcmp(getRecName(REC_CLOSE), "close") == 0);
    assert(strcmp(getRecName(REC_NONE), "none") == 0);

    assert(withinWindow(REC_OPEN, localTimeAtHour(ALLOW_OPEN_AFTER_HOUR)));
    assert(!withinWindow(REC_OPEN, localTimeAtHour(ALLOW_OPEN_AFTER_HOUR - 1)));
    assert(withinWindow(REC_CLOSE, localTimeAtHour(ALLOW_CLOSE_AFTER_HOUR)));
    assert(!withinWindow(REC_CLOSE, localTimeAtHour(ALLOW_OPEN_AFTER_HOUR)));
}

static void testJsonParseInt(void) {
    int value = 0;

    assert(jsonParseInt("{\"inside\":72,\"oa\":65,\"fanspd\":0}", "inside", &value));
    assert(value == 72);

    assert(jsonParseInt("{ \"oa\" : -4 }", "oa", &value));
    assert(value == -4);

    assert(!jsonParseInt("{\"inside\":\"warm\"}", "inside", &value));
    assert(!jsonParseInt("{\"outside\": 65}", "inside", &value));
}

int main(void) {
    testRecommendations();
    testJsonParseInt();
    puts("core tests passed");
    return 0;
}
