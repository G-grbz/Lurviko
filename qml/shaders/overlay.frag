#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 surfaceSize;
    float cornerRadius;
    vec4 nativeTint;
    vec4 nativeBase;
};
layout(binding = 1) uniform sampler2D blurredSource;
layout(binding = 2) uniform sampler2D originalSource;

void main()
{
    float radius = min(cornerRadius, min(surfaceSize.x, surfaceSize.y) * 0.5);
    vec2 offset = abs(qt_TexCoord0 * surfaceSize - surfaceSize * 0.5)
                - (surfaceSize * 0.5 - vec2(radius));
    float distance = length(max(offset, vec2(0.0)))
                   + min(max(offset.x, offset.y), 0.0) - radius;
    if (distance > 0.5)
        discard;
    float coverage = 1.0 - smoothstep(-0.5, 0.5, distance);
    // Qt color properties and sampled scene textures are premultiplied.
    // Replace the sharp scene in this rounded region instead of source-over
    // blending it a second time, which would expose the sharp original.
    vec4 backdrop = texture(blurredSource, qt_TexCoord0);
    // The overlay has a window base as well as the style's panel/menu tint.
    // Compose both sampled theme colors, including their alpha. Putting the
    // base below the captured view would leave opaque icons unaffected and
    // still too visible through menus. Never force a white or opaque base.
    vec4 tint = nativeTint + nativeBase * (1.0 - nativeTint.a);
    vec4 glass = tint + backdrop * (1.0 - tint.a);
    fragColor = mix(texture(originalSource, qt_TexCoord0), glass, coverage) * qt_Opacity;
}
