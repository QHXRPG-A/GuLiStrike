// One Slate quad for the whole marker. UV is relative to its original outer bounds.
// The extra half pixel of geometry lets the outline filter cross those bounds.
struct FGridInk
{
    float3 LinearRGB(float3 bytes)
    {
        float3 c = bytes / 255.;
        return lerp(c / 12.92, pow((c + .055) / 1.055, 2.4), step(.04045, c));
    }

    float StripeArea(float x, float pitch, float stroke, float count)
    {
        float end = x + .5 * stroke;
        float index = clamp(floor(end / pitch), 0., count);
        if (index < 1.) return 0.;
        return (index - 1.) * stroke + clamp(end - index * pitch, 0., stroke);
    }

    float3 Row(float x, float width, float pixel, float fraction, float cells, float3 fill, float3 empty, float3 ink)
    {
        float filled = fraction >= 1. ? 1. : (fraction <= 0. ? 0. : saturate(.5 + (fraction * width - x) / pixel));
        float3 color = lerp(empty, fill, filled);
        if (cells <= 1.) return color;
        float pitch = width / cells;
        float stroke = min(.85, .22 * pitch);
        float coverage = stroke / pitch;
        if (pitch > pixel)
        {
            float count = ceil(cells) - 1.;
            float area = StripeArea(x + .5 * pixel, pitch, stroke, count) - StripeArea(x - .5 * pixel, pitch, stroke, count);
            coverage = lerp(coverage, saturate(area / pixel), smoothstep(pixel, 2. * pixel, pitch));
        }
        return lerp(color, ink, .68 * coverage);
    }
};

FGridInk grid;
float2 size = max(PackedSize / 16., .001);
float2 p = UV * size;
float2 halfSize = .5 * size;
float radius = min(3., .72 * min(halfSize.x, halfSize.y));
float2 delta = abs(p - halfSize) - (halfSize - radius);
float sd = length(max(delta, 0.)) + min(max(delta.x, delta.y), 0.) - radius;
float aa = max(fwidth(sd), .001);
float silhouette = saturate(.5 - sd / aa);
float border = min(1., .15 * min(size.x, size.y));
float inside = saturate(.5 - (sd + border) / aa);

// These are the existing HUD colors in linear space: #021716, #2C3735, #8B0000, #EE9D58.
float3 ink = grid.LinearRGB(float3(2., 23., 22.));
float3 empty = grid.LinearRGB(float3(44., 55., 53.));
float3 amber = grid.LinearRGB(float3(238., 157., 88.));
float3 critical = grid.LinearRGB(float3(139., 0., 0.));
float3 healthy = float3(.15, .9, .35);
float3 life = Fractions.x < .5 ? lerp(critical, amber, 2. * Fractions.x) : lerp(amber, healthy, 2. * Fractions.x - 1.);

float2 span = max(size - 2. * border, .001);
float2 local = p - border;
float2 pixel = max(fwidth(p), .001);
float3 body = grid.Row(local.x, span.x, pixel.x, Fractions.x, CellSpans.x, life, empty, ink);
if (CellSpans.y > 0.)
{
    float3 shield = grid.Row(local.x, span.x, pixel.x, Fractions.y, CellSpans.y, float3(1., 1., 1.), empty, ink);
    shield = lerp(empty, shield, ShieldFade);
    float shieldWeight = saturate(.5 + (.3 * span.y - local.y) / pixel.y);
    float lifeWeight = saturate(.5 + (local.y - .4 * span.y) / pixel.y);
    // The middle band belongs to the same opaque body as the two rows.
    body = shield * shieldWeight + body * lifeWeight + empty * (1. - shieldWeight - lifeWeight);
}
return float4(lerp(ink, body, inside), silhouette * Fade);
