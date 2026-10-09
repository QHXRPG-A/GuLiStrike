#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Gameplay/Data/Generated/GuLiStrikeModelsTableRows.h"
#include "GuLiLocalTeamColorSubsystem.generated.h"

class UMeshComponent;
class AGuLiBattlePlayerState;

/** View-local cosmetics only; no material asset/MPC/replicated team is modified. */
UCLASS()
class GULISTRIKE_API UGuLiLocalTeamColorSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	UFUNCTION(BlueprintCallable, Category="Models|Team Colors")
	void RegisterModelComponent(UMeshComponent* Component, int32 ModelId, const FString& PartKey = TEXT("Root"), EGuLiTeam ActualTeam = EGuLiTeam::Unassigned, bool ReviewCandidate = false);
	UFUNCTION(BlueprintCallable, Category="Models|Team Colors") void UnregisterModelComponent(UMeshComponent* Component);
	UFUNCTION(BlueprintCallable, Category="Models|Team Colors") void ApplyLocalTeamColors();
	static void Register(const UObject* Context, UMeshComponent* Component, int32 ModelId, const FString& PartKey, EGuLiTeam Team = EGuLiTeam::Unassigned);
private:
	UFUNCTION() void HandleIdentityChanged();
	struct FEntry
	{
		TWeakObjectPtr<UMeshComponent> Component;
		int32 ModelId = 0;
		FString PartKey;
		EGuLiTeam ExplicitTeam = EGuLiTeam::Unassigned, LastTeam = EGuLiTeam::Unassigned;
		uint32 MaterialHash = 0, PrimitiveDataHash = 0;
		bool bCandidate = false, bDirty = true;
		FGuLiStrikeModelsModelsRow Definition;
		TArray<FGuLiStrikeModelsMaterialParametersRow> Bindings;
	};
	void Refresh(FEntry& Entry, EGuLiTeam ViewTeam);
	TMap<TWeakObjectPtr<UMeshComponent>, FEntry> Entries;
	TWeakObjectPtr<AGuLiBattlePlayerState> Identity;
	TWeakObjectPtr<UWorld> BoundWorld;
	float RefreshRemaining = 0;
	float DiscoveryRemaining = 0;
	EGuLiTeam LastViewTeam = EGuLiTeam::Unassigned;
};
