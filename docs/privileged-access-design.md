# Recursive administrator access: review and future helper

## Verified implementation (v1.1.1 preparation)

The installed packages are kio-admin 26.08.1 and KIO 6.30.0. The reviewed
kio-admin tag resolves to `e63a483a882ec501a82547e74ec693081abe989f`.
Its authorized helpers dispatch path-based KIO jobs:

- [chmodcommand.cpp, lines 16–27](https://github.com/KDE/kio-admin/blob/e63a483a882ec501a82547e74ec693081abe989f/src/helper/chmodcommand.cpp#L16)
- [chowncommand.cpp, lines 23–34](https://github.com/KDE/kio-admin/blob/e63a483a882ec501a82547e74ec693081abe989f/src/helper/chowncommand.cpp#L23)

The Unix file worker's final operations use ordinary path-based syscalls:

- [file.cpp, lines 168–181, chmod](https://github.com/KDE/kio/blob/v6.30.0/src/kioworkers/file/file.cpp#L168)
- [file_unix.cpp, lines 1251–1281, chown](https://github.com/KDE/kio/blob/v6.30.0/src/kioworkers/file/file_unix.cpp#L1251)

These calls do not pin the checked inode or request no-follow semantics. This
confirms the stat/action window in Lurviko's administrator path; it does not
establish the behavior of every distribution's patched KIO build.

## Current mitigation

Before any recursive administrator KIO job, Lurviko checks the local tree using
open directory descriptors, `openat(O_NOFOLLOW)` and `fstatat(AT_SYMLINK_NOFOLLOW)`.
Directories must be owned by root or the selected target owner. Group-write and
other-write directories are rejected, even with a sticky bit. Symlinks are not
traversed. Failed reads and cancellation fail closed. No privileged job is
started when the preflight fails. Non-recursive administrator behavior is
unchanged.

This is a conservative policy check, not an atomic transaction. A trusted
owner can change directories after checking; mutable ancestor paths, mount
changes, hard links and later permission/ownership changes still need care.
POSIX ACL write grants are bounded by the group mode mask, which this policy
rejects when writable. Non-POSIX ACLs or remote filesystem semantics are not
proved by checking ordinary Unix mode bits. Unreadable root-owned trees cannot
be recursively changed through this fallback.

## Future privileged helper (design only)

Use a small separately installed helper authorized by a dedicated polkit
permission. The unprivileged application sends the operation, numeric UID/GID,
ordinary mode bits, recursion policy and selected root identity. The helper
opens and validates the root itself; do not accept a caller's path check as
proof. Pin every parent and leaf with no-follow descriptors, compare device and
inode identities, and apply `fchmod`/`fchown` to those descriptors. Avoid opening
special devices/FIFOs and never truncate or write content. Traverse children
with `openat`, stay on the root device by default, and modify directories after
children. Define an explicit hard-link policy because an fd alone cannot
prevent changes through another link to the same inode.

Benefits: checks and mutations address the same inode, a smaller privileged
surface than running the UI as root, and consistent progress/cancellation.
Costs: a packaged privileged binary and polkit policy, extra deployment/review
work, file-descriptor limits, platform-specific syscall behavior and recovery
from partial changes. Define cancellation at item boundaries and report changed,
failed and skipped counts. Authorization must be tied to the D-Bus caller's
identity, paths/root identity, requested operation and lifetime; avoid shell
commands and arbitrary executable hooks. Keep denial as the default.
