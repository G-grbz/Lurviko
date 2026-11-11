#version 440

// Folder-preview card: a rounded, aspect-cropped thumbnail with a soft drop
// shadow, drawn in one pass. It replaces two offscreen layers per card (a
// blurred shadow and a mask), which made grid scrolling CPU-bound.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;      // full effect item, card plus shadow padding
    vec2 cardSize;      // visible thumbnail card
    vec2 sourceImageSize;     // decoded image size, for PreserveAspectCrop
    float padding;      // card inset on every side of the effect item
    float cornerRadius;
    float shadowOffset; // shadow offset along +y
    float shadowSoftness;
    float shadowAlpha;
    float hasImage;
};
layout(binding = 1) uniform sampler2D source;

float roundedBoxDistance(vec2 p, vec2 size, float radius)
{
    vec2 half_ = size * 0.5;
    vec2 q = abs(p - half_) - (half_ - vec2(radius));
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

void main()
{
    float radius = min(cornerRadius, min(cardSize.x, cardSize.y) * 0.5);
    vec2 p = qt_TexCoord0 * itemSize - vec2(padding);

    float shadowDistance = roundedBoxDistance(p - vec2(0.0, shadowOffset), cardSize, radius);
    float shadow = shadowAlpha * (1.0 - smoothstep(-shadowSoftness, shadowSoftness, shadowDistance));

    float cardDistance = roundedBoxDistance(p, cardSize, radius);
    float coverage = clamp(0.5 - cardDistance, 0.0, 1.0);

    vec4 image = vec4(0.0);
    if (hasImage > 0.5 && sourceImageSize.x > 0.0 && sourceImageSize.y > 0.0) {
        float scale = max(cardSize.x / sourceImageSize.x, cardSize.y / sourceImageSize.y);
        vec2 visible = cardSize / (sourceImageSize * scale);
        vec2 uv = vec2(0.5) + (p / cardSize - vec2(0.5)) * visible;
        image = texture(source, uv) * coverage;
    }

    // Premultiplied source-over: thumbnail above its own shadow.
    vec4 shadowColor = vec4(0.0, 0.0, 0.0, shadow);
    fragColor = (image + shadowColor * (1.0 - image.a)) * qt_Opacity;
}
