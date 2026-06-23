#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "jsonUtils.h"
#include "rec.h"
#include "settings.h"

static void testRecommendations(void) {
    assert(getRec(75, 75 - MARGIN, 0) == REC_OPEN);
    assert(getRec(75, 75 - MARGIN + 1, 0) == REC_NONE);
    assert(getRec(70, 70 + MARGIN, 1) == REC_CLOSE);
    assert(getRec(70, 70 + MARGIN - 1, 1) == REC_NONE);

    assert(strcmp(getRecName(REC_OPEN), "open") == 0);
    assert(strcmp(getRecName(REC_CLOSE), "close") == 0);
    assert(strcmp(getRecName(REC_NONE), "none") == 0);

    time_t now = time(NULL);
    assert(withinWindow(REC_OPEN, now));
    assert(!withinWindow(REC_CLOSE, now));
}

static void testJsonParseInt(void) {
    int value = 0;

    assert(jsonParseInt("{\"inside\":72,\"oa\":65,\"power\":0}", "inside", &value));
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
