#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "instance_lock.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "portable.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
static void print_windows_error(const char* action, const char* path, DWORD error_code) {
    if (error_code == ERROR_SHARING_VIOLATION || error_code == ERROR_LOCK_VIOLATION) {
        fprintf(stderr, "Another %s instance is already running\n", path);
    } else {
        fprintf(stderr, "%s %s failed with Windows error %lu\n",
                action, path, (unsigned long)error_code);
    }
}
#endif

int instance_lock_acquire(InstanceLock* lock, const char* path) {
    if (!lock || !path) {
        return 0;
    }

#ifdef _WIN32
    HANDLE handle = CreateFileA(path,
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                NULL,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        print_windows_error("Opening lock file", path, GetLastError());
        return 0;
    }

    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    if (!LockFileEx(handle,
                    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0,
                    MAXDWORD,
                    MAXDWORD,
                    &overlapped)) {
        print_windows_error("Locking", path, GetLastError());
        CloseHandle(handle);
        return 0;
    }

    SetFilePointer(handle, 0, NULL, FILE_BEGIN);
    SetEndOfFile(handle);

    char pid_text[64];
    int n = snprintf(pid_text, sizeof(pid_text), "%lld\n", portable_process_id());
    if (n > 0 && (size_t)n < sizeof(pid_text)) {
        DWORD written = 0;
        WriteFile(handle, pid_text, (DWORD)n, &written, NULL);
    }

    lock->handle = handle;
    lock->locked = 1;
    return 1;
#else
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to open lock file %s: %s\n", path, strerror(errno));
        return 0;
    }

    struct flock flock;
    memset(&flock, 0, sizeof(flock));
    flock.l_type = F_WRLCK;
    flock.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLK, &flock) != 0) {
        if (errno == EACCES || errno == EAGAIN) {
            fprintf(stderr, "Another %s instance is already running\n", path);
        } else {
            fprintf(stderr, "Failed to lock %s: %s\n", path, strerror(errno));
        }

        close(fd);
        return 0;
    }

    if (ftruncate(fd, 0) == 0) {
        char pid_text[64];
        int n = snprintf(pid_text, sizeof(pid_text), "%lld\n", portable_process_id());
        if (n > 0 && (size_t)n < sizeof(pid_text)) {
            ssize_t written = write(fd, pid_text, (size_t)n);
            (void)written;
        }
    }

    lock->fd = fd;
    lock->locked = 1;
    return 1;
#endif
}

void instance_lock_release(InstanceLock* lock) {
    if (!lock || !lock->locked) {
        return;
    }

#ifdef _WIN32
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    UnlockFileEx((HANDLE)lock->handle, 0, MAXDWORD, MAXDWORD, &overlapped);
    CloseHandle((HANDLE)lock->handle);
    lock->handle = NULL;
#else
    close(lock->fd);
    lock->fd = -1;
#endif

    lock->locked = 0;
}
