
float lengthCm = max(Length,0.001);
float along = ChunkOffset+(1.0-saturate(UV.y))*lengthCm;
float behind = HeadDistance-along;
float pointAge = max(behind,0.0)/max(Speed,1.0)+max(EndAge,0.0);
float life = 1.0-smoothstep(Hold,Hold+max(Fade,0.01),pointAge);
// Absolute distance from launch holds the wave in place as the visible window moves.
float curl = Bend*sin(6.2831853*along/max(WaveLength,100.0));
curl *= smoothstep(0.0,100.0,along);
float d = abs((UV.x-0.5)*max(Canvas,1.0)-curl);
float aa = max(fwidth(d),0.12);
float radius = Width*0.5*lerp(0.35,1.0,life);
float body = 1.0-smoothstep(radius-aa,radius+aa,d);
float core = 1.0-smoothstep(radius*0.24-aa,radius*0.24+aa,d);
float glow = exp(-pow(d/max(radius*1.5,0.1),2.0)*3.0)*0.13;
float edge = smoothstep(0.0,12.0,behind)*smoothstep(0.0,150.0,WaveLength-behind)*smoothstep(0.0,80.0,along);
float heat = 1.0-smoothstep(0.0,0.06,pointAge);
float3 color = lerp(TailColor.rgb,HeadColor.rgb,heat);
float3 rgb = Emission*(color*(body+glow)+CoreColor.rgb*core*lerp(0.22,0.6,heat));
// There is deliberately no ellipse/bulb. Every pixel belongs to the same thin stroke.
return float4(rgb,saturate(body+glow)*life*edge*step(1.0,Length));
