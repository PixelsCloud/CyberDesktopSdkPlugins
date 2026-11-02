// Adapted from rdev/liquid-glass-react ac48eab (Copyright 2025 MAX ROVENSKY).
// MIT license: resources/LiquidGlassReact-MIT.txt.
// SDFs below provide CSS shape coverage only; they never weight displacement.
cbuffer Settings : register(b0) {
    float4 Input;   // source dimensions, body origin in source pixels
    float4 Panel;   // untransformed body dimensions, radius, DPI
    float4 Optics;  // displacement in pixels, saturation factor, aberration, mode
    float4 Pose;    // CSS scale XY, fractional body origin XY
    float4 Pointer;// mouseOffset percentages XY, hover/press glow opacity
    float4 Glow;   // third radial opacity, overLight, unused
    float4 Frame;  // output dimensions, SVG blur padding XY
    float4 Mask;   // template body dimensions, padding, attached-top
    float4 Slice;  // nine-slice body caps XY, mask texture dimensions
    float4 Texel;  // allocated warp dimensions XY
};
Texture2D<float4> Backdrop : register(t0);
Texture2D<float2> Displacement : register(t1);
Texture2D<float4> FilteredWarp : register(t2);
Texture2D<float4> InsetMask : register(t3);
Texture2D<float4> EdgeMask : register(t4);
Texture2D<float4> RawBackdrop : register(t5);
Texture2D<float4> SourceImage : register(t6);
Texture2D<float4> OriginalBackdrop : register(t7);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> Result : register(u0);
float2 MaskUv(float2 localPx);

float3 Stored(float3 rgb) {
    // Separate CSS compositor layers store into an 8-bit sRGB surface between
    // draws. Preserve these boundaries instead of blending the entire stack
    // at floating-point precision and quantizing only its final result.
    return round(saturate(rgb) * 255) / 255;
}

float3 Rounded(float2 p, float2 halfSize, float radius) {
    radius = min(radius, min(halfSize.x, halfSize.y));
    float2 q = abs(p) - halfSize + radius;
    float2 outside = max(q, 0.0);
    float magnitude = length(outside);
    float2 normal = magnitude > 0.0001 ? sign(p) * outside / magnitude :
        q.x > q.y ? float2(sign(p.x), 0) : float2(0, sign(p.y));
    return float3(magnitude + min(max(q.x, q.y), 0.0) - radius, normal);
}

float3 Shape(float2 pixel, float2 size, float radius) {
    float2 halfSize = size * 0.5;
    if (Mask.w < 0.5) return Rounded(pixel - halfSize, halfSize, radius);
    float shoulder = min(12.0 * Panel.w, min(size.x * 0.25, halfSize.y));
    float bottomRadius = pixel.y > halfSize.y ? min(radius, min(halfSize.x - shoulder, size.y - shoulder)) : 0.0;
    float3 body = Rounded(pixel - halfSize, float2(halfSize.x - shoulder, halfSize.y), bottomRadius);
    float3 flare = Rounded(pixel - float2(halfSize.x, shoulder * 0.5), float2(halfSize.x, shoulder * 0.5), 0);
    float2 left = pixel - float2(0, shoulder), right = pixel - float2(size.x, shoulder);
    float leftLength = max(length(left), .0001), rightLength = max(length(right), .0001);
    float3 cutLeft = float3(shoulder - leftLength, -left / leftLength);
    float3 cutRight = float3(shoulder - rightLength, -right / rightLength);
    flare = cutLeft.x > flare.x ? cutLeft : flare;
    flare = cutRight.x > flare.x ? cutRight : flare;
    return body.x < flare.x ? body : flare;
}

float Coverage(float3 shape) {
    float gradient = max(length(shape.yz / Pose.xy), .0001);
    return saturate(.5 - shape.x / gradient);
}

