// GPU world-space history, centimetres. One live head and nine history lanes.
// Legacy upload contract: Size=(1.1 * disc diameter, actual horizontal speed).
// Meta.b carries the CPU-resolved local-view opacity. Zero is the legacy sender.
// A distance clock avoids resampling old buckets when the speed changes.
AnchorOut=SavedAnchor; HeadingOut=SavedHeading; SampleDistanceOut=SavedSampleDistance;
TravelOut=SavedTravel; PhaseOut=SavedPhase; PreviousOut=NozzlePosition;
LastMovingOut=SavedLastMoving; BornOut=Born; GenerationOut=SeenGeneration;
SpanOut=SavedSpan; PreviousTimeOut=Now; LengthOut=SavedLength;
WidthOut=SavedWidth;
MoveAgeOut=SavedMoveAge;
float speed=max(0.0,NozzleSize.y);
bool active=Meta.g>0.5 && NozzleSize.x>0.0;
bool moving=active && speed>5.0;
WasMovingOut=moving ? 1.0 : 0.0;
float dt=Now-PreviousTime;
bool reset=active && (abs(Meta.r-SeenGeneration)>0.5 || dt<0.0 || dt>0.5);
float target=lerp(MinLength,MaxLength,saturate((speed-720.0)/720.0));
// A new movement must not revive the previous full-length strand.
bool starting=reset || (moving && SavedWasMoving<0.5);
if(starting)
{
    GenerationOut=Meta.r; TravelOut=0.0; PhaseOut=0.0;
    BornOut=-100000.0; SampleDistanceOut=-100000.0;
    LastMovingOut=moving ? Now : -100000.0;
    MoveAgeOut=0.0; LengthOut=0.0;
}
if(!starting && moving && dt>0.0)
{
    float3 delta=NozzlePosition-PreviousPosition;
    float distanceMoved=length(delta.xy);
    // A discontinuity invalidates the old strand even if a caller misses reset.
    if(distanceMoved>max(250.0,speed*max(dt,0.0)*3.0))
    {
        BornOut=-100000.0; SampleDistanceOut=-100000.0;
        TravelOut=0.0; PhaseOut=0.0; MoveAgeOut=0.0; LengthOut=0.0;
    }
    else
    {
        MoveAgeOut=SavedMoveAge+dt;
        float growth=saturate(MoveAgeOut/max(GrowTime,0.001));
        growth=growth*growth*(3.0-2.0*growth);
        // Smoothly reveal the actual travelled path; never fabricate a full
        // target-length tail on the first moving frame.
        LengthOut=min(SavedTravel+distanceMoved,target*growth);
        WidthOut=NozzleSize.x/1.10*0.24;
        float spacing=target/9.0;
        float advance=distanceMoved/spacing;
        PhaseOut=SavedPhase+advance;
        TravelOut=SavedTravel+distanceMoved;
        LastMovingOut=Now;
        int newest=(int)floor(PhaseOut);
        int laneBucket=newest-((newest-(Lane-1)+9) % 9);
        if(Lane==0)
        {
            // Fill the live end even when the grown tail is shorter than one
            // history interval. The core cone remains in the other emitter.
            AnchorOut=NozzlePosition;
            HeadingOut=distanceMoved>0.001 ? normalize(float3(delta.xy,0)) : SavedHeading;
            SampleDistanceOut=TravelOut; BornOut=Now; SpanOut=spacing*1.65;
        }
        else if(advance>0.00001 && laneBucket>(int)floor(SavedPhase))
        {
            float fraction=saturate((float(laneBucket)-SavedPhase)/advance);
            AnchorOut=lerp(PreviousPosition,NozzlePosition,fraction);
            HeadingOut=distanceMoved>0.001 ? normalize(float3(delta.xy,0)) : SavedHeading;
            SampleDistanceOut=SavedTravel+distanceMoved*fraction;
            BornOut=Now-dt*(1.0-fraction);
            SpanOut=spacing;
        }
    }
}
float behind=max(0.0,TravelOut-SampleDistanceOut);
float distanceFade=pow(saturate(1.0-behind/max(LengthOut,1.0)),0.45);
float stopFade=saturate(1.0-max(0.0,Now-LastMovingOut)/0.30);
// Keep the last admitted opacity for the short retired-tail fade. A current
// source uses the same near/tactical/overview policy as its attached core.
// Legacy senders already gate admission on the CPU. Never add a second,
// camera-distance gate here: it would hide admitted tactical-view particles.
ViewFadeOut=active ? (Meta.b>0.0 ? saturate(Meta.b) : 1.0) : SavedViewFade;
float viewFade=ViewFadeOut;
float valid=BornOut>-99999.0 && behind<=LengthOut ? 1.0 : 0.0;
AlignmentOut=HeadingOut;
// Taper the width along the travelled path; overlap adjacent soft segments.
float width=max(18.0,WidthOut)*lerp(0.22,1.0,distanceFade);
float halfSpan=max(SpanOut*1.65,40.0)*0.5;
float backward=min(halfSpan,max(0.0,LengthOut-behind));
float forward=min(halfSpan,behind);
float span=Lane==0 ? min(SpanOut,LengthOut) : backward+forward;
// Clamp both ends to the travelled path: no segment projects ahead of its disc.
PositionOut=Lane==0 ? AnchorOut-HeadingOut*span*0.5 : AnchorOut+HeadingOut*(forward-backward)*0.5;
SizeOut=float2(width,span)*valid;
ColorOut=float4(0.08,0.60,1.0,0.92*distanceFade*stopFade*viewFade*valid);
