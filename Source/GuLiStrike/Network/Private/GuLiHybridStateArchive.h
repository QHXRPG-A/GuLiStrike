#pragma once

#include "CoreMinimal.h"
#include "GuLiNetProtocol.h"
#include "Network/Private/GuLiNetConversions.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "UObject/UnrealType.h"
#include <bit>

/** Bounded private-state encoding. No UObject, entity, navigation or process-local name indices. */
class FGuLiHybridStateArchive
{
    GuLiNet::Writer Writer;
    GuLiNet::Reader Reader;
    bool bReading=false,bValid=true;
public:
    explicit FGuLiHybridStateArchive(uint32 Limit=256*1024) : Writer(Limit),Reader({}) {}
    explicit FGuLiHybridStateArchive(TConstArrayView<uint8> Bytes,uint32 Limit=256*1024)
        : Writer(0),Reader(std::span<const uint8>(Bytes.GetData(),Bytes.Num())),bReading(true)
    { bValid=Bytes.Num()>0 && uint32(Bytes.Num())<=Limit; }
    bool IsReading() const { return bReading; }
    bool Good() const { return bValid && (bReading ? true : Writer.Good()); }
    bool Done() const { return Good() && (!bReading || Reader.Done()); }
    void Reject() { bValid=false; }
    bool Finish(TArray<uint8>& Out)
    {
        if (bReading || !Good()) return false;
        const auto Bytes=Writer.Take(); if (Bytes.empty()) return false;
        Out.Reset(); Out.Append(Bytes.data(),static_cast<int32>(Bytes.size())); return true;
    }
    void Value(uint8& V) { if (bReading) bValid=Reader.U8(V)&&bValid; else Writer.U8(V); }
    void Value(uint16& V) { if (bReading) bValid=Reader.U16(V)&&bValid; else Writer.U16(V); }
    void Value(uint32& V) { if (bReading) bValid=Reader.U32(V)&&bValid; else Writer.U32(V); }
    void Value(uint64& V) { if (bReading) bValid=Reader.U64(V)&&bValid; else Writer.U64(V); }
    void Value(int32& V) { uint32 Bits=std::bit_cast<uint32>(V); Value(Bits); if (bReading) V=std::bit_cast<int32>(Bits); }
    void Value(int64& V) { uint64 Bits=std::bit_cast<uint64>(V); Value(Bits); if (bReading) V=std::bit_cast<int64>(Bits); }
    void Value(bool& V) { uint8 B=V?1:0; Value(B); if (B>1) Reject(); if (bReading) V=B!=0; }
    void Value(float& V) { if (bReading) bValid=Reader.F32(V)&&bValid; else Writer.F32(V); if (!FMath::IsFinite(V)) Reject(); }
    void Value(double& V) { if (bReading) bValid=Reader.F64(V)&&bValid; else Writer.F64(V); if (!FMath::IsFinite(V)) Reject(); }
    void Value(FVector& V) { Value(V.X); Value(V.Y); Value(V.Z); }
    void Value(FVector2D& V) { Value(V.X); Value(V.Y); }
    void Value(FQuat& V)
    { Value(V.X); Value(V.Y); Value(V.Z); Value(V.W); if (FMath::Abs(V.SizeSquared()-1.)>.001) Reject(); }
    void Value(FRotator& V) { Value(V.Pitch); Value(V.Yaw); Value(V.Roll); }
    void Value(FTransform& V)
    {
        auto Position=V.GetLocation(); auto Rotation=V.GetRotation(); auto Scale=V.GetScale3D();
        Value(Position); Value(Rotation); Value(Scale);
        if (bReading && Good()) V=FTransform(Rotation,Position,Scale);
    }
    void Value(FGuid& V)
    {
        auto ID=GuLiNetBridge::ToCore(V);
        if (bReading) { bValid=Reader.Identity(ID)&&bValid; V=GuLiNetBridge::ToUE(ID); } else Writer.Identity(ID);
    }
    void Value(FGuLiHybridObjectKey& V)
    {
        uint8 Kind=static_cast<uint8>(V.Kind); Value(Kind); Value(V.StableId); Value(V.LifeGeneration);
        if (Kind>static_cast<uint8>(EGuLiHybridObjectKind::BusinessWork)) Reject();
        if (bReading && Good()) V.Kind=static_cast<EGuLiHybridObjectKind>(Kind);
    }
    void String(FString& V,uint32 Limit=4096)
    {
        if (bReading) { std::string S; bValid=Reader.Text(S,Limit)&&bValid; V=UTF8_TO_TCHAR(S.c_str()); }
        else { const std::string S=TCHAR_TO_UTF8(*V); if (S.size()>Limit) Reject(); else Writer.Text(S); }
    }
    void Value(FName& V) { FString S=V.ToString(); String(S,256); if (bReading && Good()) V=FName(*S); }
    template<class E> void Enum(E& V,uint8 Last)
    { uint8 N=static_cast<uint8>(V); Value(N); if (N>Last) Reject(); if (bReading && Good()) V=static_cast<E>(N); }
    template<class T,class F> void Array(TArray<T>& Values,uint32 Maximum,F Serialize)
    {
        uint32 Count=static_cast<uint32>(Values.Num()); Value(Count);
        if (!Good() || Count>Maximum) { Reject(); return; }
        if (bReading) { Values.Reset(); Values.SetNum(Count); }
        for (auto& V : Values) { Serialize(*this,V); if (!Good()) break; }
    }
    void Bytes(TArray<uint8>& V,uint32 Maximum=128*1024)
    {
        if (bReading)
        {
            GuLiNet::Bytes Data; bValid=Reader.Blob(Data,Maximum)&&bValid;
            if (Good()) { V.Reset(); V.Append(Data.data(),static_cast<int32>(Data.size())); }
        }
        else if (V.Num()>static_cast<int32>(Maximum)) Reject();
        else Writer.Blob(std::span<const uint8>(V.GetData(),V.Num()));
    }
    void Random(FRandomStream& V)
    {
        int32 Initial=V.GetInitialSeed(),Current=V.GetCurrentSeed(); Value(Initial); Value(Current);
        if (bReading && Good())
        {
            // These reflected integer fields are the complete FRandomStream state.
            auto* Type=TBaseStructure<FRandomStream>::Get();
            const auto* InitialProperty=FindFProperty<FIntProperty>(Type,TEXT("InitialSeed"));
            const auto* SeedProperty=FindFProperty<FIntProperty>(Type,TEXT("Seed"));
            if (!InitialProperty || !SeedProperty) { Reject(); return; }
            InitialProperty->SetPropertyValue_InContainer(&V,Initial);
            SeedProperty->SetPropertyValue_InContainer(&V,Current);
        }
    }
};
