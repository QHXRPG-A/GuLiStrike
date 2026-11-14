// Explicit UE-local absolute positions; dense, uncompressed source timing.
// Full-precision integer IDs prevent StaticMeshBuilder welding adjacent atlas IDs.
// UV0=(position ID, LOD); UV2.y=normal ID.
struct FGuLiMinerAtlas
{
    float3 Sample(Texture2D T, SamplerState S, float Id, float Frame, float Rows, float Height, bool Flip)
    {
        float X=(fmod(Id,2048.0)+0.5)/2048.0;
        float Row=floor(Id/2048.0);
        float A=(floor(Frame)*Rows+Row+0.5)/Height;
        float B=(ceil(Frame)*Rows+Row+0.5)/Height;
        return lerp(Texture2DSampleLevel(T,S,float2(X,Flip?1-A:A),0).xyz,
            Texture2DSampleLevel(T,S,float2(X,Flip?1-B:B),0).xyz,frac(Frame));
    }
};
FGuLiMinerAtlas Atlas;
int L=(int)clamp(round(PUV.y),0,2);
float PId=round(PUV.x), NId=round(NUV.y);
float3 P0,P1,N0,N1;
if(L==0)
{
    P0=Atlas.Sample(Position0,Position0Sampler,PId,FrameA,PRows0,PHeight0,true);
    P1=Atlas.Sample(Position0,Position0Sampler,PId,FrameB,PRows0,PHeight0,true);
    N0=Atlas.Sample(Normal0,Normal0Sampler,NId,FrameA,NRows0,NHeight0,false);
    N1=Atlas.Sample(Normal0,Normal0Sampler,NId,FrameB,NRows0,NHeight0,false);
}
else if(L==1)
{
    P0=Atlas.Sample(Position1,Position1Sampler,PId,FrameA,PRows1,PHeight1,true);
    P1=Atlas.Sample(Position1,Position1Sampler,PId,FrameB,PRows1,PHeight1,true);
    N0=Atlas.Sample(Normal1,Normal1Sampler,NId,FrameA,NRows1,NHeight1,false);
    N1=Atlas.Sample(Normal1,Normal1Sampler,NId,FrameB,NRows1,NHeight1,false);
}
else
{
    P0=Atlas.Sample(Position2,Position2Sampler,PId,FrameA,PRows2,PHeight2,true);
    P1=Atlas.Sample(Position2,Position2Sampler,PId,FrameB,PRows2,PHeight2,true);
    N0=Atlas.Sample(Normal2,Normal2Sampler,NId,FrameA,NRows2,NHeight2,false);
    N1=Atlas.Sample(Normal2,Normal2Sampler,NId,FrameB,NRows2,NHeight2,false);
}
AnimatedNormal=normalize(lerp(N0,N1,saturate(Blend))*2-1);
return lerp(P0,P1,saturate(Blend))-Rest;
