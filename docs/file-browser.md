# File browser views

Use the upper-right **View** menu to choose **Grid**, **List** or **Columns**.
The preference is saved. The default shortcuts are Ctrl+1, Ctrl+2 and Ctrl+3;
they can be changed in the keyboard shortcuts dialog.

## Directory size

Local directory footers show the recursive logical file size beside the item
count, in IEC units (KiB, MiB, GiB). The calculation includes hidden descendants
and skips symbolic links. It runs in the background and is cancelled when the
pane leaves that directory. A short delay avoids scanning locations passed
through during navigation; transfers take priority over automatic recalculation.

Properties shares this calculation and its immediate-child subtotals. Selecting
all visible items therefore reuses the ready result, or waits for the same
in-flight scan, rather than starting another recursive walk. Hidden items that
are not selected are excluded from selection totals. Results are snapshots;
refreshing the directory recalculates its size.

## Video titles and episodes

The video's lower strip uses Lurviko tooltips. Available TMDB titles replace
release filenames there; otherwise the original filename is shown. Recognized
`S01E03`, `1x03` and explicit season/episode names add the episode identity.

For a matched TV series, the playing file can resolve its episode through
[TMDB episode details](https://developer.themoviedb.org/reference/tv-episode-details).
The pause screen and local video hover cards keep the series title/logo and
add the episode name and number, preferring the episode overview and runtime
when available. Missing
localized plots can fall back to English; missing episodes retain the existing
series information. Episode results are cached, and hovering the strip does
not start episode requests.

## Category selections

Explore categories provide **Select all / Deselect all** and **Actions** controls.
The same toggle on the right of a date header affects only that day's items;
other selected dates remain selected. Card checkboxes select individual files
without opening them. In Music, album, artist and playlist checkboxes select
those collections' underlying tracks; repeated tracks are counted once.

Right-clicking a selected item preserves the whole selection. **Actions** opens
the same context menu for cut, copy, copy actions, batch rename, sharing,
compression, copy/move to a destination, Trash and properties. Hold Shift to
switch Trash to permanent deletion, with the existing confirmation preference.
Available actions depend on the selection. Selections store original file URLs,
even when the files belong to different directories. Duplicates are created
beside their respective originals; mixed-directory archives retain relative
parent paths so files with equal names remain distinct.

## Qt desktop appearance

Enable **View → Use Qt theme** to use the Qt application's desktop palette
instead of Lurviko's light/dark colors. The setting is saved; disabling it
restores the previous light/dark choice. The sidebar's light/dark switch is
disabled while Qt mode is active. Icon-theme selection remains independent.

Window backgrounds also sample the selected QWidget style's native painting:
this includes Kvantum background opacity when its theme enables translucency.
Large background panels share one backdrop rather than stacking opaque fills.
The backdrop is painted by the scene graph over a transparent window clear;
this keeps the color premultiplied correctly on GPU backends. Explore cards
use light translucent overlays and outlines rather than opaque palette bevels.
KWin blur is requested for translucent main and detached music windows using
KWindowEffects. KWin must provide and enable the blur effect; compositor opacity
rules remain controlled by KWin. Fully opaque styles remain opaque. Lurviko
keeps its own control layout and does not reproduce a style's SVG textures or
widget shapes. Qt palette/style changes delivered to the application are
applied live; styles that only load settings at startup require a restart.
Qt mode also applies to photo/video controls and photo hover cards. Music and
video hover previews retain their artwork, dark backgrounds and light text
independently of the Qt theme. In custom
light/dark mode the video player keeps its fixed dark chrome; the video canvas
remains black in either mode.

Menus and combo popups use the selected Qt style's menu background, separately
from toolbar backgrounds. Embedded overlays compose a native window base and
the panel/menu tint above their blurred content, preserving both sampled alpha
values. This reduces show-through without a fixed white or opaque fill.
Menus stay inside the application
window to retain their click positions on Wayland and scaled displays. Modal
settings, keyboard shortcuts, notifications and photo hover surfaces share the
native window background. Qt Quick blurs the application's content beneath
these overlays; KWin handles the desktop behind the main window. Captures are
limited to the visible overlay and released on close. Software rendering or an
unsafe capture source uses an opaque native color for readability. Video
controls also use solid native colors because capturing VideoOutput textures
is unreliable on some drivers. Tooltip colors follow the Qt palette.

Native API references: [Qt palette](https://doc.qt.io/qt-6/qpalette.html),
[KDE window effects](https://api.kde.org/kwindoweffects.html), and
[Kvantum native window painting](https://github.com/tsujan/Kvantum/blob/master/Kvantum/style/Kvantum.cpp).

## Miller Columns

Selecting a folder shows its contents in a column to the right. Selecting a
different folder replaces that branch. A single selection stays in its current
column, so Up/Down can browse siblings without repeatedly opening and closing
folders. Right enters the next column; Left returns to the previous column.
Double-clicking a folder also enters its column. Enter and the usual file
shortcuts use the existing browser actions.

Drag a column's right separator to resize it. The last saved width is used for
new columns. Use the bottom scrollbar to reach columns outside the viewport.
Selecting a file shows a bounded thumbnail or an icon with its name, type and
size. **Open** uses the normal file-opening action; selecting an executable
does not run it automatically.

Columns share the browser's selection, hidden-file and sorting controls,
context menus, inline rename and KIO copy/move/drop actions. Tabs and split panes
use the same navigation infrastructure. Each column lists asynchronously and
creates only visible rows, rather than creating a card for every file.

Explore categories keep their specialized layouts. Search results use the
existing List/Grid interface and its persistent tab search sessions; closing
search returns to directory browsing.

## Git status badges

When Git is installed, local repositories show small badges in Grid, List and
Columns: **M** modified, **+** added, **?** untracked, **−** deleted, **R** renamed,
**I** ignored and **!** conflicted. Folder badges summarize changed descendants.
A deleted file has no physical row; its containing folder can carry the deleted
badge. Ignored/untracked directory badges also apply to their children. The
existing file icons, custom folder icons and icon-theme choice are preserved.

Repository folders also show badges when viewed from a non-repository parent
(for example a directory containing several projects). Visible folder entries
are inspected in small background batches. **G** means a repository has been
detected and its status is not available yet; **✓** means a successful scan
found no changes. Modified repositories show their aggregate change badge.

Discovery and parsing run in background workers, and `git status` runs in a
separate asynchronous process. Columns in the same repository share a snapshot.
Visible-directory changes, index/HEAD changes, window activation and Refresh
request updates; there is no periodic Git scan while idle. Linked worktrees are
supported. Edits in descendants without an active directory watcher are picked
up on window activation or Refresh; the entire worktree is not watched recursively.
This integration only reads status and disables executable fsmonitor
hooks from repository configuration.

To bound resource use, at most eight full repository snapshots, 512 small root
summaries and two simultaneous
Git processes are kept. Each scan has a 15-second deadline, an 8 MiB output cap
and a 100,000-entry parsed-status cap. If Git fails or a limit is reached, the
previous snapshot remains; badges are best-effort and can be stale until a later
successful refresh. Git is not started for remote KIO URLs or non-repositories.

## File operation feedback

Copy, move, duplicate and trash keep their existing asynchronous KIO jobs and
KDE conflict/authorization handling. Permanent deletion, emptying Trash,
restoring from Trash and batch rename also use the shared operation tracker.
Choose Plasma/KIO notifications or integrated cards in **View**. Trash continues
to show preparation feedback in-app in either mode.

Cards use worker-reported item/byte counters and speed. An ETA is shown when
remaining bytes and speed are available; otherwise it remains unknown. Skipped
files do not get guessed result URLs, and successful completion no longer
forces measured counters to the requested totals. A job reporting 100% is held
at 99% in the card until its result confirms success. Cancellation and failure
are distinct; pause/cancel buttons follow the job's supported capabilities.
Failed or cancelled KIO jobs refresh the view too, since a batch may already
have changed some entries.

Archive inspection and overwrite conflict checks stream in a background worker
and can be cancelled before extraction starts. Compression writes to an
exclusively created hidden staging file, then publishes a completed archive
without replacing an existing name. A racing name collision gets a numbered
name. Cancellation/failure cleans up the staging file, preserving existing
archives and source files. On filesystems that support neither no-replace rename
nor hard links, publication fails safely instead of overwriting a file.

Extraction retains libarchive's safe-write and path guards. Cancellation or
failure can leave entries already extracted; these are retained and the view
refreshes. There is no whole-directory rollback. Archive tools remain local;
remote copy/move/delete/restore operations continue through KIO workers.

For drag extraction from Ark, Lurviko briefly watches the destination and its
immediate children. Actual changes refresh merged-folder counts and select and
reveal the first observed results, with a short notification even when system
progress is enabled. Later updates refresh counts without stealing a new user
selection. This indicates that contents became available; Ark's own job still
reports extraction progress and errors. Its D-Bus acknowledgement does not
confirm completion. Monitoring is shallow and temporary (up to 4,096 child
watches, 30 seconds idle / three minutes total), with no recursive tree scan.

### Verification limits

Native integration tests exercise local KIO copy/move/duplicate/trash/restore/
delete, conflict skip/overwrite, missing sources, permission errors, cancellation,
archive creation/extraction and staging-name collisions. Controlled KJob doubles
cover disk-full, connection-broken and premature-destruction result paths.
The tests also verify counters, speed/ETA formatting and GUI responsiveness
during slow archive inspection. Actual SMB/SFTP outages and physical disk-full
conditions have not been exercised by these tests; remote worker behavior and
available progress metrics still depend on the installed KIO protocols.
