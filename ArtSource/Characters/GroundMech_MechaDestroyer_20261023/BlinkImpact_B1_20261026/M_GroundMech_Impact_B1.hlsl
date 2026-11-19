
float t=saturate(Age);
float2 p=(UV-0.5)*2.0;
float r=length(p), a=atan2(p.y,p.x);
float expansion=lerp(0.65,1.0,saturate(t*4.0));
float teeth=pow(saturate(cos(a*7.0+0.7)),10.0);
float radius=(0.24+0.43*teeth)*expansion;
float aa=max(fwidth(r),0.002);
float burst=1.0-smoothstep(radius-aa,radius+aa,r);
float core=1.0-smoothstep(radius*0.42-aa,radius*0.42+aa,r);
float ringRadius=lerp(0.22,0.88,t);
float ring=(1.0-smoothstep(0.018,0.035,abs(r-ringRadius)))*(1.0-t)*0.35;
float chips=0.0;
for(int i=0;i<6;i++) {
 float angle=i*1.0472+0.26; float2 dir=float2(cos(angle),sin(angle));
 float2 pos=dir*lerp(0.46,0.91,t);
 float2 v=p-pos; float d=max(abs(dot(v,dir))*0.62,abs(dot(v,float2(-dir.y,dir.x))));
 chips=max(chips,1.0-smoothstep(0.021,0.028,d));
}
float fade=1.0-smoothstep(0.0,1.0,t);
float mask=saturate(burst+ring+chips*0.65)*fade;
return float4((Heat.rgb*(burst+ring+chips)+Core.rgb*core*1.35)*5.0,mask);
