#include "Network/Private/GuLiHybridReflectedState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr uint32 Schema=1,MaximumItems=4096,MaximumFields=256,MaximumDepth=32,MaximumValues=65536;
struct FContext {uint32 Values=0;uint64 Allocation=0;};
bool Allocation(FGuLiHybridStateArchive& A,FContext& C,uint64 Bytes)
{
    if (A.IsReading()) C.Allocation+=Bytes;
    if (Bytes>8*1024*1024 || C.Allocation>32*1024*1024) {A.Reject();return false;}
    return true;
}
bool Field(FGuLiHybridStateArchive&,const FProperty*,void*,FContext&,uint32);
bool Structure(FGuLiHybridStateArchive& A,const UScriptStruct* Type,void* Data,FContext& C,uint32 Depth)
{
    if (!Type || !Data || Depth>MaximumDepth || ++C.Values>MaximumValues) {A.Reject();return false;}
    FString Path=Type->GetPathName();A.String(Path,512);
    if (Path!=Type->GetPathName()) {A.Reject();return false;}
    if (Type==TBaseStructure<FGuid>::Get()) {A.Value(*static_cast<FGuid*>(Data));return A.Good();}
    if (Type==TBaseStructure<FVector>::Get()) {A.Value(*static_cast<FVector*>(Data));return A.Good();}
    if (Type==TBaseStructure<FVector2D>::Get()) {A.Value(*static_cast<FVector2D*>(Data));return A.Good();}
    if (Type==TBaseStructure<FIntVector>::Get()) {auto& V=*static_cast<FIntVector*>(Data);A.Value(V.X);A.Value(V.Y);A.Value(V.Z);return A.Good();}
    if (Type==TBaseStructure<FIntPoint>::Get()) {auto& V=*static_cast<FIntPoint*>(Data);A.Value(V.X);A.Value(V.Y);return A.Good();}
    if (Type==TBaseStructure<FRotator>::Get()) {A.Value(*static_cast<FRotator*>(Data));return A.Good();}
    if (Type==TBaseStructure<FQuat>::Get()) {A.Value(*static_cast<FQuat*>(Data));return A.Good();}
    if (Type==TBaseStructure<FTransform>::Get()) {A.Value(*static_cast<FTransform*>(Data));return A.Good();}
    if (Type==TBaseStructure<FRandomStream>::Get()) {A.Random(*static_cast<FRandomStream*>(Data));return A.Good();}
    TArray<const FProperty*> Properties;
    for (TFieldIterator<FProperty> It(Type);It;++It) Properties.Add(*It);
    Properties.Sort([](const FProperty& L,const FProperty& R){return L.GetFName().LexicalLess(R.GetFName());});
    uint32 Count=Properties.Num();A.Value(Count);
    if (!Count || Count!=uint32(Properties.Num()) || Count>MaximumFields) {A.Reject();return false;}
    for (const auto* P:Properties)
    {
        FString Name=P->GetName();A.String(Name,256);uint32 Dimension=P->ArrayDim;A.Value(Dimension);
        if (Name!=P->GetName() || !Dimension || Dimension!=uint32(P->ArrayDim) || Dimension>MaximumFields) {A.Reject();return false;}
        for (uint32 Index=0;Index<Dimension;++Index)
            if (!Field(A,P,P->ContainerPtrToValuePtr<void>(Data,Index),C,Depth+1)) return false;
    }
    return A.Good();
}
bool Encoded(const FProperty* P,void* Data,TArray<uint8>& Bytes,FContext& C,uint32 Depth)
{
    FGuLiHybridStateArchive A(1024*1024);return Field(A,P,Data,C,Depth) && A.Finish(Bytes);
}
bool Less(const TArray<uint8>& L,const TArray<uint8>& R)
{
    const int32 Size=FMath::Min(L.Num(),R.Num());
    const int32 Compare=Size?FMemory::Memcmp(L.GetData(),R.GetData(),Size):0;
    return Compare<0 || (!Compare && L.Num()<R.Num());
}
bool Field(FGuLiHybridStateArchive& A,const FProperty* P,void* Data,FContext& C,uint32 Depth)
{
    if (!P || !Data || Depth>MaximumDepth || ++C.Values>MaximumValues) {A.Reject();return false;}
    FString Type=P->GetCPPType();A.String(Type,512);if (Type!=P->GetCPPType()) {A.Reject();return false;}
    if (const auto* B=CastField<FBoolProperty>(P))
    {bool V=B->GetPropertyValue(Data);A.Value(V);if (A.IsReading() && A.Good()) B->SetPropertyValue(Data,V);return A.Good();}
    if (const auto* E=CastField<FEnumProperty>(P)) return Field(A,E->GetUnderlyingProperty(),Data,C,Depth+1);
    if (const auto* N=CastField<FNumericProperty>(P))
    {
        if (N->IsInteger())
        {
            uint64 V=N->GetUnsignedIntPropertyValue(Data);A.Value(V);
            if (A.IsReading() && A.Good())
            {
                N->SetIntPropertyValue(Data,V);
                // Reject values that cannot be represented in this concrete integer type.
                if (N->GetUnsignedIntPropertyValue(Data)!=V) A.Reject();
            }
        }
        else
        {
            double V=N->GetFloatingPointPropertyValue(Data);A.Value(V);
            if (A.IsReading() && A.Good())
            {N->SetFloatingPointPropertyValue(Data,V);if (!FMath::IsFinite(N->GetFloatingPointPropertyValue(Data))) A.Reject();}
        }
        return A.Good();
    }
    if (const auto* S=CastField<FStrProperty>(P))
    {FString V=S->GetPropertyValue(Data);A.String(V,4096);if (A.IsReading() && A.Good()) S->SetPropertyValue(Data,V);return A.Good();}
    if (const auto* N=CastField<FNameProperty>(P))
    {FName V=N->GetPropertyValue(Data);A.Value(V);if (A.IsReading() && A.Good()) N->SetPropertyValue(Data,V);return A.Good();}
    if (const auto* S=CastField<FStructProperty>(P)) return Structure(A,S->Struct,Data,C,Depth+1);
    if (const auto* Array=CastField<FArrayProperty>(P))
    {
        FScriptArrayHelper Values(Array,Data);uint32 Count=Values.Num();A.Value(Count);
        if (!A.Good() || Count>MaximumItems || C.Values+Count>MaximumValues
            || !Allocation(A,C,uint64(Count)*Array->Inner->GetElementSize())) {A.Reject();return false;}
        if (A.IsReading()) Values.Resize(Count);
        for (uint32 Index=0;Index<Count;++Index)
            if (!Field(A,Array->Inner,Values.GetRawPtr(Index),C,Depth+1)) return false;
        return A.Good();
    }
    if (const auto* Set=CastField<FSetProperty>(P))
    {
        FScriptSetHelper Values(Set,Data);uint32 Count=Values.Num();A.Value(Count);
        if (!A.Good() || Count>MaximumItems || C.Values+Count>MaximumValues
            || !Allocation(A,C,uint64(Count)*(Set->ElementProp->GetElementSize()+32))) {A.Reject();return false;}
        if (!A.IsReading())
        {
            TArray<TArray<uint8>> Ordered;
            for (int32 Index=0;Index<Values.GetMaxIndex();++Index) if (Values.IsValidIndex(Index))
            {TArray<uint8> Bytes;if (!Encoded(Set->ElementProp,Values.GetElementPtr(Index),Bytes,C,Depth+1)) return false;Ordered.Add(MoveTemp(Bytes));}
            Ordered.Sort([](const auto& L,const auto& R){return Less(L,R);});
            for (auto& Bytes:Ordered) A.Bytes(Bytes,1024*1024);
        }
        else
        {
            Values.EmptyElements();TArray<uint8> Previous;
            for (uint32 Index=0;Index<Count;++Index)
            {
                TArray<uint8> Bytes;A.Bytes(Bytes,1024*1024);
                if (!A.Good() || (Index && !Less(Previous,Bytes))) {A.Reject();return false;}
                const int32 Slot=Values.AddDefaultValue_Invalid_NeedsRehash();FGuLiHybridStateArchive Read(Bytes,1024*1024);
                if (!Field(Read,Set->ElementProp,Values.GetElementPtr(Slot),C,Depth+1) || !Read.Done()) {A.Reject();return false;}
                for (int32 Earlier=0;Earlier<Slot;++Earlier) if (Values.IsValidIndex(Earlier)
                    && Set->ElementProp->Identical(Values.GetElementPtr(Earlier),Values.GetElementPtr(Slot))) {A.Reject();return false;}
                Previous=MoveTemp(Bytes);
            }
            Values.Rehash();
        }
        return A.Good();
    }
    if (const auto* Map=CastField<FMapProperty>(P))
    {
        FScriptMapHelper Values(Map,Data);uint32 Count=Values.Num();A.Value(Count);
        if (!A.Good() || Count>MaximumItems || C.Values+Count*2>MaximumValues
            || !Allocation(A,C,uint64(Count)*(Map->KeyProp->GetElementSize()+Map->ValueProp->GetElementSize()+32))) {A.Reject();return false;}
        if (!A.IsReading())
        {
            struct FPair {TArray<uint8> Key;int32 Index=0;};TArray<FPair> Ordered;
            for (int32 Index=0;Index<Values.GetMaxIndex();++Index) if (Values.IsValidIndex(Index))
            {FPair Pair;Pair.Index=Index;if (!Encoded(Map->KeyProp,Values.GetKeyPtr(Index),Pair.Key,C,Depth+1)) return false;Ordered.Add(MoveTemp(Pair));}
            Ordered.Sort([](const auto& L,const auto& R){return Less(L.Key,R.Key);});
            for (auto& Pair:Ordered) {A.Bytes(Pair.Key,1024*1024);if (!Field(A,Map->ValueProp,Values.GetValuePtr(Pair.Index),C,Depth+1)) return false;}
        }
        else
        {
            Values.EmptyValues();TArray<uint8> Previous;
            for (uint32 Index=0;Index<Count;++Index)
            {
                TArray<uint8> Bytes;A.Bytes(Bytes,1024*1024);
                if (!A.Good() || (Index && !Less(Previous,Bytes))) {A.Reject();return false;}
                const int32 Slot=Values.AddDefaultValue_Invalid_NeedsRehash();FGuLiHybridStateArchive Read(Bytes,1024*1024);
                if (!Field(Read,Map->KeyProp,Values.GetKeyPtr(Slot),C,Depth+1) || !Read.Done()) {A.Reject();return false;}
                for (int32 Earlier=0;Earlier<Slot;++Earlier) if (Values.IsValidIndex(Earlier)
                    && Map->KeyProp->Identical(Values.GetKeyPtr(Earlier),Values.GetKeyPtr(Slot))) {A.Reject();return false;}
                if (!Field(A,Map->ValueProp,Values.GetValuePtr(Slot),C,Depth+1)) return false;Previous=MoveTemp(Bytes);
            }
            Values.Rehash();
        }
        return A.Good();
    }
    A.Reject();return false;
}
}
bool FGuLiHybridReflectedState::Value(FGuLiHybridStateArchive& A,const UScriptStruct* Type,void* Data)
{
    uint32 Version=Schema;A.Value(Version);FContext Context;
    if (Version!=Schema) {A.Reject();return false;}
    return Structure(A,Type,Data,Context,0);
}
