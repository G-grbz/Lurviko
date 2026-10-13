#include "syscall_doubles.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
namespace SecurityTestDoubles {
QByteArray foreignOwnerPath;
QByteArray differentDevicePath;
QByteArray replacePath;
QByteArray replacementTarget;
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
        if (!SecurityTestDoubles::replacePath.isEmpty() && path == SecurityTestDoubles::replacePath) {
            const auto original = SecurityTestDoubles::replacePath;
            SecurityTestDoubles::replacePath.clear();
            // Deterministically replace the checked pathname before chmod/chown.
            if (::rename(original.constData(), (original + ".original").constData()) == 0)
                ::symlink(SecurityTestDoubles::replacementTarget.constData(), original.constData());
        }
    }
    return result;
}
