#include "notification_worker.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logger.h"
#include "notification_lock.h"
#include "portable.h"

typedef struct NotificationJob {
    SensorReading reading;
    time_t now;
    Rec rec;
    struct NotificationJob* next;
} NotificationJob;

static void* notificationThread(void* arg);
static NotificationJob* popNotificationJob(void);
static void sendNotification(const NotificationJob* job);
static int logNotificationEvent(const NotificationJob* job,
                                const char* event,
                                const char* detail);
static int logNotificationStatus(const char* event, const char* detail);
static int queueContainsRecLocked(Rec rec);
static void clearActiveNotification(Rec rec);

static const AppConfig* notification_config;
static NotificationJob* notification_head;
static NotificationJob* notification_tail;
static Rec active_notification_rec = REC_NONE;
static sem_t notification_sem;
static pthread_mutex_t notification_mutex = PTHREAD_MUTEX_INITIALIZER;

int notificationWorkerStart(const AppConfig* config)
{
    if (!config) {
        return 0;
    }

    notification_config = config;

    // The semaphore tracks queued jobs, while the mutex protects the linked FIFO.
    if (sem_init(&notification_sem, 0, 0) == -1)
    {
        perror("sem_init for notification worker");
        return 0;
    }

    pthread_t notification_thread;
    int res = pthread_create(&notification_thread, NULL, notificationThread, NULL);
    if (res != 0)
    {
        fprintf(stderr, "Notif Thread: %s\n", strerror(res));
        return 0;
    }
    pthread_detach(notification_thread);

    return 1;
}

int notificationQueueReading(const SensorReading* reading, time_t now, Rec rec)
{
    if (!reading) {
        return 0;
    }

    NotificationJob* job = malloc(sizeof(*job));
    if (!job) {
        return 0;
    }

    job->reading = *reading;
    job->now = now;
    job->rec = rec;
    job->next = NULL;

    pthread_mutex_lock(&notification_mutex);
    if (active_notification_rec == rec || queueContainsRecLocked(rec)) {
        pthread_mutex_unlock(&notification_mutex);
        free(job);
        return 0;
    }

    if (notification_tail) {
        notification_tail->next = job;
    } else {
        notification_head = job;
    }
    notification_tail = job;
    pthread_mutex_unlock(&notification_mutex);

    // Wake the worker after the job is visible in the queue.
    if (sem_post(&notification_sem) == -1)
    {
        perror("sem_post in poller thread");
        exit(EXIT_FAILURE);
    }

    return 1;
}

static void* notificationThread(void* arg)
{
    (void)arg;

    for (;;)
    {
        if (sem_wait(&notification_sem) == -1)
        {
            perror("sem_wait in notif thread");
            exit(EXIT_FAILURE);
        }

        NotificationJob* job = popNotificationJob();
        if (!job) {
            continue;
        }

        sendNotification(job);
        free(job);
    }

    return NULL;
}

static NotificationJob* popNotificationJob(void)
{
    pthread_mutex_lock(&notification_mutex);
    NotificationJob* job = notification_head;
    if (job) {
        notification_head = job->next;
        if (!notification_head) {
            notification_tail = NULL;
        }
        active_notification_rec = job->rec;
    }
    pthread_mutex_unlock(&notification_mutex);
    return job;
}

