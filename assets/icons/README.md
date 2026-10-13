# Bundled artwork

| Folder | Contents |
| --- | --- |
| `png/` | PNG images, including the application logo and music cover fallback |
| `symbolic/` | Single-color SVG glyphs for actions, navigation and controls |
| `color/` | Colored SVG artwork, including folders, file types and cloud providers |

The eight color subfolders contain ready-made folder SVGs. Enable **Colored
folder icons** below **Use default icons** in View to use them. Turkish/English
folder names select picture, music, video, document or download artwork. Color
names such as `Mor` and `Purple` explicitly select that color; other folders get
a stable color from their name. Icons are selected without recoloring, directory
scans or background work; the name lookup cache is bounded to 512 entries.

Folder SVGs are also rendered into 64–512 pixel PNG sizes during the build.
The file list uses these original-color rasters immediately, so navigation and
scrolling do not queue SVG rendering behind thumbnails. Runtime raster and
scaled-image caches are bounded to 8 MiB and 16 MiB respectively. Editing an
SVG and rebuilding automatically regenerates its rasters; the public SVG
resource names and the original artwork remain available.

`color/last-location.svg` is the Last Location fallback when a selected external
icon cannot be loaded. Replace that file to customize the bundled fallback.

Keep filenames unique across these folders. CMake automatically discovers PNGs
in `png/` and SVGs in `symbolic/` and recursively in `color/`. The compiled resource aliases keep
the existing icon names usable in settings and in both QML and native rendering.
Moving a glyph between the SVG folders does not change its rendering behavior.
On startup, local icon settings that pointed at retired flat files in this
source tree are updated to their grouped paths. Unrelated custom files are kept.

After replacing artwork, run `./update-icons.sh` from the repository and restart
Lurviko. The script refreshes files in all three folders, rebuilds and installs.
Third-party attribution is in `assets/LICENSE-FontAwesome.txt` and `THIRD_PARTY.md`.
