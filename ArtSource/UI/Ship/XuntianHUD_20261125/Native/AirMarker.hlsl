// UV is measured in design pixels. Each glyph, including its outline, occupies one quad.
// Style.x selects gun, cross, height dashes or foot. Style.y is the dash pitch.
if (Style.x > 1.5)
{
    float lateralAA = max(fwidth(UV.x), 0.001);
    float coverage = saturate(0.5 + (0.8 - abs(UV.x)) / lateralAA);
    if (Style.x < 2.5)
    {
        float pitch = Style.y;
        float alongAA = max(fwidth(UV.y), 0.001);
        float fromCenter = abs(frac(UV.y / pitch + 0.16) - 0.5) * pitch;
        coverage *= saturate(0.5 + (pitch * 0.30 - fromCenter) / alongAA);
    }
    return float4(Ink.rgb, coverage * 0.82);
}

struct FStroke
{
    float Distance(float2 p, float2 a, float2 b)
    {
        float2 v = b - a;
        return length(p - a - v * saturate(dot(p - a, v) / dot(v, v)));
    }
};
FStroke stroke;
float2 p = UV * (32.0 / 28.0);
float d;
if (Style.x > 0.5)
{
    d = min(stroke.Distance(p, float2(-7,-7), float2(7,7)),
            stroke.Distance(p, float2(-7,7), float2(7,-7)));
}
else
{
    static const float4 segments[7] = {
        float4(-8,6,5,6), float4(0,3,5,-5), float4(3,4,8,-4), float4(-6,4,-4,0),
        float4(-4,0,0,0), float4(0,0,2,4), float4(2,4,-6,4)
    };
    d = 1e4;
    [unroll] for (int i = 0; i < 7; ++i)
        d = min(d, stroke.Distance(p, segments[i].xy, segments[i].zw));
}
d *= 28.0 / 32.0;
float aa = max(fwidth(d), max(fwidth(UV.x), fwidth(UV.y)) * 0.8);
aa = max(aa, 0.001);
float outer = saturate(0.5 + (1.9 - d) / aa);
float inner = saturate(0.5 + (0.95 - d) / aa);
return float4(lerp(DarkInk, Ink.rgb, inner / max(outer, 0.001)), outer);
