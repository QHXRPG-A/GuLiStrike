#pragma once
#include "CoreMinimal.h"
#include "PrimitiveComponentId.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiCommanderOverviewSubsystem.generated.h"
class UPrimitiveComponent;

/** Local render registry; simulation, replication and UI components remain independent. */
UCLASS()
class GULISTRIKE_API UGuLiCommanderOverviewSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void RegisterVisual(UPrimitiveComponent* Component);
	static void ForgetVisual(UPrimitiveComponent* Component);
	void GatherHiddenComponents(TSet<FPrimitiveComponentId>& Out);
	const TArray<TWeakObjectPtr<AActor>>& GetActors() const { return Actors; }
	static bool IsUnitOrBuilding(const AActor* Actor);
private:
	void ActorSpawned(AActor* Actor);
	TArray<TWeakObjectPtr<AActor>> Actors;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Visuals;
	FDelegateHandle SpawnHandle;
};
