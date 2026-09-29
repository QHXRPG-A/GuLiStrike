// GPU-only persistent lane. Ten lanes per nozzle, .03 s apart, .30 s lifetime.
// The DI reader supplies current nozzle Position/Direction, Size=(width,speed), Meta=(generation,active,0,0).
// SavedAnchor is a Position (simulation space); direction/age/width are particle state.
AnchorOut=SavedAnchor; DirectionOut=SavedDirection; BornOut=Born; WidthOut=SavedWidth;
GenerationOut=SeenGeneration; BucketOut=SeenBucket;
int bucket=(int)floor(Now/0.03);
bool reset=abs(Meta.r-SeenGeneration)>0.5;
if(reset) { GenerationOut=Meta.r; BucketOut=bucket; }
if(!reset && Meta.g>0.5 && NozzleSize.y>5.0 && bucket%10==Lane && bucket>SeenBucket)
{
    AnchorOut=NozzlePosition; DirectionOut=NozzleDirection; BornOut=Now;
    WidthOut=NozzleSize.x; BucketOut=bucket;
}
float age=max(0.0,Now-BornOut);
PositionOut=AnchorOut+DirectionOut*(80.0*age);
float fade=saturate((20000.0-distance(Camera,PositionOut))/2000.0);
float life=saturate(1.0-age/0.30);
SizeOut=life*fade>0.0 ? float2(WidthOut,WidthOut*.55)*(1.0+min(age,.30)) : float2(0,0);
ColorOut=float4(.04,.28,.95,.45*life*life*fade);
