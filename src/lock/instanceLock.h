#ifndef INSTANCE_LOCK_H
#define INSTANCE_LOCK_H

#ifdef _WIN32
#define INSTANCE_LOCK_INIT { 0, 0 }
typedef struct {
    void* handle;
    int locked;
} InstanceLock;
#else
#define INSTANCE_LOCK_INIT { -1, 0 }
typedef struct {
    int fd;
    int locked;
} InstanceLock;
#endif

int instanceLockAcquire(InstanceLock* lock, const char* path);
void instanceLockRelease(InstanceLock* lock);

#endif
