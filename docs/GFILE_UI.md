# G-File UI Design System

G-File uses Qt Quick Controls for input, focus, keyboard and accessibility behavior, but owns the visual layer itself. `QQuickStyle::setStyle("Basic")` remains the stable cross-platform fallback; application-facing controls use G-File components instead of desktop-theme visuals.

## Design language

- Desktop-first, touch-friendly geometry: 40 px standard controls, compact 34 px controls.
- Rounded but not mobile-scaled: 8/12/18/24 px radius ladder.
- One violet accent family for focus, selection and primary actions.
- Light and dark palettes come from `AppTheme`; visual controls do not query KDE/Kvantum colors.
- Motion is short and functional: 90/140/220 ms timing tokens.
- Bundled UI icons stay independent of the optional system-icon preference.

## Core controls

Use these in pages/components instead of the corresponding raw Qt Quick Controls:

| G-File control | Qt behavior base |
| --- | --- |
| `GButton` | `Button` |
| `GToolButton` | `ToolButton` |
| `GTextField` | `TextField` |
| `GSlider` | `Slider` |
| `GSwitch` | `Switch` |
| `GCheckBox` | `CheckBox` |
| `GComboBox` | `ComboBox` |
| `GSpinBox` | `SpinBox` |
| `GProgressBar` | `ProgressBar` |
| `GBusyIndicator` | `BusyIndicator` |

One-off media overlays may override `background`, `contentItem`, `delegate` or `popup`; the G-File control remains the behavioral base.

## Tokens

Keep shared dimensions, colors and animation durations in `qml/AppTheme.qml`. Avoid introducing literal theme colors for normal application chrome when an `AppTheme` token exists. Media overlays are allowed to use purpose-specific translucent colors where they intentionally sit on top of video/artwork.

## Migration rule

New UI under `qml/pages`, `qml/components`, and `qml/Main.qml` should not instantiate the covered raw Qt controls directly. Run:

```bash
python3 tests/test-gfile-ui-controls.py
```

before committing UI work.


## G-File UI v2 — visual identity pass

The second pass intentionally changes the shell, not only individual controls:

- floating navigation panel with rounded selection pills and icon wells
- framed workspace canvas distinct from the desktop background
- unified command bar for browser tabs/actions
- 20 px family card surfaces for Quick Access, categories, cloud and disks
- provider/accent rails and icon wells instead of generic desktop cards
- 20 px context menus and 28 px modal surfaces
- capsule search and stronger settings/info hierarchy

These surfaces continue to use Qt Quick Controls for input/focus/accessibility, but visual state is owned by G-File tokens in `AppTheme.qml`.
