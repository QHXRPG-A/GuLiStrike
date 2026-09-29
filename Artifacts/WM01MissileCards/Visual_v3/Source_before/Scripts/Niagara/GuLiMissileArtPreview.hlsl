// Editor-only illustrative trajectories. The registered combat assets use the
// authoritative CPU/client trajectory via arrays; these preview copies do not.
// Leave 0.8 seconds after the 2.4 s flight + 2.4 s tail before reusing slots.
float t=fmod(Age,5.6);
float a=saturate(t/2.4);
float s=sin(a*3.14159265),c=cos(a*3.14159265);
PositionOut=float3(2200*a,(Slot-1.5)*175+sin(a*6+Slot)*160*s,
    120+s*(430+Slot*100)+sin(a*8+Slot)*70*s);
DirectionOut=normalize(float3(2200,160*(6*cos(a*6+Slot)*s+3.14159265*sin(a*6+Slot)*c),
    3.14159265*c*(430+Slot*100)+70*(8*cos(a*8+Slot)*s+3.14159265*sin(a*8+Slot)*c)));
SizeOut=float2(21,195);
MetaOut=Slot<4 ? float4(floor(Age/5.6)*64+Slot+7,t<2.4?1:2,2.4,2.4) : float4(0,0,0,2.4);
TimeOut=Age;
