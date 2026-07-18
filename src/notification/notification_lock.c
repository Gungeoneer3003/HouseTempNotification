#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "notification_lock.h"

#include <stdio.h>
#include <string.h>

#define NOTIFICATION_LOCK_TMP_SUFFIX ".tmp"

typedef struct NotificationLockState {
    time_t open_until;
    time_t close_until;
} NotificationLockState;

static NotificationLockState readState(const char* path);
static int writeState(const char* path, const NotificationLockState* state);
static time_t* stateSlot(NotificationLockState* state, Rec rec);
static const time_t* constStateSlot(const NotificationLockState* state, Rec rec);

// Check the persisted quiet-period state so a process restart cannot resend the
// same open/close recommendation before the next recommendation window begins.
time_t notificationLockActiveUntil(const char* path, Rec rec, time_t now)
{
    if (!path || !*path) {
        return 0;
    }

    NotificationLockState state = readState(path);
    const time_t* until = constStateSlot(&state, rec);
    if (!until || *until <= now) {
        return 0;
    }

    return *until;
}

int notificationLockMarkSent(const char* path, Rec rec, time_t until_time)
{
    if (!path || !*path) {
        return 0;
    }

    NotificationLockState state = readState(path);
    time_t* slot = stateSlot(&state, rec);
    if (!slot) {
        return 0;
    }

    *slot = until_time;
    return writeState(path, &state);
}

static NotificationLockState readState(const char* path)
{
    NotificationLockState state = {0, 0};
    FILE* file = fopen(path, "r");
    if (!file) {
        return state;
    }

    char rec_name[32];
    long long until_value;
    while (fscanf(file, "%31s %lld", rec_name, &until_value) == 2) {
        if (strcmp(rec_name, "open") == 0) {
            state.open_until = (time_t)until_value;
        } else if (strcmp(rec_name, "close") == 0) {
            state.close_until = (time_t)until_value;
        }
    }

    fclose(file);
    return state;
}

static int writeState(const char* path, const NotificationLockState* state)
{
    char temp_path[512];
    int n = snprintf(temp_path,
                     sizeof(temp_path),
                     "%s%s",
                     path,
                     NOTIFICATION_LOCK_TMP_SUFFIX);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) {
        return 0;
    }

    FILE* file = fopen(temp_path, "w");
    if (!file) {
        return 0;
    }

    // Keep both recommendation slots in the file so future reads are simple and
    // older unexpired locks are not lost when the other recommendation is sent.
    fprintf(file, "open %lld\n", (long long)state->open_until);
    fprintf(file, "close %lld\n", (long long)state->close_until);

    if (fclose(file) != 0) {
        remove(temp_path);
        return 0;
    }

    // Windows rename will not replace an existing file, so remove the old state
    // first. The temporary file still prevents partial writes from becoming the
    // active lock file.
    remove(path);
    if (rename(temp_path, path) != 0) {
        remove(temp_path);
        return 0;
    }

    return 1;
}

static time_t* stateSlot(NotificationLockState* state, Rec rec)
{
    if (!state) {
        return NULL;
    }

    switch (rec) {
        case REC_OPEN:
            return &state->open_until;
        case REC_CLOSE:
            return &state->close_until;
        default:
            return NULL;
    }
}

static const time_t* constStateSlot(const NotificationLockState* state, Rec rec)
{
    return stateSlot((NotificationLockState*)state, rec);
}
