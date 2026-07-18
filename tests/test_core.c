#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "jsonUtils.h"
#include "notification_lock.h"
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
    time_t open_time = localTimeAtHour(ALLOW_OPEN_AFTER_HOUR);
    time_t close_time = localTimeAtHour(ALLOW_CLOSE_AFTER_HOUR);

    assert(getRecForTime(75, 74 - MARGIN, open_time) == REC_OPEN);
    assert(getRecForTime(75, 74 - MARGIN, close_time) == REC_NONE);
    assert(getRecForTime(70, 71 + MARGIN, close_time) == REC_CLOSE);
    assert(getRecForTime(70, 71 + MARGIN, open_time) == REC_NONE);
    
    // With MARGIN=0, equal temperatures are enough to trigger the active
    // notification window. Open is checked first during the open window, and
    // close is checked during the close-only window.
    assert(getRecForTime(70, 70, open_time) == REC_OPEN);
    assert(getRecForTime(70, 70, close_time) == REC_CLOSE);

    assert(strcmp(getRecName(REC_OPEN), "open") == 0);
    assert(strcmp(getRecName(REC_CLOSE), "close") == 0);
    assert(strcmp(getCurrentStatusForTime(75, 74 - MARGIN, open_time),
                  "Cooler out than in - Open windows") == 0);
    assert(strcmp(getCurrentStatusForTime(70, 71 + MARGIN, close_time),
                  "Hotter out than in - Close windows") == 0);
    assert(strcmp(getCurrentStatusForTime(70, 71 + MARGIN, open_time),
                  "Hotter out than in - waiting for close window") == 0);
    assert(strcmp(getCurrentStatusForTime(70, 70, open_time),
                  "Cooler out than in - Open windows") == 0);
    assert(strcmp(getRecName(REC_NONE), "none") == 0);

    assert(withinWindow(REC_OPEN, open_time));
    assert(!withinWindow(REC_OPEN, localTimeAtHour(ALLOW_OPEN_AFTER_HOUR - 1)));
    assert(withinWindow(REC_CLOSE, close_time));
    assert(!withinWindow(REC_CLOSE, open_time));
}

static void testNotificationLock(void) {
    const char* path = "test_notification_lock.tmp";
    time_t now = 1000;

    remove(path);
    remove("test_notification_lock.tmp.tmp");

    assert(notificationLockActiveUntil(path, REC_CLOSE, now) == 0);
    assert(notificationLockMarkSent(path, REC_CLOSE, now + 300));
    assert(notificationLockActiveUntil(path, REC_CLOSE, now) == now + 300);
    assert(notificationLockActiveUntil(path, REC_OPEN, now) == 0);
    assert(notificationLockActiveUntil(path, REC_CLOSE, now + 301) == 0);

    // Updating one recommendation should not erase the other saved slot.
    assert(notificationLockMarkSent(path, REC_OPEN, now + 600));
    assert(notificationLockActiveUntil(path, REC_CLOSE, now) == now + 300);
    assert(notificationLockActiveUntil(path, REC_OPEN, now) == now + 600);

    remove(path);
    remove("test_notification_lock.tmp.tmp");
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
    testNotificationLock();
    testJsonParseInt();
    puts("core tests passed");
    return 0;
}
