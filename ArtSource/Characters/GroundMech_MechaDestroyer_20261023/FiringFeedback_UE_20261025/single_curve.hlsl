
float t = saturate(UV.y);
float center = 0.5 + Bend*sin(6.2831853*t)*sin(3.14159265*t)*smoothstep(0.12,0.3,t);
float d = abs(UV.x-center);
float aa = max(fwidth(d),0.0008);
float head = (1.0/6.0)*sqrt(saturate(1.0-pow((t-0.09)/0.09,2.0)));
float stroke = 0.028*pow(saturate(1.0-t),0.7)*smoothstep(0.0,0.07,t);
float width = max(head,stroke);
float fade = smoothstep(0.0,0.012,t)*(1.0-smoothstep(0.84,1.0,t));
float body = (1.0-smoothstep(width-aa,width+aa,d))*fade;
float core = (1.0-smoothstep(width*0.28-aa,width*0.28+aa,d))*fade;
float glow = exp(-pow(d/max(width+0.022,0.001),2.0)*3.5)*fade*0.18;
float3 color = lerp(HeadColor.rgb,TailColor.rgb,smoothstep(0.08,0.65,t));
float3 rgb = Emission*(color*(body+glow)+CoreColor.rgb*core*lerp(0.65,0.12,t));
return float4(rgb,saturate(body+glow));
