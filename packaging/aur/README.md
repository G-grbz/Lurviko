# AUR packaging

`lurviko/` contains the stable source package recipe for the current Lurviko
release. The packaging files are licensed under **0BSD**; this does not change
the application's GPL license or the bundled artwork licenses.

## Build and verify

Keep the Arch installation fully updated and install `base-devel`, `git`,
`namcap` and the dependencies listed in `PKGBUILD`. Then run:

```bash
cd packaging/aur/lurviko
makepkg --syncdeps --cleanbuild
namcap PKGBUILD
namcap lurviko-*.pkg.tar.zst
```

The source is downloaded from the matching GitHub release and verified using
its SHA-256 checksum. `check()` runs the upstream headless tests and validates
the desktop entry. `CMAKE_BUILD_PARALLEL_LEVEL` can limit parallel build jobs.
Only x86_64 is declared because it is the tested architecture.

`namcap` can report optional Python imports and bundled worker modules because
AI dependencies are loaded from a user-managed runtime. The recipe declares
available Arch helpers as optional dependencies; downloaded models and Python
environments are not included in the package. Qt SVG plugins and `ffmpeg`
helpers are runtime dependencies even though they are not directly linked.

## Desktop integration

The package installs `/usr/bin/lurviko`, the `g-file` compatibility symlink,
desktop entries, icons and subtitle workers. Pacman's existing desktop and
icon cache hooks handle updates. Installing the package does not change user
MIME defaults.

Select Lurviko in Plasma's default applications settings, or run:

```bash
xdg-mime default lurviko.desktop inode/directory
xdg-mime default lurviko.desktop x-scheme-handler/trash
```

Other file managers can own the generic system FileManager1 service filename.
The AUR package therefore ships an opt-in service template without replacing
their files. To enable Lurviko for D-Bus **Show in folder**, run:

```bash
install -Dm644 /usr/share/Lurviko/dbus-1/services/org.freedesktop.FileManager1.service \
  "${XDG_DATA_HOME:-$HOME/.local/share}/dbus-1/services/org.freedesktop.FileManager1.service"
```

Log out and back in after changing activation providers. If you previously
installed Lurviko under `~/.local`, remove that old installation's binaries
and desktop integration files when switching to the system package so they
do not shadow it. Keep your user configuration, library and cache directories.

## Publish and update

The AUR Git repository contains only `PKGBUILD`, `.SRCINFO`, `lurviko.install`
and `LICENSE`; application sources and built packages remain upstream.
Push submissions to the AUR repository's `master` branch.

For a new upstream release, update `pkgver`, reset `pkgrel=1`, and update the
source checksum from that release's `SHA256SUMS`. For packaging-only changes,
increment `pkgrel`. Rebuild, rerun `namcap`, regenerate `.SRCINFO` with
`makepkg --printsrcinfo > .SRCINFO`, then commit and push the AUR files.

Official references: [PKGBUILD manual](https://man.archlinux.org/man/PKGBUILD.5.en),
[AUR submission guidelines](https://wiki.archlinux.org/title/AUR_submission_guidelines).
