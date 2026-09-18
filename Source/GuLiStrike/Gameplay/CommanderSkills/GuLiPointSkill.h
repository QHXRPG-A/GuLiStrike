#pragma once

#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "GuLiPointSkill.generated.h"

struct FGuLiCombatAttackRequest;

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiPointSkillConfiguration : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UGuLiProjectileEffectDefinition> Projectile;
	/** Single source of damage and effect timing is the existing SpellFields row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName FieldConfigId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector LaunchOffset = FVector(0, 0, 20);
	/** Reuse the source unit's resolved weapon damage, field identity and calibrated muzzles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SourceWeaponSlot;
	/** Diameter of the server-sampled ground impact area; zero retains a point target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="cm")) float TargetAreaDiameterCentimeters = 0;
	/** Other callers retain the existing straight point-projectile behavior by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUseAuthoredTrajectory = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UGuLiGroundWarningStyle> GroundWarningStyle;
};

UCLASS()
class GULISTRIKE_API UGuLiPointSkillExecutor : public UGuLiCommanderSkillExecutor
{
	GENERATED_BODY()
public:
	virtual bool ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const override;
	virtual FGuLiActiveSkillExecutionResult Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const override;
protected:
	/** A concrete skill resolves its payload; the shared adapter launches and reports success. */
	virtual bool ConfigurePayload(const FGuLiActiveSkillExecutionContext& Context, const UGuLiPointSkillConfiguration& Configuration,
		FGuLiCombatAttackRequest& Request, FString& Error) const;
};

