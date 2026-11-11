# Third-party components

## Bundled Python tools

Lurviko's built-in subtitle engine preserves its [GNU General Public License v3 text](tools/subtitle-ai/LICENSE).
Whisper transcription and local AI translation run directly through this engine.
Downloaded Python packages and AI models are not bundled in this repository; each has its own upstream license.

## System dependencies

Qt, KDE Frameworks, OpenSSL and optional helper programs are installed separately. Their licenses are provided by their respective upstream projects and distribution packages. The application source license does not replace those licenses.

## Visual assets

The monochrome interface glyphs in `assets/icons/symbolic` are [Microsoft Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons), distributed under the **MIT License**. Some were renamed and they are recolored at runtime. The copyright and license notice is in [assets/LICENSE-FluentUI.txt](assets/LICENSE-FluentUI.txt) and is installed with the application. This attribution does not imply affiliation with Microsoft.

Most of the coloured artwork (`assets/icons/color`, folder rasters and application branding) is Lurviko's own design. A few coloured files were adapted from KDE/freedesktop icon themes; their exact upstream theme and license have not been identified yet. Files whose embedded Inkscape document name is a theme or application icon name (for example `file.svg` from `x-office-document-template`, `trash.svg`/`trashfull.svg` from `user-trash`/`user-trash-full`, `music-note.svg` from `playmymusic` and `pdf.svg` from `masterpdfeditor`) are the likely candidates. Their embedded metadata is preserved.

The `assets/icons/` directory also contains cloud-provider marks. Provider trademarks remain the property of their owners. A root source-code license does not grant trademark rights or establish a new license for third-party artwork. Existing embedded metadata is preserved; upstream provenance for imported artwork has not been comprehensively documented. Contributors adding or replacing artwork should include the original source and license.
