#pragma once

#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/UI/GuLiSceneUITypes.h"

class AGuLiCommanderPresentationActor;
class APlayerController;
class UGuLiCommanderNetSyncComponent;
class UGuLiCombatHealthComponent;
class FGuLiHealthBarPoseCache;
struct FGuLiHealthBarEvent;

namespace GuLiHealthBars
{
	FVector ViewOrigin(const APlayerController& Viewer);
	bool IsNearby(const FVector& Origin, const FVector& Location);
	float DamageOpacity(double Now, double LastDamage);
	float Visibility(const FVector& Origin, const FVector& Location, float RecentDamageOpacity);
	void SetVitals(FGuLiSceneUIHealthBar& Bar, float Health, float MaxHealth, float Shield, float MaxShield);
	FGuLiSceneUIHealthBar FromEvent(const UObject* Source, uint64 Element, const FGuLiHealthBarEvent& Event, double Now);
}

/** Per-view model sources and selection for world health markers. */
struct FGuLiWorldHealthBars
{
	TArray<TWeakObjectPtr<UGuLiCombatHealthComponent>> ActorHealthSources;
	void Gather(APlayerController& Viewer, const AGuLiCommanderPresentationActor* Presentation,
		FGuLiHealthBarPoseCache& Poses, TArray<FGuLiSceneUIHealthBar>& Bars);
	bool ReadSoldierModel(const AGuLiCommanderPresentationActor& Presentation, FGuLiSoldierId Id, uint16 Type, FGuLiHealthBarModel& Model);
	const FBox& SoldierBounds(const AGuLiCommanderPresentationActor& Presentation, uint16 Type);
	static bool ReadActorModel(const AActor* Actor, FGuLiHealthBarModel& Model);

private:
	TWeakObjectPtr<const UGuLiCommanderNetSyncComponent> SelectionSource;
	uint32 SelectionRevision = 0;
	TSet<FGuLiSoldierId> SelectedSoldiers;
	TWeakObjectPtr<const AGuLiCommanderPresentationActor> BoundsOwner;
	TMap<uint16, FBox> ModelBounds;
};
