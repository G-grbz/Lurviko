#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <unistd.h>

static void delay_gui_probe(void)
{
    if (getenv("GFILE_TEST_SLOW_STORAGE") && syscall(SYS_gettid) == getpid())
        usleep(200000);
}

int statvfs(const char *path, struct statvfs *result)
{
    int (*real_statvfs)(const char *, struct statvfs *) = dlsym(RTLD_NEXT, "statvfs");
    delay_gui_probe();
    return real_statvfs(path, result);
}

int statvfs64(const char *path, struct statvfs64 *result)
{
    int (*real_statvfs64)(const char *, struct statvfs64 *) = dlsym(RTLD_NEXT, "statvfs64");
    delay_gui_probe();
    return real_statvfs64(path, result);
}

int statfs64(const char *path, struct statfs64 *result)
{
    int (*real_statfs64)(const char *, struct statfs64 *) = dlsym(RTLD_NEXT, "statfs64");
    delay_gui_probe();
    return real_statfs64(path, result);
}
