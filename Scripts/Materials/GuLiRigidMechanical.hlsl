// Embedded in MF_GuLiRigidMechanical by build_mass_rigid_materials.py.
// UV1 = pivot.xy, UV2 = pivot.z/part; all distances are authored mesh cm.
// Pose 0..13: legacy; 14..15: upper/leg bank; 16..23: stabilized gun quaternions.
// Matches BuildVisualFrame / ResolveVisualMuzzle / ResolveVisualNozzle. UE pitch: +X -> +Z.
struct RigidMath
{
    float3 Pitch(float3 v, float a) { float s,c; sincos(a,s,c); return float3(c*v.x-s*v.z,v.y,s*v.x+c*v.z); }
    float3 Yaw(float3 v, float a) { float s,c; sincos(a,s,c); return float3(c*v.x-s*v.y,s*v.x+c*v.y,v.z); }
    float3 Axis(float3 v, float3 axis, float a) { float s,c; sincos(a,s,c); return v*c+cross(axis,v)*s+axis*dot(axis,v)*(1-c); }
    float3 Quat(float3 v, float4 q) { return v+2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
};
RigidMath r;
float3 p = Position;
float3 n = LocalNormal;
float3 pivot = float3(PivotXY, PivotZPart.x);
int part = (int)round(PivotZPart.y);
if (Kind > 0.5 && Kind < 1.5)
{
    if (part == 4 || part == 5) p.x -= part == 4 ? V3 : V4;
    if (part >= 2 && part <= 5)
    {
        float pitch = (part == 2 || part == 4) ? V1 : V2;
        float4 q = (part == 2 || part == 4) ? float4(V16,V17,V18,V19) : float4(V20,V21,V22,V23);
        // Zero means legacy/editor pose. A visual pose supplies the full stabilized rotation.
        if (dot(q,q) > 0.5) { p=pivot+r.Quat(p-pivot,q); n=r.Quat(n,q); }
        else { p = pivot + r.Pitch(p-pivot, pitch); n = r.Pitch(n, pitch); }
    }
    bool missilePod = part == 10 || part == 11;
    if (missilePod && PodVisible < 0.5)
    {
        // Degenerate all pod and contour triangles inside the upper body. This
        // removes silhouette/shadow/hit-overlay coverage without translucency.
        p = UpperPivot; n = float3(0,0,1);
    }
    if ((part >= 1 && part <= 5) || missilePod)
    {
        p = UpperPivot+r.Axis(r.Yaw(p-UpperPivot,V0),float3(1,0,0),V14);
        n = r.Axis(r.Yaw(n,V0),float3(1,0,0),V14);
    }
    if (part >= 6 && part <= 9)
    {
        float angle=length(float2(V5,V6));
        float3 axis=float3(V5,V6,0)/max(angle,0.000001);
        p=pivot+r.Axis(p-pivot,axis,angle); n=r.Axis(n,axis,angle);
    }
    if ((part >= 6 && part <= 9) || (part >= 12 && part <= 19))
    {
        int leg=part<=9 ? part-6 : (part-12)%4;
        float3 root=leg==0?LegRoot0:(leg==1?LegRoot1:(leg==2?LegRoot2:LegRoot3));
        float3 end=leg==0?LegEnd0:(leg==1?LegEnd1:(leg==2?LegEnd2:LegEnd3));
        float3 axis=leg==0?LegAxis0:(leg==1?LegAxis1:(leg==2?LegAxis2:LegAxis3));
        float angle=-sign(end.y)*V15;
        if (dot(axis,axis)>0.5)
        {
            if (part>=12 && part<=15) { p=root+r.Axis(p-root,axis,angle); n=r.Axis(n,axis,angle); }
            else p+=root+r.Axis(end-root,axis,angle)-end;
        }
    }
    // Final rigid body hover uses authored cm; instance scale is applied only by LocalToWorld.
    p=UpperPivot+r.Pitch(r.Axis(p-UpperPivot,float3(1,0,0),V13),V12)+float3(0,0,V11);
    n=r.Pitch(r.Axis(n,float3(1,0,0),V13),V12);
}
else if (Kind >= 1.5)
{
    if (part == 2) { p=pivot+r.Pitch(p-pivot,V1); n=r.Pitch(n,V1); }
    if (part >= 6 && part <= 9)
    {
        float angle=part==6?V7:(part==7?V8:(part==8?V9:V10));
        p=pivot+r.Axis(p-pivot,float3(0,1,0),angle); n=r.Axis(n,float3(0,1,0),angle);
    }
}
AnimatedNormal=normalize(n);
return p-Position;
