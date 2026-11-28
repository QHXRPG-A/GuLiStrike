// Angular coordinates in world axes keep the equator level through the entire camera orbit.
float3 dir = normalize(WorldPoint - Eye);
float planar = length(dir.xy);
float elevation = atan2(dir.z, planar);
float bearing = atan2(dir.y, dir.x);
float pixel = max(fwidth(elevation), 0.00007);
float phase = abs(sin(18.0 * bearing));
float dash = smoothstep(0.16, 0.33, phase);
float rim = abs(elevation) / pixel;
float core = (1.0 - smoothstep(0.65, 1.55, rim)) * dash;
float outline = (1.0 - smoothstep(1.45, 2.9, rim)) * dash;
float toothWidth = max(fwidth(phase), 0.003);
float teeth = (1.0 - smoothstep(toothWidth, 2.1 * toothWidth, phase))
            * smoothstep(-pixel, pixel, elevation) * (1.0 - smoothstep(0.009, 0.012, elevation));
float stepAngle = 0.261799388;
float ring = abs(frac(elevation / stepAngle + 0.5) - 0.5) * stepAngle / pixel;
float band = (1.0 - smoothstep(0.65, 1.65, ring))
           * (1.0 - smoothstep(0.32, 0.52, abs(sin(6.0 * bearing))))
           * smoothstep(0.12, 0.18, elevation) * (1.0 - smoothstep(1.39, 1.46, elevation));
float pole = abs(atan2(planar, dir.z) - 0.035) / pixel;
band = max(band, (1.0 - smoothstep(0.6, 1.65, pole)) * smoothstep(0.10, 0.35, abs(sin(3.0 * bearing))));
float tint = smoothstep(0.0, 2.0 * pixel, elevation) * (0.024 + 0.09 * (1.0 - smoothstep(0.0, 0.07, elevation)));
float marks = max(core, teeth);
float alpha = max(max(tint, 0.53 * band), max(0.68 * outline, 0.92 * marks));
float3 ink = lerp(OpenInk, DarkInk, outline * (1.0 - marks));
return float4(lerp(ink, lerp(OpenInk, RimInk, 0.23), marks), alpha);
