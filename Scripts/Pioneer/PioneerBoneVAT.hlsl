// UV2.x selects one of 44 bones; UV2.y = 0 chassis/legs, 1 upper, 2 left gun, 3 right gun.
// Textures are frame-major RGBA32F nearest samples, identical to UGuLiVATDefinition::SampleBone.
struct PioneerMath
{
    float3 Quat(float3 v,float4 q) { return v+2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
    float3 Pitch(float3 v,float a) { float s,c; sincos(a,s,c); return float3(c*v.x-s*v.z,v.y,s*v.x+c*v.z); }
    float3 Yaw(float3 v,float a) { float s,c; sincos(a,s,c); return float3(c*v.x-s*v.y,s*v.x+c*v.y,v.z); }
};
PioneerMath math;
float bone = floor(BoneUV.x*44);
float first = floor(Frame), second = min(first+1,310), alpha = Frame-first;
float2 uv0=float2((bone+.5)/44,(first+.5)/311), uv1=float2((bone+.5)/44,(second+.5)/311);
float3 t=lerp(Texture2DSampleLevel(BonePosition,BonePositionSampler,uv0,0).xyz,
              Texture2DSampleLevel(BonePosition,BonePositionSampler,uv1,0).xyz,alpha);
float4 q0=Texture2DSampleLevel(BoneRotation,BoneRotationSampler,uv0,0);
float4 q1=Texture2DSampleLevel(BoneRotation,BoneRotationSampler,uv1,0);
float4 q=normalize(lerp(q0,dot(q0,q1)<0?-q1:q1,alpha));
float3 p=math.Quat(Position,q)+t;
float3 n=math.Quat(LocalNormal,q);
// Native FBX import flips V on all UV channels, including this integer payload.
int flag=(int)round(1-BoneUV.y);
if (flag>=2)
{
    float3 referencePivot=flag==2?LeftPivot:RightPivot;
    float3 pivot=math.Quat(referencePivot,q)+t;
    float pitch=flag==2?LeftPitch:RightPitch;
    p=pivot+math.Pitch(p-pivot,pitch); n=math.Pitch(n,pitch);
}
if (flag>=1)
{
    float2 a=float2((20+.5)/44,(first+.5)/311),b=float2((20+.5)/44,(second+.5)/311);
    float3 topT=lerp(Texture2DSampleLevel(BonePosition,BonePositionSampler,a,0).xyz,
                    Texture2DSampleLevel(BonePosition,BonePositionSampler,b,0).xyz,alpha);
    float4 topQ0=Texture2DSampleLevel(BoneRotation,BoneRotationSampler,a,0);
    float4 topQ1=Texture2DSampleLevel(BoneRotation,BoneRotationSampler,b,0);
    float4 topQ=normalize(lerp(topQ0,dot(topQ0,topQ1)<0?-topQ1:topQ1,alpha));
    float3 pivot=math.Quat(UpperPivot,topQ)+topT;
    p=pivot+math.Yaw(p-pivot,UpperYaw); n=math.Yaw(n,UpperYaw);
}
AnimatedNormal=normalize(n);
return p-Position;