static void sendNotification(const NotificationJob* job)
{
    char msg[256];
    int fanOffOk = 1;
    const char* lock_path = notification_config ? notification_config->notification_lock_path : NULL;

    // Honor the persisted quiet-period lock before doing any side effects. This
    // prevents a service restart from resending the same notification window.
    time_t lock_check_now = time(NULL);
    time_t active_until = notificationLockActiveUntil(lock_path, job->rec, lock_check_now);
    if (active_until > lock_check_now)
    {
        long sleep_sec = (long)difftime(active_until, lock_check_now);
        char sleep_detail[64];
        snprintf(sleep_detail, sizeof(sleep_detail), "sleep(%u)", (unsigned int)sleep_sec);
        logNotificationStatus("Sleeping", sleep_detail);
        portableSleepSeconds((unsigned int)sleep_sec);
        clearActiveNotification(job->rec);
        return;
    }

    // Close recommendations are no longer based on fan speed, but the action
    // should only issue a fan-off command when the reading says fans are on.
    // Successful program shutoffs get their own graph marker event.
    if (job->rec == REC_CLOSE && job->reading.speed > 0)
    {
        fanOffOk = houseTurnOffFans(notification_config);
        if (fanOffOk) {
            logNotificationEvent(job, "fan auto off", "program turned fans off");
        }
    }

    if (job->rec == REC_OPEN)
    {
        snprintf(msg, sizeof(msg), "Open the windows (Out:%2d In:%2d)",
                 job->reading.outside_air, job->reading.house);
    }
    else if (fanOffOk)
    {
        snprintf(msg, sizeof(msg), "Close the windows (Out:%2d In:%2d)",
                 job->reading.outside_air, job->reading.house);
    }
    else
    {
        snprintf(msg, sizeof(msg), "Close the windows (fan failed)");
    }

    int msgResult = pushoverSendMessage(notification_config, msg);

    // Successful sends become graphable open/close events; failures stay explicit.
    if (msgResult)
    {
        const char* notifEvent = job->rec == REC_CLOSE ? "close notif" : "open notif";
        logNotificationEvent(job, notifEvent, msg);

        long sleep_sec = secUntilWindow(job->rec, job->now);
        time_t quiet_until = job->now + (sleep_sec > 0 ? sleep_sec : 0);
        if (!notificationLockMarkSent(lock_path, job->rec, quiet_until))
        {
            logNotificationStatus("notify lock failed", lock_path ? lock_path : "");
        }

        char sleep_detail[64];
        snprintf(sleep_detail, sizeof(sleep_detail), "sleep(%u)", (unsigned int)sleep_sec);
        logNotificationStatus("Sleeping", sleep_detail);
        if (sleep_sec > 0)
        {
            portableSleepSeconds((unsigned int)sleep_sec);
        }
    }
    else
    {
        logNotificationEvent(job, "notify failed", "");
    }

    clearActiveNotification(job->rec);
}

static int logNotificationEvent(const NotificationJob* job,
                                const char* event,
                                const char* detail)
{
    char house[16];
    char outside_air[16];
    char attic[16];
    char speed[16];

    if (!job || !notification_config) {
        return 0;
    }

    snprintf(house, sizeof(house), "%d", job->reading.house);
    snprintf(outside_air, sizeof(outside_air), "%d", job->reading.outside_air);
    snprintf(attic, sizeof(attic), "%d", job->reading.attic);
    snprintf(speed, sizeof(speed), "%d", job->reading.speed);

    const char* fields[] = {
        house,
        outside_air,
        attic,
        speed,
        getRecName(job->rec),
        event ? event : "",
        detail ? detail : ""
    };
    return logger_log_fields(notification_config->logger,
                             fields,
                             sizeof(fields) / sizeof(fields[0]));
}

static int logNotificationStatus(const char* event, const char* detail)
{
    if (!notification_config) {
        return 0;
    }

    const char* fields[] = {
        "-",
        "-",
        "-",
        "-",
        "-",
        event ? event : "",
        detail ? detail : ""
    };
    return logger_log_fields(notification_config->logger,
                             fields,
                             sizeof(fields) / sizeof(fields[0]));
}

static int queueContainsRecLocked(Rec rec)
{
    // Caller holds notification_mutex; this prevents duplicate work while retaining a real FIFO.
    for (NotificationJob* job = notification_head; job; job = job->next) {
        if (job->rec == rec) {
            return 1;
        }
    }

    return 0;
}

static void clearActiveNotification(Rec rec)
{
    pthread_mutex_lock(&notification_mutex);
    if (active_notification_rec == rec) {
        active_notification_rec = REC_NONE;
    }
    pthread_mutex_unlock(&notification_mutex);
}
