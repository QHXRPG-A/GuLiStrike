// Real per-vertex animation. No skeleton, bone index, bone matrix or skinning at runtime.
// UV3.x is a persistent vertex ID; UV3.y identifies a rigid piston half.
// UV4 carries yaw/pitch deformation weights; UV5.x selects the vertex atlas LOD.
struct GuLiVertexVATMath
{
    float3 Quat(float3 v,float4 q) { return v+2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
    float3 Rotate(float3 v,float3 axis,float angle)
    { float s,c;sincos(angle,s,c);axis=normalize(axis);return v*c+cross(axis,v)*s+axis*dot(axis,v)*(1-c); }
    float3 Between(float3 v,float3 a,float3 b)
    {
        a=normalize(a);b=normalize(b);float3 ab=cross(a,b);float d=clamp(dot(a,b),-1,1);
        if(d<-.99999)return Rotate(v,abs(a.z)<.9?normalize(cross(a,float3(0,0,1))):normalize(cross(a,float3(0,1,0))),3.14159265);
        return v+cross(ab,v)+cross(ab,cross(ab,v))/max(1+d,1e-6);
    }
    float RealFrame(float encoded,float samples,float canonical)
    {
        float clip=floor(encoded/canonical);
        return clip>=5 ? 5*samples+clamp(encoded-5*canonical,0,1)
            : clip*samples+frac(encoded/canonical)*canonical/(canonical-1)*(samples-1);
    }
    float4 Sample(Texture2D tex,SamplerState smp,float vertex,float frame,float rows,float height,float width,bool reverseRows)
    {
        // Blender's EXR buffer is bottom-up; the raw rotation PNG is authored top-down.
        float y=(frame*rows+floor(vertex/width)+.5)/height;
        return Texture2DSampleLevel(tex,smp,float2((fmod(vertex,width)+.5)/width,reverseRows?1-y:y),0);
    }
    float4 PositionValue(Texture2D tex,SamplerState smp,float vertex,float frame,float rows,float height,float width)
    { return lerp(Sample(tex,smp,vertex,floor(frame),rows,height,width,true),Sample(tex,smp,vertex,ceil(frame),rows,height,width,true),frac(frame)); }
    float4 RotationValue(Texture2D tex,SamplerState smp,float vertex,float frame,float rows,float height,float width)
    {
        float4 a=normalize(Sample(tex,smp,vertex,floor(frame),rows,height,width,false)*2-1);
        float4 b=normalize(Sample(tex,smp,vertex,ceil(frame),rows,height,width,false)*2-1);
        return normalize(lerp(a,dot(a,b)<0?-b:b,frac(frame)));
    }
};
GuLiVertexVATMath math;
int lod=(int)clamp(round(LODIndex.x),0,2);
float count=lod==0?Count0:(lod==1?Count1:Count2);
float rows=lod==0?Rows0:(lod==1?Rows1:Rows2);
float height=lod==0?Height0:(lod==1?Height1:Height2);
float samples=lod==0?Samples0:(lod==1?Samples1:Samples2);
float vertex=clamp(floor(VertexIndex.x*count),0,count-1);
float first=math.RealFrame(Frame,samples,CanonicalSamples),second=math.RealFrame(OtherFrame,samples,CanonicalSamples);
float3 deltaA,deltaB;float4 qA,qB;
if(lod==0){deltaA=math.PositionValue(Position0,Position0Sampler,vertex,first,rows,height,TextureWidth).xyz;deltaB=math.PositionValue(Position0,Position0Sampler,vertex,second,rows,height,TextureWidth).xyz;qA=math.RotationValue(Rotation0,Rotation0Sampler,vertex,first,rows,height,TextureWidth);qB=math.RotationValue(Rotation0,Rotation0Sampler,vertex,second,rows,height,TextureWidth);}
else if(lod==1){deltaA=math.PositionValue(Position1,Position1Sampler,vertex,first,rows,height,TextureWidth).xyz;deltaB=math.PositionValue(Position1,Position1Sampler,vertex,second,rows,height,TextureWidth).xyz;qA=math.RotationValue(Rotation1,Rotation1Sampler,vertex,first,rows,height,TextureWidth);qB=math.RotationValue(Rotation1,Rotation1Sampler,vertex,second,rows,height,TextureWidth);}
else {deltaA=math.PositionValue(Position2,Position2Sampler,vertex,first,rows,height,TextureWidth).xyz;deltaB=math.PositionValue(Position2,Position2Sampler,vertex,second,rows,height,TextureWidth).xyz;qA=math.RotationValue(Rotation2,Rotation2Sampler,vertex,first,rows,height,TextureWidth);qB=math.RotationValue(Rotation2,Rotation2Sampler,vertex,second,rows,height,TextureWidth);}
float4 q=normalize(lerp(qA,dot(qA,qB)<0?-qB:qB,DirectionBlend));
// Vertex-local rigid interpolation preserves armour dimensions during diagonal gait.
// Translation is recovered from the baked vertex position and its baked rotation.
float3 tA=Position+deltaA-math.Quat(Position,qA),tB=Position+deltaB-math.Quat(Position,qB);
float3 p=math.Quat(Position,q)+lerp(tA,tB,DirectionBlend);
float3 n=math.Quat(LocalNormal,q);
float yawWeight=saturate(AimWeights.x),pitchWeight=saturate(1-AimWeights.y);
int flag=(int)round(1-VertexIndex.y);
if(flag>=3 && flag<=6)
{
    bool secondPair=flag>=5,halfA=flag==3 || flag==5;
    float3 a=secondPair?LinkageAnchorA1:LinkageAnchorA0,b=secondPair?LinkageAnchorB1:LinkageAnchorB0;
    float3 newA=PitchPivot+math.Rotate(a-PitchPivot,PitchAxis,GunPitch);
    float3 oldAnchor=halfA?a:b,newAnchor=halfA?newA:b;
    p=newAnchor+math.Between(p-oldAnchor,b-a,b-newA);n=math.Between(n,b-a,b-newA);
}
else
{
    p=lerp(p,PitchPivot+math.Rotate(p-PitchPivot,PitchAxis,GunPitch),pitchWeight);
    n=normalize(lerp(n,math.Rotate(n,PitchAxis,GunPitch),pitchWeight));
}
p=lerp(p,UpperPivot+math.Rotate(p-UpperPivot,float3(0,0,1),UpperYaw),yawWeight);
n=normalize(lerp(n,math.Rotate(n,float3(0,0,1),UpperYaw),yawWeight));
AnimatedNormal=n;
return p-Position;
