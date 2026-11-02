#pragma once
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>

// Request only fields used by the caller, and verify mask coverage. No
// DONT_SYNC: disk-backed metadata is still allowed to require real I/O.
inline int directoryMetadataAt(int fd, const char* name, struct stat* out, bool minimal = false, bool useStatx = true)
{
#ifdef SYS_statx
    if (useStatx) {
        struct statx sx {};
        const unsigned mask = minimal ? STATX_TYPE | STATX_SIZE
            : STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_INO | STATX_MTIME | STATX_CTIME;
        if (::syscall(SYS_statx, fd, name, AT_SYMLINK_NOFOLLOW, mask, &sx) == 0 && (sx.stx_mask & mask) == mask) {
            *out = {};
            out->st_mode = sx.stx_mode; out->st_size = sx.stx_size; out->st_ino = sx.stx_ino;
            out->st_dev = makedev(sx.stx_dev_major, sx.stx_dev_minor);
            out->st_mtim = {sx.stx_mtime.tv_sec, sx.stx_mtime.tv_nsec};
            out->st_ctim = {sx.stx_ctime.tv_sec, sx.stx_ctime.tv_nsec};
            return 0;
        }
    }
#endif
    return ::fstatat(fd, name, out, AT_SYMLINK_NOFOLLOW);
}
