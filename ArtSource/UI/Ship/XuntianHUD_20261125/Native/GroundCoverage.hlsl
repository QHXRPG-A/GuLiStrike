// UV.x is the signed available margin sampled from the active ship's actual gun mounts.
// UV.y is zero for a zero-width angle interval. XYZ are Unreal world centimetres.
float pixel = UV.x / max(fwidth(UV.x), 0.0001);
float fill = smoothstep(-0.65, 0.65, pixel) * UV.y;
float band = (WorldPoint.y - 0.72 * WorldPoint.x) / 700.0;
float footprint = max(fwidth(band), 0.005);
float hatch = (1.0 - smoothstep(0.04, 0.04 + footprint, abs(frac(band) - 0.5)))
            * (1.0 - smoothstep(0.14, 0.55, footprint));
float blindAlpha = (0.025 + 0.34 * hatch) * (1.0 - fill);
float openAlpha = 0.17 * fill;
float alpha = blindAlpha + openAlpha;
float3 premul = BlindInk * blindAlpha + OpenInk * openAlpha;
float outline = 0.67 * (1.0 - smoothstep(1.75, 3.1, abs(pixel)));
premul = lerp(premul, DarkInk, outline);
alpha = lerp(alpha, 1.0, outline);
float edge = 0.97 * (1.0 - smoothstep(0.9, 1.85, abs(pixel)));
premul = lerp(premul, OpenInk, edge);
alpha = lerp(alpha, 1.0, edge);
float fade = 1.0 - smoothstep(0.88 * Reach, Reach, distance(Eye, WorldPoint));
return float4(premul / max(alpha, 0.0001), alpha * fade);
