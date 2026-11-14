// Shared with UGuLiRecoilVATDefinition::Sample/Evaluate. RGBA32F, point sampled.
struct RecoilMath
{
    float4 Read(Texture2D T,SamplerState S,float Column,float Width,float Frame,float Frames)
    {
        float a=floor(Frame),b=min(a+1,Frames-1);
        return lerp(Texture2DSampleLevel(T,S,float2((Column+.5)/Width,(a+.5)/Frames),0),
                    Texture2DSampleLevel(T,S,float2((Column+.5)/Width,(b+.5)/Frames),0),Frame-a);
    }
    float4 Rotation(Texture2D T,SamplerState S,float Frame,float Frames)
    {
        float a=floor(Frame),b=min(a+1,Frames-1);
        float4 q0=Texture2DSampleLevel(T,S,float2(.5,(a+.5)/Frames),0);
        float4 q1=Texture2DSampleLevel(T,S,float2(.5,(b+.5)/Frames),0);
        return normalize(lerp(q0,dot(q0,q1)<0?-q1:q1,Frame-a));
    }
    float4 Mul(float4 b,float4 a)
    {
        return float4(b.w*a.xyz+a.w*b.xyz+cross(b.xyz,a.xyz),b.w*a.w-dot(b.xyz,a.xyz));
    }
    float3 Quat(float3 v,float4 q) { return v+2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
};
RecoilMath recoil;
float3 recoilUpper=0,recoilLeft=0,recoilRight=0;
float4 recoilQ=float4(0,0,0,1);
if (RecoilEnabled>.5)
{
    recoilUpper=recoil.Read(RecoilPosition,RecoilPositionSampler,0,3,RecoilL,RecoilFrames).xyz
               +recoil.Read(RecoilPosition,RecoilPositionSampler,0,3,RecoilR,RecoilFrames).xyz;
    recoilLeft=recoil.Read(RecoilPosition,RecoilPositionSampler,1,3,RecoilL,RecoilFrames).xyz;
    recoilRight=recoil.Read(RecoilPosition,RecoilPositionSampler,2,3,RecoilR,RecoilFrames).xyz;
    recoilQ=recoil.Mul(recoil.Rotation(RecoilRotation,RecoilRotationSampler,RecoilR,RecoilFrames),
                       recoil.Rotation(RecoilRotation,RecoilRotationSampler,RecoilL,RecoilFrames));
    if (RecoilMissileEnabled>.5)
    {
        float4 mq=recoil.Rotation(RecoilRotation,RecoilRotationSampler,RecoilM,RecoilFrames);
        float weight=saturate(2*acos(saturate(abs(mq.w)))/max(RecoilMissilePeakRadians,.0001));
        recoilUpper=recoilUpper*(1-weight)+recoil.Read(RecoilPosition,RecoilPositionSampler,0,3,RecoilM,RecoilFrames).xyz;
        recoilQ=recoil.Mul(mq,normalize(lerp(recoilQ.w<0?-recoilQ:recoilQ,float4(0,0,0,1),weight)));
    }
}
