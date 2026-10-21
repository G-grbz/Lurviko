# Overlay composition

`overlay.frag.qsb` is checked in so building Lurviko requires no additional
shader tools. Its Qt 6.4 serialization format is compatible with the project's
Qt 6.9 minimum. Regenerate it after changing `overlay.frag`:

```sh
qsb --qt6 --qsbversion 64 -o qml/shaders/overlay.frag.qsb qml/shaders/overlay.frag
```

The final pass replaces the sharp scene inside the rounded surface. It applies
the native window base followed by the native panel/menu tint, using Qt's
premultiplied source-over rule. Both colors retain their sampled theme alpha:
the resulting opacity is `1 - (1 - baseAlpha) * (1 - panelAlpha)`. The base
is applied above the blurred view so opaque icons are subdued too. No fixed
white color or opaque fill is added. Outside the rounded shape, fragments are discarded. During
opacity transitions it uses ordinary blending so a fading popup cannot erase
the scene beneath it. Software rendering uses the existing solid fallback.

The source scene excludes the popup overlay to avoid feedback. Its crop follows
both item hierarchies, including reparenting, translation and scale. The
appearance integration tests check moved colored targets, transparent source
content, and framebuffer alpha under OpenGL.
