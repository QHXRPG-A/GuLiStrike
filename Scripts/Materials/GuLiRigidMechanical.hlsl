// Embedded in MF_GuLiRigidMechanical by build_mass_rigid_materials.py.
// UV1 = pivot.xy, UV2 = pivot.z/part; all distances are authored mesh cm.
// Pose 0..13 matches GuLiMechanicalAnimation::BuildFrame. UE pitch is +X -> +Z.
struct RigidMath
{
    float3 Pitch(float3 v, float a) { float s,c; sincos(a,s,c); return float3(c*v.x-s*v.z,v.y,s*v.x+c*v.z); }
    float3 Yaw(float3 v, float a) { float s,c; sincos(a,s,c); return float3(c*v.x-s*v.y,s*v.x+c*v.y,v.z); }
    float3 Axis(float3 v, float3 axis, float a) { float s,c; sincos(a,s,c); return v*c+cross(axis,v)*s+axis*dot(axis,v)*(1-c); }
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
        p = pivot + r.Pitch(p-pivot, pitch); n = r.Pitch(n, pitch);
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
        p = UpperPivot+r.Yaw(p-UpperPivot,V0); n = r.Yaw(n,V0);
    }
    if (part >= 6 && part <= 9)
    {
        float angle=length(float2(V5,V6));
        float3 axis=float3(V5,V6,0)/max(angle,0.000001);
        p=pivot+r.Axis(p-pivot,axis,angle); n=r.Axis(n,axis,angle);
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
