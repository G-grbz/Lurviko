#pragma once

class QQmlEngine;

// The image:// providers every Lurviko QML scene relies on (file previews,
// system and bundled icons, folder artwork). Shared by the main window and
// the file chooser portal so both render files identically.
void installImageProviders(QQmlEngine &engine);