float RectCoverage(float2 p) {
    return saturate(p.x + .5) * saturate(p.y + .5) *
        saturate(Panel.x + .5 - p.x) * saturate(Panel.y + .5 - p.y);
}

float3 Saturated(float3 rgb) {
    // CSS saturate(), in the filter's explicitly selected sRGB space.
    float luminance = dot(rgb, float3(.213, .715, .072));
    return saturate(lerp(luminance.xxx, rgb, Optics.y));
}

float4 RasterBounds() {
    float2 first = floor(Pose.zw);
    float2 last = ceil(Pose.zw + Panel.xy * Pose.xy);
    bool enlarged = max(Pose.x, Pose.y) > 1;
    if (Glow.y > .5 && enlarged) {
        first = float2(Pose.x < 1 ? ceil(Pose.z) : first.x, Pose.y < 1 ? ceil(Pose.w) : first.y);
        float2 end = Pose.zw + Panel.xy * Pose.xy;
        last = float2(Pose.x < 1 ? floor(end.x) : last.x, Pose.y < 1 ? floor(end.y) : last.y);
    }
    return float4(first, max(1, last - first));
}

float2 Mirrored(float2 localPx) {
    float4 bounds = RasterBounds();
    float2 pixel = localPx * Pose.xy + Pose.zw;
    float2 tiled = frac((pixel - bounds.xy) / (2 * bounds.zw)) * 2;
    return ((1 - abs(tiled - 1)) * bounds.zw + bounds.xy - Pose.zw) / Pose.xy;
}

float4 SourceGraphic(float2 localPx) {
    return SourceImage.SampleLevel(LinearClamp, Mirrored(localPx) / Texel.zw, 0);
}

[numthreads(16, 16, 1)]
void RasterSource(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(Texel.zw))) return;
    float2 screenPx = (float2(id.xy) + .5) * Pose.xy + Pose.zw + Input.zw;
    Result[id.xy] = float4(Saturated(Backdrop.SampleLevel(LinearClamp, screenPx / Input.xy, 0).rgb), 1);
}

float2 MapValue(float2 localPx) {
    if (any(localPx < 0) || any(localPx > Panel.xy)) return 0;
    // feImage preserveAspectRatio="xMidYMid slice". Static maps are square;
    // the generated shader map has the component's own logical dimensions.
    float2 uv = Optics.w < 2.5 ? (localPx - Panel.xy * .5) / max(Panel.x, Panel.y) + .5 : localPx / Panel.xy;
    return Displacement.SampleLevel(LinearClamp, uv, 0);
}

[numthreads(16, 16, 1)]
void Underlay(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(Input.xy))) return;
    // The compositor captures an enclosing physical-pixel rectangle before
    // applying the CSS transform and blur. Mirror at that raster boundary,
    // including the fractional margins around the transformed element.
    float4 bounds = RasterBounds();
    bool enlarged = max(Pose.x, Pose.y) > 1;
    float2 rasterOrigin = Input.zw + bounds.xy;
    float2 rasterSize = bounds.zw;
    float2 tile = frac((float2(id.xy) + .5 - rasterOrigin) / (2 * rasterSize)) * 2;
    float2 sourcePx = (1 - abs(tile - 1)) * rasterSize + rasterOrigin;
    float3 rgb = RawBackdrop.SampleLevel(LinearClamp, sourcePx / Input.xy, 0).rgb;
    float2 localPx = (sourcePx - Input.zw - Pose.zw) / Pose.xy;
    float shapeCoverage = Coverage(Shape(localPx, Panel.xy, Panel.z));
    if (Glow.y > .5 && !enlarged) {
        // The black CSS layers are rasterized in the element's local space
        // before their transform. Backdrop filtering sees that layer input,
        // including its clamped edge texels, before final screen coverage.
        shapeCoverage = InsetMask.SampleLevel(LinearClamp,
            MaskUv(clamp(localPx, .5, Panel.xy - .5)), 0).a;
    }
    float shadowAlpha = EdgeMask.SampleLevel(LinearClamp,
        MaskUv(localPx - float2(0, (Glow.y > .5 ? 16 : 12) * Panel.w)), 0).a;
    float coverage = shapeCoverage * Glow.y;
    // Literal bg-black opacity-20, then a black mix-blend-overlay layer.
    rgb = Stored(rgb * (1 - .2 * coverage));
    rgb = Stored(rgb - coverage * min(rgb, 1 - rgb));
    // GlassContainer follows both black layers in the original DOM; its
    // outer box shadow is painted over them before the backdrop is filtered.
    rgb = Stored(rgb * (1 - shadowAlpha * (Glow.y > .5 ? .75 : .25) * (1 - shapeCoverage)));
    Result[id.xy] = float4(saturate(rgb), 1);
}

