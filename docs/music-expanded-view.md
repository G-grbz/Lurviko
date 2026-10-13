# Lurviko Music: expanded and detached player

Both views use the *existing* `MusicPlayer` instance created once in `qml/Main.qml`. The expanded views are only UI controllers; they do not instantiate a second `MediaPlayer`, sound output, MPRIS endpoint, or queue.

## How to use

1. Play a track in the Music library. From the docked player, press **Expand** to open the new Now Playing workspace inside the Music page. You can also press Expand from the sidebar mini-player.
2. Press **Open in separate window** (the external-window icon) from the docked player or the expanded view to detach. Music keeps playing when the main window navigates elsewhere.
3. From the detached window press **Attach to Music page** to return to the expanded view in the main workspace. Closing either expanded presentation **does not stop the music**; only the original player close/stop control does.
4. The right panel switches between **Lyrics** and **Queue**. Timed `.lrc` or embedded synced lyrics follow the track automatically, and clicking a synced lyric seeks to its timestamp. Unsynced or missing lyrics are displayed without claiming synchronization.
5. Shuffle, repeat, previous/next, play/pause, queue selection, seeking, volume, and favorite state all share the global player.

## Implementation notes

- `qml/components/MusicExpandedView.qml`: presentational component; references `MusicPlayer` by property.
- `qml/Main.qml`: owns the expanded/detached presentation state, top-level `Window`, and routing. Detaching or attaching does not reload the current media.
- `qml/components/MusicPlayer.qml`: provides UI signals for expansion/detachment, hides its dock while the in-page workspace is active, and continues playback underneath.
- `qml/components/AppSidebar.qml`: the existing mini-player's expand action now opens the Music workspace.
- `.lrc` timing and lyrics loading remain in the existing `MusicMetadataManager` / `MusicPlayer`; no network lyric service is introduced.

The blurry album-color wash uses a small source image to limit rendering cost. Layout and language use the existing AppTheme and Turkish/English language setting.

## Build

Qt 6.9+, the KF6 modules and the other dependencies listed in `CMakeLists.txt` are required:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The visual implementation needs to be exercised on KDE Plasma/Wayland with a real music collection to confirm geometry, performance, window focus, and `lrc` behavior. This repository change does not include an application binary.

## Mouse input and light-theme tabs (2026-10-12 fix)

- The expanded canvas now has an input shield **behind** its controls. It catches
  mouse clicks, hover and wheel interactions in unused whitespace, so events do
  not fall through the in-page overlay into the Music library underneath.
  In particular, pressing empty space around the footer's transport controls
  must not select a different background track or restart playback.
- Lyrics/Queue tab buttons use explicit foreground, border and background
  theme tokens. Selected tab text is `AppTheme.accent` on `AppTheme.accentSoft`
  instead of Qt Controls' platform-dependent checked/primary palette.
- Check both light and dark themes in embedded and detached mode. Run a track,
  click both footer whitespace gaps, hover them for unexpected background
  tooltips, wheel on whitespace, switch Lyrics/Queue and confirm only actual
  queue and transport buttons modify playback.

## Playlists and compact playback bar (2026-10-12 update)

The right-hand panel now offers **Şarkı Sözleri / Kuyruk / Çalma Listeleri**
(Lyrics / Queue / Playlists). The saved lists are the very same lists used by the
original docked player. The playlist panel displays names and track counts, and
allows playing, creating a list from the current queue, and deleting with
confirmation. Saving with a previously used name has the same replacement
semantics as the existing player; music files are never deleted.

The footer is now 94 px high (previously 112 px). Track art/name remain on the
left, transport and scrubber stay grouped in the middle, and a volume control
is capped at 146 px overall with a 104 px maximum slider. Controls retain
realtime bindings to the global player; no new audio engine or events were
introduced. The original background-hit-test shield is preserved.

Manual checks on Qt/KDE recommended:

- In light and dark themes, open all three tabs in embedded and detached modes.
- Save the queue as a new playlist, start a saved playlist, confirm or cancel
  deletion, and verify that list changes appear in the regular music player.
- Click unused footer whitespace (including around volume) and ensure tracks
  don't change unexpectedly. Drag both volume and seek without event bleed.
- Resize both windows and confirm all three tabs and the shortened footer fit.
