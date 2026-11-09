# Recent Files / Son Dosyalar

`category:/recent` is a Discover category with the same click behavior,
appearance and drag-to-reorder controls as the other categories. Unlike them,
it does **not** scan folders.

## Source: the desktop's shared recent list

Linux desktop applications record the files they open and save in
`~/.local/share/recently-used.xbel` (the freedesktop.org recent files list).
GTK applications, Chrome/Chromium, Firefox and KDE applications (through
`KRecentDocument`) all write to it; Dolphin's and GNOME Files' "Recent" views
read the same file.

Lurviko reads this one small XML file on startup and again whenever an
application updates it. There is no directory walk, inotify tree or file
attribute probing, so the category costs practically no CPU and also shows
files saved while Lurviko was closed.

**KDE Connect** does not add received files to that list itself. Lurviko
listens for KDE Connect's `shareReceived` D-Bus announcement and records each
received file there (via `KRecentDocument`), so it appears in Recent Files —
and in other applications' recent lists — like a browser download.

## What is shown

An entry is shown when all of the following are true:

1. It is a local file that still exists (deleted or moved files drop out).
2. Its extension is an archive, document, image, audio or video type.
   AppImages, code and other file types are excluded.
3. It is inside one of Recent's configured roots (default: home directory),
   is not matched by Recent's ignore rules and follows the hidden-file setting.
4. The newest of its *added*, *modified* and *visited* times in the list falls
   within the selected window (7, 30 or 90 days, or all recorded time).

Entries are ordered by that time, newest first.

## Limitations

- The category lists files that were *used* recently (downloaded, received,
  opened or saved), not only downloads. A video you just played appears too;
  narrow the roots or add ignore rules if that is not wanted.
- Applications that do not record recent files (some command-line tools,
  `cp`/`mv`) do not add entries.
- KDE applications deliberately do not record files inside the temporary
  directory.
- Desktops can limit the list's length or disable it ("Remember opened
  documents" in Plasma's privacy settings); then fewer or no files appear.
