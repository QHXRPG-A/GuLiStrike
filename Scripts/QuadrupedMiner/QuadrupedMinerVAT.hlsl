// Position/normal VAT for a static mesh. No runtime skeleton or dynamic material instances.
// UV0.x: position texel ID; UV1.rg + UV2.r: crease distances; UV2.g: normal texel ID.
// Atlas addresses are integer texel IDs. The mesh uses full precision UVs.
// EXR position rows are bottom-up; the authored normal PNG is top-down.
struct FQuadrupedVAT
{
    float4 Read(Texture2D Tex, SamplerState Samp, float ID, float Frame, float Rows, float Height, bool Reverse)
    {
        float Y = (Frame * Rows + floor(ID / 4096.0) + 0.5) / Height;
        return Texture2DSampleLevel(Tex, Samp, float2((fmod(ID, 4096.0) + 0.5) / 4096.0, Reverse ? 1.0 - Y : Y), 0);
    }
    float4 ReadPair(Texture2D Tex, SamplerState Samp, float ID, float Frame, float Rows, float Height, bool Reverse)
    {
        return lerp(Read(Tex, Samp, ID, floor(Frame), Rows, Height, Reverse),
            Read(Tex, Samp, ID, ceil(Frame), Rows, Height, Reverse), frac(Frame));
    }
};
FQuadrupedVAT VAT;
int LOD = (int)clamp(round(LODIndex), 0, 2);
float RowsP = LOD == 0 ? PositionRows0 : (LOD == 1 ? PositionRows1 : PositionRows2);
float RowsN = LOD == 0 ? NormalRows0 : (LOD == 1 ? NormalRows1 : NormalRows2);
float HeightP = LOD == 0 ? PositionHeight0 : (LOD == 1 ? PositionHeight1 : PositionHeight2);
float HeightN = LOD == 0 ? NormalHeight0 : (LOD == 1 ? NormalHeight1 : NormalHeight2);
float PositionID = floor(PositionUV.x * 4096.0);
// FBX import flips the mesh V coordinates; atlas sampling itself stays top-down.
float NormalID = floor((1.0 - NormalUV.y) * 4096.0);
float3 P0, P1, N0, N1;
if (LOD == 0)
{
    P0 = VAT.ReadPair(Position0, Position0Sampler, PositionID, Frame, RowsP, HeightP, true).xyz;
    P1 = VAT.ReadPair(Position0, Position0Sampler, PositionID, PreviousFrame, RowsP, HeightP, true).xyz;
    N0 = VAT.ReadPair(Normal0, Normal0Sampler, NormalID, Frame, RowsN, HeightN, false).xyz;
    N1 = VAT.ReadPair(Normal0, Normal0Sampler, NormalID, PreviousFrame, RowsN, HeightN, false).xyz;
}
else if (LOD == 1)
{
    P0 = VAT.ReadPair(Position1, Position1Sampler, PositionID, Frame, RowsP, HeightP, true).xyz;
    P1 = VAT.ReadPair(Position1, Position1Sampler, PositionID, PreviousFrame, RowsP, HeightP, true).xyz;
    N0 = VAT.ReadPair(Normal1, Normal1Sampler, NormalID, Frame, RowsN, HeightN, false).xyz;
    N1 = VAT.ReadPair(Normal1, Normal1Sampler, NormalID, PreviousFrame, RowsN, HeightN, false).xyz;
}
else
{
    P0 = VAT.ReadPair(Position2, Position2Sampler, PositionID, Frame, RowsP, HeightP, true).xyz;
    P1 = VAT.ReadPair(Position2, Position2Sampler, PositionID, PreviousFrame, RowsP, HeightP, true).xyz;
    N0 = VAT.ReadPair(Normal2, Normal2Sampler, NormalID, Frame, RowsN, HeightN, false).xyz;
    N1 = VAT.ReadPair(Normal2, Normal2Sampler, NormalID, PreviousFrame, RowsN, HeightN, false).xyz;
}
AnimatedNormal = normalize(lerp(N0, N1, 0.5) * 2.0 - 1.0);
return lerp(P0, P1, 0.5) - Position;
