# Third-party components

## Bundled Python tools

Lurviko's built-in subtitle engine preserves its [GNU General Public License v3 text](tools/subtitle-ai/LICENSE).
Whisper transcription and local AI translation run directly through this engine.
Downloaded Python packages and AI models are not bundled in this repository; each has its own upstream license.

## System dependencies

Qt, KDE Frameworks, OpenSSL and optional helper programs are installed separately. Their licenses are provided by their respective upstream projects and distribution packages. The application source license does not replace those licenses.

## Visual assets

The former Font Awesome interface glyphs were replaced with Microsoft Fluent UI System Icons, which are distributed under the **MIT License**. The copyright and license notice is in [assets/LICENSE-FluentUI.txt](assets/LICENSE-FluentUI.txt) and is installed with the application. See the [upstream repository and license](https://github.com/microsoft/fluentui-system-icons/blob/main/LICENSE).

The 24 `assets/icons/symbolic/git.svg` and `menu-*.svg` action icons were
replaced with user-provided Papirus-derived SVG artwork for the menu refresh.
Papirus icon theme is distributed under the **GNU GPL v3**; see
[Papirus upstream](https://github.com/PapirusDevelopmentTeam/papirus-icon-theme)
and the GPL v3 license text in [LICENSE](LICENSE). Icons retain their source
metadata and are recolored at runtime using the existing alpha-mask renderer.
This attribution does not imply affiliation with the Papirus project.

The `assets/icons/` directory contains the project's current artwork, including modified SVGs, application branding and cloud-provider marks. Provider trademarks remain the property of their owners. A root source-code license does not grant trademark rights or establish a new license for third-party artwork. Existing embedded metadata is preserved; upstream provenance for imported artwork has not been comprehensively documented. Contributors adding or replacing artwork should include the original source and license.
