#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeVfxTableRows.h"
#include "Gameplay/Data/Generated/GuLiVfxIds.h"
#include "GuLiVfxRegistrySubsystem.generated.h"

class AActor;
UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiVfxRegistrySettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="VFX") TSoftObjectPtr<UDataTable> EffectsTable;
};

/** World-local metadata and strong resource cache. No cosmetic assets are loaded on dedicated servers. */
UCLASS()
class GULISTRIKE_API UGuLiVfxRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintCallable, Category="VFX")
	bool GetDefinition(int32 VfxId, FGuLiStrikeVfxEffectsRow& Definition);
	/** Set LoadIfNeeded=false after an asynchronous load to avoid introducing a synchronous load. */
	UFUNCTION(BlueprintCallable, Category="VFX", meta=(DeterminesOutputType="ExpectedClass"))
	UObject* LoadResource(int32 VfxId, TSubclassOf<UObject> ExpectedClass, bool LoadIfNeeded = true);
	/** Blueprint resources are registered by their generated _C path so they remain loadable after Cook. */
	UFUNCTION(BlueprintCallable, Category="VFX", meta=(DeterminesOutputType="ExpectedBaseClass"))
	TSubclassOf<AActor> LoadActorClass(int32 VfxId, TSubclassOf<AActor> ExpectedBaseClass);
	/** Authoring/static validation only; never creates a World or plays an effect. */
	UFUNCTION(BlueprintCallable, Category="VFX|Editor")
	static bool GetDefinitionWithoutWorld(int32 VfxId, FGuLiStrikeVfxEffectsRow& Definition);
	UFUNCTION(BlueprintPure, Category="VFX")
	static FVector ComposeScale(FVector BaseScale, FVector DynamicScale);
	static bool IsValidScale(const FVector& Scale);
	static bool SameDefinition(const FGuLiStrikeVfxEffectsRow& A, const FGuLiStrikeVfxEffectsRow& B);
	static bool ValidateDefinitions(const TArray<FGuLiStrikeVfxEffectsRow>& Rows, FString& Error);
	static bool MatchesType(const UObject* Resource, const UClass* ExpectedClass);
	static UObject* Resolve(const UObject* Context, int32 VfxId, UClass* ExpectedClass, bool LoadIfNeeded = true);
	static bool Query(const UObject* Context, int32 VfxId, FGuLiStrikeVfxEffectsRow& Definition);

private:
	bool EnsureCatalog();
	bool bCatalogAttempted = false;
	UPROPERTY(Transient) TObjectPtr<UDataTable> Catalog;
	UPROPERTY(Transient) TMap<int32, FGuLiStrikeVfxEffectsRow> Definitions;
	UPROPERTY(Transient) TMap<FSoftObjectPath, TObjectPtr<UObject>> Resources;
	TSet<int32> ReportedFailures;
};

namespace GuLiVfx
{
	template<class T> T* Load(const UObject* Context, int32 VfxId, bool LoadIfNeeded = true)
	{
		return Cast<T>(UGuLiVfxRegistrySubsystem::Resolve(Context, VfxId, T::StaticClass(), LoadIfNeeded));
	}
	GULISTRIKE_API FVector Scale(const UObject* Context, int32 VfxId, const FVector& DynamicScale = FVector::OneVector);
	GULISTRIKE_API FSoftObjectPath Path(const UObject* Context, int32 VfxId);
	template<class T> UClass* LoadClass(const UObject* Context, int32 VfxId)
	{
		UClass* Class = Load<UClass>(Context, VfxId);
		if (Class && !Class->IsChildOf(T::StaticClass()))
		{
			UE_LOG(LogTemp, Error, TEXT("VfxId %d resolves to an incompatible actor class."), VfxId);
			return nullptr;
		}
		return Class;
	}
}
