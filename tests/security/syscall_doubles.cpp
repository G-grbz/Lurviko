#include "syscall_doubles.h"
#include <sys/stat.h>
#include <unistd.h>
namespace SecurityTestDoubles {
QByteArray foreignOwnerPath;
QByteArray differentDevicePath;
}
extern "C" int __real_fstat(int, struct stat *);
extern "C" int __wrap_fstat(int fd, struct stat *st)
{
    const int result = __real_fstat(fd, st);
    if (result == 0) {
        char buffer[4096];
        const QByteArray link = "/proc/self/fd/" + QByteArray::number(fd);
        const ssize_t count = ::readlink(link.constData(), buffer, sizeof(buffer));
        const QByteArray path = count < 0 ? QByteArray{} : QByteArray(buffer, count);
        if (!SecurityTestDoubles::foreignOwnerPath.isEmpty() && path == SecurityTestDoubles::foreignOwnerPath)
            st->st_uid = ::getuid() + 1; // Simulate a foreign inode; no root/chown needed.
        if (!SecurityTestDoubles::differentDevicePath.isEmpty() && path == SecurityTestDoubles::differentDevicePath)
            ++st->st_dev; // Simulate a mount after open, exercising fd verification.
    }
    return result;
}
