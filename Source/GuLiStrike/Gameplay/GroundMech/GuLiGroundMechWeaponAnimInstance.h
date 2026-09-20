#pragma once

#include "Animation/AnimInstance.h"
#include "GuLiGroundMechWeaponAnimInstance.generated.h"

class UCurveFloat;

UCLASS(Transient, Blueprintable)
class GULISTRIKE_API UGuLiGroundMechWeaponAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	void Configure(UCurveFloat* Curve, FName Bone, float TargetZ, float Duration);
	void TriggerRecoil(float AgeSeconds = 0);
	UPROPERTY(BlueprintReadOnly, Category="Recoil") float RecoilAlpha = 0;
	UPROPERTY(BlueprintReadOnly, Category="Recoil") float RecoilOffset = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil") TObjectPtr<UCurveFloat> RecoilCurve;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil") FName RecoilBone = TEXT("Barrel_big");
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil") float RecoilTargetZ = 152;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Recoil") float RecoilDuration = .15f;
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
private:
	float Age = 100;
	float StartAlpha = 0;
	float RestZ = 0;
};
