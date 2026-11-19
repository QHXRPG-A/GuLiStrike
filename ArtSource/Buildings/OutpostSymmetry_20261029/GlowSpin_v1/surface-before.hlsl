float m=floor(Class.x+.5);
float owned=step(.5,Owned)*step(.5,Enabled);
float3 c=float3(0.9911020971,0.7758222183,0.6938717613);
if(m==1)c=float3(0.0202885631,0.0761853815,0.1195384280);else if(m==2)c=float3(0.1441284709,0.3712376805,0.5149176654);else if(m==3)c=float3(0.0103298230,0.0091340587,0.0284260395);
float light=saturate(dot(normalize(Normal),normalize(float3(-.45,-.5,.74)))*.5+.5);
float band=light<.36?.46:(light<.76?.73:1.0);
float3 ink=float3(.01033,.00913,.02843);
float3 paint=lerp(c*band,ink,saturate(Line)*.85);
if(m==4)paint=float3(0.0040247170,0.2788942635,0.3185467781)*1.7;
else if(m==5)paint=float3(0.1441284709,0.3712376805,0.5149176654)*3.5;
else if(m>=6){float3 core=lerp(float3(1,1,1),Primary.rgb,owned);
float gain=lerp(1.0,max(0.0,Strength)/.35,owned);
paint=core*1.65*(m==6?.36:1.10)*gain;}
return paint;