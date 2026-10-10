#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GuLiProjectileFlightPresentationProfile.generated.h"

class UNiagaraSystem;

/** Opt-in cosmetic configuration. No motion, collision, networking or weapon routing. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiProjectileFlightPresentationProfile final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UNiagaraSystem> BatchSystem;
	/** Frozen rendered transverse diameter from the legacy mesh/particle/registry/component scale chain. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation",meta=(Units="cm",ClampMin="0")) float LegacyCoreDiameter=0;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FLinearColor CoreColor=FLinearColor::FromSRGBColor(FColor(254,228,217));
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FLinearColor TrailColor=FLinearColor::FromSRGBColor(FColor(238,157,88));
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation",meta=(Units="cm",ClampMin="1000",ClampMax="5000")) float MaximumTrailLength=5000;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation",meta=(Units="s",ClampMin="0.01")) float PulsePeriod=.5f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation",meta=(Units="s",ClampMin="0.01")) float TrailFadeSeconds=.55f;
	bool IsValidProfile() const
	{
		return !BatchSystem.IsNull() && FMath::IsFinite(LegacyCoreDiameter) && LegacyCoreDiameter>0
			&& FMath::IsFinite(MaximumTrailLength) && MaximumTrailLength>=1000 && MaximumTrailLength<=5000
			&& FMath::IsFinite(PulsePeriod) && PulsePeriod>0 && FMath::IsFinite(TrailFadeSeconds) && TrailFadeSeconds>0;
	}
};
