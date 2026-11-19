// GuLiOutpostHDR_WhiteBelt_Core_x4_v1: HDR is restricted to source material classes 4..7.
float m=floor(Class.x+.5);
float owned=step(.5,Owned)*step(.5,Enabled);
float3 c=float3(0.9911020971,0.7758222183,0.6938717613);
if(m==1)c=float3(0.0202885631,0.0761853815,0.1195384280);else if(m==2)c=float3(0.1441284709,0.3712376805,0.5149176654);else if(m==3)c=float3(0.0103298230,0.0091340587,0.0284260395);
float light=saturate(dot(normalize(Normal),normalize(float3(-.45,-.5,.74)))*.5+.5);
float band=light<.36?.46:(light<.76?.73:1.0);
float3 ink=float3(.01033,.00913,.02843);
float3 paint=lerp(c*band,ink,saturate(Line)*.85);
// Retain a colored broad column; concentrate the highest HDR in its narrow lines.
// Normalize the peak before boosting so darker enemy paint is not a dim lamp.
// A modest chroma curve keeps low channels below the highlight shoulder.
if(m>=4){
    float3 energy=m==4?float3(0.0040247170,0.2788942635,0.3185467781):(m==5?float3(0.1441284709,0.3712376805,0.5149176654):lerp(float3(1,1,1),Primary.rgb,owned));
    // The user's white waist band is separate from the colored guard/crown slots.
    if((m==4||m==5)&&abs(LocalPosition.z)<50.0)energy=float3(1,1,1);
    energy=max(energy,float3(0,0,0));
    float peak=max(max(energy.r,energy.g),energy.b);
    float3 chroma=pow(saturate(energy/max(peak,.0001)),1.35);
    float hdr=m==4?5.6:(m==5?8.0:(m==6?8.8:24.0));
    float gain=m>=6?lerp(1.0,max(0.0,Strength)/.35,owned):1.0;
    paint=chroma*hdr*gain;
}
return paint;