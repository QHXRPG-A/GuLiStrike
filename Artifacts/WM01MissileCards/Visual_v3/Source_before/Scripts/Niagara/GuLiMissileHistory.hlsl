// Each GPU lane owns a historical world-space segment. Generation changes reset
// only the reusable live slot; retired quality batches hold immutable array snapshots.
StartOut=SavedStart; EndOut=SavedEnd; BornOut=Born;
SampleOut=SavedSample; GenerationOut=SeenGeneration; BucketOut=SeenBucket;
float state=(EmitHistory<.5 && Meta.g>.5) ? 2 : Meta.g;
TimeOut=Now; StateOut=state;
float lifetime=max(Meta.a,.05);
// One existing lane bridges the latest sample to the live exhaust origin.
// The other lanes span the full lifetime without increasing particle capacity.
float interval=lifetime/(LANES-1);
int bucket=(int)floor(Now/interval);
bool reset=abs(Meta.r-SeenGeneration)>.5 || state<.5 || Now<LastTime || Now-LastTime>.20 || distance(Head,SavedSample)>600;
if(reset)
{
    StartOut=Head; EndOut=Head; SampleOut=Head; BornOut=-10000;
    GenerationOut=Meta.r; BucketOut=bucket;
}
else if((state<1.5 && bucket>SeenBucket) || (state>1.5 && SeenState<1.5))
{
    // Use the next lane when an impact falls inside the current bucket: preserve
    // the preceding full segment instead of overwriting it with the final stub.
    int writeBucket=max(bucket,SeenBucket+1);
    if(writeBucket%LANES==Lane)
    {
        StartOut=SavedSample; EndOut=Head; BornOut=Now;
    }
    SampleOut=Head; BucketOut=writeBucket;
}
if(!reset && state>.5 && state<1.5 && (BucketOut+1)%LANES==Lane)
{
    StartOut=SampleOut; EndOut=Head; BornOut=Now;
}
float age=max(0,Now-BornOut);
float life=saturate(1-age/lifetime);
float3 delta=EndOut-StartOut;
float segment=length(delta);
// Smooth low-frequency world-space drift, bounded to <27 cm over 2.4 seconds.
// Both endpoints use the same field so adjacent narrow segments remain coherent.
float3 midpoint=(StartOut+EndOut)*.5;
float3 drift=float3(sin(midpoint.y*.002+Meta.r*.13),cos(midpoint.x*.002+Meta.r*.13),.55)*min(age,2.4)*7.4;
PositionOut=midpoint+drift;
AlignmentOut=segment>.01 ? delta/segment : float3(1,0,0);
float width=min(33,Width*(1+.55*saturate(age/lifetime)));
SizeOut=(life>0 && segment>.01 && !reset) ? float2(width,segment+width*.12) : float2(0,0);
// A brief warm origin gives way to dense charcoal smoke, never an emissive ribbon.
float heat=1-smoothstep(0,.08,age);
ColorOut=float4(lerp(float3(.055,.060,.065),float3(1.15,.62,.27),heat),.70*pow(life,1.25)*saturate(TrailWeight));