[numthreads(16, 16, 1)]
void Warp(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(Texel.xy))) return;
    if (any(id.xy >= uint2(ceil(Panel.xy) + 2 * Frame.zw))) { Result[id.xy] = 0; return; }
    float2 localPx = float2(id.xy) + .5 - Frame.zw;
    float2 delta = MapValue(localPx) - .5;
    float direction = Optics.w > 2.5 ? 1 : -1;
    float4 red = SourceGraphic(localPx + delta * Optics.x * direction);
    float4 green = SourceGraphic(localPx + delta * Optics.x * (direction - Optics.z * .05));
    float4 blue = SourceGraphic(localPx + delta * Optics.x * (direction - Optics.z * .1));
    // feColorMatrix isolates channels. Screen blending their premultiplied
    // colors keeps RGB and takes the union of the three sampled alpha values.
    float alpha = 1 - (1 - red.a) * (1 - green.a) * (1 - blue.a);
    Result[id.xy] = float4(red.r, green.g, blue.b, alpha);
}

float Fold(float value, float fullSize, float templateSize, float cap) {
    if (value < cap) return value;
    if (value > fullSize - cap) return templateSize - (fullSize - value);
    return templateSize * .5;
}

float2 MaskUv(float2 localPx) {
    return (float2(Fold(localPx.x, Panel.x, Mask.x, Slice.x),
                   Fold(localPx.y, Panel.y, Mask.y, Slice.y)) + Mask.z) / Slice.zw;
}

float Gradient(float2 localPx, bool second) {
    float angle = (135 + Pointer.x * 1.2) * .0174532925199433;
    float2 direction = float2(sin(angle), -cos(angle));
    float extent = max(dot(abs(direction), Panel.xy), .0001);
    float t = dot(localPx - Panel.xy * .5, direction) / extent + .5;
    float first = max(.1, .33 + Pointer.y * .003);
    float next = max(first, min(.9, .66 + Pointer.y * .004));
    float last = max(1.0, next);
    float a = saturate((second ? .32 : .12) + abs(Pointer.x) * .008);
    float b = saturate((second ? .60 : .40) + abs(Pointer.x) * .012);
    if (t < first) return saturate(t / first) * a;
    if (t < next) return lerp(a, b, (t - first) / max(next - first, .0001));
    return b * saturate((last - t) / max(last - next, .0001));
}

float2 BorderPaint(float2 localPx, float outer, float inner, bool second) {
    float band = max(0.0, outer - inner);
    float alpha = Gradient(localPx, second) * outer;
    float premultiplied = alpha;
    float inset = .25 * (1 - InsetMask.SampleLevel(LinearClamp, MaskUv(localPx - float2(0, Panel.w)), 0).a) * outer;
    premultiplied = inset + premultiplied * (1 - inset);
    alpha = inset + alpha * (1 - inset);
    float hairInner = Coverage(Rounded(localPx - Panel.xy * .5, Panel.xy * .5 - .5 * Panel.w,
                                      max(0.0, Panel.z - .5 * Panel.w)));
    float hair = .5 * max(0.0, outer - hairInner);
    premultiplied = hair + premultiplied * (1 - hair);
    alpha = hair + alpha * (1 - hair);
    float black = .35 * EdgeMask.SampleLevel(LinearClamp, MaskUv(localPx - float2(0, Panel.w)), 0).a * (1 - outer);
    alpha += black * (1 - alpha);
    return float2(premultiplied, alpha) * band;
}

