# Recursive administrator access: descriptor helper

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

## Implemented helper

Permission and ownership changes now use the separately installed
`lurviko-access-helper`, launched through `pkexec --disable-internal-agent`.
The desktop polkit agent authenticates the administrator; the application never
receives a password. Administrator locations and other KIO features are unchanged.

The helper accepts one bounded JSON request with absolute selected paths,
expected device/inode identities, numeric UID/GID, ordinary mode bits and a
recursion flag. It pins every path component with `O_PATH|O_NOFOLLOW`, rejects
changed selected roots, and validates every selected root before any mutation.
All chmod/chown operations address open descriptors using `AT_EMPTY_PATH`.
There is no privileged path-based mutation fallback. Directories are traversed
as root and changed after their children, so recursive repair of a `0644`
directory no longer requires the unprivileged application's search permission.
Symbolic links and special files are skipped; traversal stays on the selected
root's device. File contents are never opened for writing.

The parent keeps a pipe open for the helper's lifetime. Cancellation or parent
exit stops work at item boundaries. Progress is returned as bounded JSON lines.
No shell commands, arbitrary executable hooks, password inputs or content payloads
are accepted. Authentication cancellation and helper errors are shown in the
selected UI language. Installation includes the helper in libexec, and the
existing uninstall manifest tracks it.

## Limits and follow-up

Changes are not an atomic transaction: cancellation or an I/O failure can leave
a partly updated selection. Open inodes can be renamed while the operation runs;
a pin deliberately continues to refer to the original inode. Hard links share
permissions and ownership with all links to the same inode. Device checks do not
identify same-device bind mounts. The administrator is authorizing a root chmod/
chown operation, not a promise that every pathname remains stationary.

Descriptor chmod requires Linux 6.5+ and glibc 2.39+ (`fchmodat2`/`AT_EMPTY_PATH`).
Unsupported systems fail rather than falling back to pathname mutation. The
helper uses generic pkexec authorization; a dedicated packaged polkit action
with a more descriptive authentication dialog is a possible later improvement.

Tests cover replaced roots and regular files, external symbolic-link targets,
malformed requests, changed root identities, cancellation, mount boundaries,
ordinary-user UI/helper transport and unchanged SVG contents. The optional
`python3 tests/run-administrator-access-tests.py --authenticate` case uses the
real desktop polkit agent to repair an isolated temporary `0644` tree as root;
it never modifies personal files or runs unattended in CI.
