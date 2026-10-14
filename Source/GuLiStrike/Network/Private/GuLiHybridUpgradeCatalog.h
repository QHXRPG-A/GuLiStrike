#pragma once
#include "Network/GuLiHybridManifest.h"
#include "GuLiNetUpgrade.h"

namespace GuLiHybridUpgradeCatalog
{
inline bool Append(FGuLiHybridManifestData& Data,const FString& Domain,const GuLiNet::UpgradeCatalog& Catalog,FString& Error)
{
    GuLiNet::CompiledUpgradeCatalog Compiled;std::string Why;
    if (!GuLiNet::CompileUpgradeCatalog(Catalog,Compiled,Why)) {Error=UTF8_TO_TCHAR(Why.c_str());return false;}
    const auto Bytes=GuLiNet::EncodeUpgradeCatalog(Catalog);
    const FString Base=TEXT("upgrade_catalog/")+Domain+TEXT("/");
    if (Bytes.empty() || Data.Numbers.Contains(Base+TEXT("schema"))) {Error=TEXT("Empty or duplicate upgrade catalog: ")+Domain;return false;}
    Data.Numbers.Add(Base+TEXT("schema"),GuLiNet::UpgradeCatalogSchema);Data.Numbers.Add(Base+TEXT("revision"),Catalog.Version);
    Data.Numbers.Add(Base+TEXT("chunks"),(Bytes.size()+2047)/2048);
    constexpr TCHAR Digits[]=TEXT("0123456789abcdef");
    for (size_t At=0;At<Bytes.size();At+=2048)
    {
        FString Hex;Hex.Reserve(4096);
        for (size_t I=At;I<Bytes.size() && I<At+2048;++I) {Hex.AppendChar(Digits[Bytes[I]>>4]);Hex.AppendChar(Digits[Bytes[I]&15]);}
        Data.Strings.Add(Base+TEXT("chunk/")+FString::FromInt(int32(At/2048)),MoveTemp(Hex));
    }
    return true;
}
inline void Initial(FGuLiHybridManifestData& Data,const FString& Definition,const FString& Domain,const TArray<FString>& Nodes)
{
    const FString Base=TEXT("initial_upgrade/")+Domain+TEXT("/")+Definition+TEXT("/");
    Data.Numbers.Add(Base+TEXT("count"),Nodes.Num());
    for (int32 I=0;I<Nodes.Num();++I) Data.Strings.Add(Base+FString::FromInt(I),Nodes[I].ToLower());
}
}
