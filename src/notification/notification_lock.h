#ifndef NOTIFICATION_LOCK_H
#define NOTIFICATION_LOCK_H

#include <time.h>
#include "rec.h"

// Returns the saved wake time for rec when a previous successful notification
// is still inside its quiet period. A return value of 0 means no active lock.
time_t notificationLockActiveUntil(const char* path, Rec rec, time_t now);

// Records that rec was successfully sent and should stay quiet until until_time.
int notificationLockMarkSent(const char* path, Rec rec, time_t until_time);

#endif