float3 Overlay(float3 destination, float premultiplied, float alpha) {
    return destination + (2 * premultiplied - alpha) * min(destination, 1 - destination);
}

[numthreads(16, 16, 1)]
void Composite(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(Frame.xy))) return;
    float2 screenPx = float2(id.xy) + .5;
    float2 localPx = (screenPx - Pose.zw) / Pose.xy;
    float outer = Coverage(Shape(localPx, Panel.xy, Panel.z));
    float inner = Coverage(Rounded(localPx - Panel.xy * .5, Panel.xy * .5 - 1.5 * Panel.w,
                                   max(0.0, Panel.z - 1.5 * Panel.w)));
    float mapAlpha = RectCoverage(localPx);
    float edge = mapAlpha >= 2.0 / 3.0 ? 1 : mapAlpha >= 1.0 / 3.0 ? saturate(Optics.z * .05) : 0;
    float4 warped = FilteredWarp.SampleLevel(LinearClamp, (localPx + Frame.zw) / Texel.xy, 0) * edge;
    float4 center = SourceGraphic(localPx) * (1 - edge);
    float4 glass = warped + center * (1 - warped.a);
    float3 raw = OriginalBackdrop.SampleLevel(LinearClamp, (screenPx + Input.zw) / Input.xy, 0).rgb;
    float coverage = outer * Glow.y;
    float3 base = Stored(raw * (1 - .2 * coverage));
    base = Stored(base - coverage * min(base, 1 - base));
    float3 rgb = Stored(glass.rgb * outer + base * (1 - glass.a * outer));
    float alpha = 1 - (1 - outer) * (1 - .2 * coverage) * (1 - coverage);
    float2 first = BorderPaint(localPx, outer, inner, false) * .2;
    rgb = Stored(rgb + (1 - rgb) * first.x);
    alpha = first.y + alpha * (1 - first.y);
    float2 second = BorderPaint(localPx, outer, inner, true);
    rgb = Stored(Overlay(rgb, second.x, second.y));
    alpha = second.y + alpha * (1 - second.y);
    // Three literal radial-gradient layers. Their opacity is animated by the host.
    float2 radialSize = Panel.xy + float2(Panel.w, 0);
    float2 radialPx = localPx + float2(.5 * Panel.w, 0);
    float radialCoverage = Coverage(Rounded(radialPx - radialSize * .5, radialSize * .5, Panel.z));
    float radius = length(float2(radialSize.x * .5, radialSize.y));
    float distance = length(radialPx - float2(radialSize.x * .5, 0)) / max(radius, .0001);
    float a = .5 * saturate(1 - distance / .5) * Pointer.z * radialCoverage;
    float b = saturate(1 - distance / .8) * Pointer.w * radialCoverage;
    float c = saturate(1 - distance) * Glow.x * radialCoverage;
    rgb = Stored(Overlay(rgb, a, a)); alpha = a + alpha * (1 - a);
    rgb = Stored(Overlay(rgb, b, b)); alpha = b + alpha * (1 - b);
    rgb = Stored(Overlay(rgb, c, c)); alpha = c + alpha * (1 - c);
    Result[id.xy] = float4(clamp(rgb - raw * (1 - alpha), 0, alpha), alpha);
}

[numthreads(16, 16, 1)]
void MaskImage(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(Slice.zw))) return;
    float2 localPx = float2(id.xy) + .5 - Mask.z;
    float3 shape = Shape(localPx, Mask.xy, Panel.z);
    Result[id.xy] = float4(0, 0, 0, saturate(.5 - shape.x));
}
