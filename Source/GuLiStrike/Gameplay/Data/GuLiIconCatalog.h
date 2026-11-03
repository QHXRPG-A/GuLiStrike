#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GuLiIconCatalog.generated.h"
class UTexture2D;
class UDataTable;

/** Client-owned prepared icons. Find never loads resources or changes gameplay. */
UCLASS()
class GULISTRIKE_API UGuLiIconCatalog final : public UObject
{
    GENERATED_BODY()
public:
    bool Prepare(const TSet<int32>& Ids, const UWorld* World);
    UTexture2D* Find(int32 Id) const;
    void Release();
    int32 NumLoaded() const { return Textures.Num(); }
private:
    UPROPERTY(Transient) TObjectPtr<UDataTable> Table;
    UPROPERTY(Transient) TMap<int32,TObjectPtr<UTexture2D>> Textures;
};
