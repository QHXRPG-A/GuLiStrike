// UV0..2 remain the approved art channels. UV3..6 carry four exact skin influences.
// All matrix rows come from the 152-bone affine bake, including nonuniform scale/shear.
// Every count, bone index, pivot and linkage is supplied from authored metadata.
struct GuLiWeightedVATMath
{
    float3 Quat(float3 v,float4 q) { return v+2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
    float4 Rotation(Texture2D tex,SamplerState smp,float bone,float frame,float bones,float frames)
    {
        float first=floor(frame),next=min(first+1,frames-1),alpha=frame-first;
        float4 a=Texture2DSampleLevel(tex,smp,float2((bone+.5)/bones,(first+.5)/frames),0);
        float4 b=Texture2DSampleLevel(tex,smp,float2((bone+.5)/bones,(next+.5)/frames),0);
        return normalize(lerp(a,dot(a,b)<0?-b:b,alpha));
    }
    float4 Row(Texture2D tex, SamplerState smp, float bone, float row, float frame, float bones, float frames)
    {
        float first=floor(frame),next=min(first+1,frames-1),alpha=frame-first;
        float x=(bone*3+row+.5)/(bones*3);
        return lerp(Texture2DSampleLevel(tex,smp,float2(x,(first+.5)/frames),0),
                    Texture2DSampleLevel(tex,smp,float2(x,(next+.5)/frames),0),alpha);
    }
    float3 Point(float3 p,float4 a,float4 b,float4 c)
    { return float3(dot(a,float4(p,1)),dot(b,float4(p,1)),dot(c,float4(p,1))); }
    float3 Normal(float3 n,float4 a,float4 b,float4 c)
    {
        float3 x=cross(b.xyz,c.xyz),y=cross(c.xyz,a.xyz),z=cross(a.xyz,b.xyz);
        float determinant=dot(a.xyz,x);
        return normalize(float3(dot(x,n),dot(y,n),dot(z,n))/max(abs(determinant),1e-8)*sign(determinant));
    }
    float3 Rotate(float3 v,float3 axis,float angle)
    { float s,c;sincos(angle,s,c);axis=normalize(axis);return v*c+cross(axis,v)*s+axis*dot(axis,v)*(1-c); }
    float3 Between(float3 v,float3 a,float3 b)
    {
        a=normalize(a);b=normalize(b);float3 crossAB=cross(a,b);float d=clamp(dot(a,b),-1,1);
        if(d<-.99999) return Rotate(v,abs(a.z)<.9?normalize(cross(a,float3(0,0,1))):normalize(cross(a,float3(0,1,0))),3.14159265);
        return v+cross(crossAB,v)+cross(crossAB,cross(crossAB,v))/max(1+d,1e-6);
    }
};
GuLiWeightedVATMath math;
float4 ids=float4(Indices01.x,1-Indices01.y,Indices23.x,1-Indices23.y);
float4 weights=max(float4(Weights01.x,1-Weights01.y,Weights23.x,1-Weights23.y),0);
weights/=max(dot(weights,float4(1,1,1,1)),1e-8);
float upperBone=floor(UpperBoneIndex+.5),pitchBone=floor(PitchBoneIndex+.5);
float4 ua=lerp(math.Row(BoneAffine,BoneAffineSampler,upperBone,0,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,upperBone,0,OtherFrame,BoneCount,FrameCount),DirectionBlend);
float4 ub=lerp(math.Row(BoneAffine,BoneAffineSampler,upperBone,1,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,upperBone,1,OtherFrame,BoneCount,FrameCount),DirectionBlend);
float4 uc=lerp(math.Row(BoneAffine,BoneAffineSampler,upperBone,2,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,upperBone,2,OtherFrame,BoneCount,FrameCount),DirectionBlend);
float3 upper=math.Point(UpperPivot,ua,ub,uc);
float4 pa=lerp(math.Row(BoneAffine,BoneAffineSampler,pitchBone,0,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,pitchBone,0,OtherFrame,BoneCount,FrameCount),DirectionBlend);
float4 pb=lerp(math.Row(BoneAffine,BoneAffineSampler,pitchBone,1,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,pitchBone,1,OtherFrame,BoneCount,FrameCount),DirectionBlend);
float4 pc=lerp(math.Row(BoneAffine,BoneAffineSampler,pitchBone,2,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,pitchBone,2,OtherFrame,BoneCount,FrameCount),DirectionBlend);
float3 pitch=math.Point(PitchPivot,pa,pb,pc);
float3 result=0,normal=0;
for(int influence=0;influence<4;influence++)
{
    if(weights[influence]<1e-7)continue;
    float bone=clamp(floor(ids[influence]*BoneCount),0,BoneCount-1);
    float4 a0=math.Row(BoneAffine,BoneAffineSampler,bone,0,Frame,BoneCount,FrameCount),a1=math.Row(BoneAffine,BoneAffineSampler,bone,0,OtherFrame,BoneCount,FrameCount);
    float4 b0=math.Row(BoneAffine,BoneAffineSampler,bone,1,Frame,BoneCount,FrameCount),b1=math.Row(BoneAffine,BoneAffineSampler,bone,1,OtherFrame,BoneCount,FrameCount);
    float4 c0=math.Row(BoneAffine,BoneAffineSampler,bone,2,Frame,BoneCount,FrameCount),c1=math.Row(BoneAffine,BoneAffineSampler,bone,2,OtherFrame,BoneCount,FrameCount);
    float4 q0=math.Rotation(BoneRotation,BoneRotationSampler,bone,Frame,BoneCount,FrameCount);
    float4 q1=math.Rotation(BoneRotation,BoneRotationSampler,bone,OtherFrame,BoneCount,FrameCount);
    float4 q=normalize(lerp(q0,dot(q0,q1)<0?-q1:q1,DirectionBlend));
    float3 t0=float3(a0.w,b0.w,c0.w),t1=float3(a1.w,b1.w,c1.w);
    // Blend rotation separately from affine stretch: diagonal gait must not shrink rigid armour.
    float3 v0=math.Quat(math.Point(Position,a0,b0,c0)-t0,float4(-q0.xyz,q0.w));
    float3 v1=math.Quat(math.Point(Position,a1,b1,c1)-t1,float4(-q1.xyz,q1.w));
    float3 p=math.Quat(lerp(v0,v1,DirectionBlend),q)+lerp(t0,t1,DirectionBlend);
    float3 n0=math.Quat(math.Normal(LocalNormal,a0,b0,c0),float4(-q0.xyz,q0.w));
    float3 n1=math.Quat(math.Normal(LocalNormal,a1,b1,c1),float4(-q1.xyz,q1.w));
    float3 n=math.Quat(normalize(lerp(n0,n1,DirectionBlend)),q);
    int flag=(int)round(Texture2DSampleLevel(BonePosition,BonePositionSampler,float2((bone+.5)/BoneCount,.5/FrameCount),0).w);
    bool linkage=false;
    for(int pair=0;pair<2;pair++)
    {
        float2 indices=pair==0?LinkageIndices0:LinkageIndices1;
        float3 refA=pair==0?LinkageAnchorA0:LinkageAnchorA1;
        float3 refB=pair==0?LinkageAnchorB0:LinkageAnchorB1;
        if(abs(bone-indices.x)>.1 && abs(bone-indices.y)>.1)continue;
        // Both linkage halves align between their physical anchors; no piston gap at pitch extremes.
        float4 ax=lerp(math.Row(BoneAffine,BoneAffineSampler,indices.x,0,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,indices.x,0,OtherFrame,BoneCount,FrameCount),DirectionBlend);
        float4 ay=lerp(math.Row(BoneAffine,BoneAffineSampler,indices.x,1,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,indices.x,1,OtherFrame,BoneCount,FrameCount),DirectionBlend);
        float4 az=lerp(math.Row(BoneAffine,BoneAffineSampler,indices.x,2,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,indices.x,2,OtherFrame,BoneCount,FrameCount),DirectionBlend);
        float4 bx=lerp(math.Row(BoneAffine,BoneAffineSampler,indices.y,0,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,indices.y,0,OtherFrame,BoneCount,FrameCount),DirectionBlend);
        float4 by=lerp(math.Row(BoneAffine,BoneAffineSampler,indices.y,1,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,indices.y,1,OtherFrame,BoneCount,FrameCount),DirectionBlend);
        float4 bz=lerp(math.Row(BoneAffine,BoneAffineSampler,indices.y,2,Frame,BoneCount,FrameCount),math.Row(BoneAffine,BoneAffineSampler,indices.y,2,OtherFrame,BoneCount,FrameCount),DirectionBlend);
        float3 oldA=math.Point(refA,ax,ay,az),oldB=math.Point(refB,bx,by,bz);
        float3 newA=pitch+math.Rotate(oldA-pitch,PitchAxis,GunPitch),newB=oldB;
        float3 oldAnchor=abs(bone-indices.x)<.1?oldA:oldB,newAnchor=abs(bone-indices.x)<.1?newA:newB;
        p=newAnchor+math.Between(p-oldAnchor,oldB-oldA,newB-newA);
        n=math.Between(n,oldB-oldA,newB-newA);linkage=true;
    }
    if(flag>=2 && !linkage){p=pitch+math.Rotate(p-pitch,PitchAxis,GunPitch);n=math.Rotate(n,PitchAxis,GunPitch);}
    if(flag>=1){p=upper+math.Rotate(p-upper,float3(0,0,1),UpperYaw);n=math.Rotate(n,float3(0,0,1),UpperYaw);}
    result+=p*weights[influence];normal+=n*weights[influence];
}
AnimatedNormal=normalize(normal);
return result-Position;
