// Rebuild the jet from this frame's nozzle every update. Only the separate
// history emitter may retain world positions; the cone never trails behind.
float speed01=saturate((Size.y-60.0)/30.0);
float jetLength=lerp(125.0,140.0,speed01);
float3 nozzle=Center-Down*Size.y*0.5;
PositionOut=nozzle+Down*jetLength*0.5;
SizeOut=Size.x>0 ? float2(Size.x/1.10*0.32,jetLength) : float2(0,0);
ColorOut=float4(1,1,1,min(1.0,Tint.a/0.90)*0.98);
