#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GuLiCommanderUITheme.generated.h"

class UTexture2D;
class UFont;

/** Project-owned references; vendor assets remain immutable and have no gameplay ownership. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiCommanderUITheme : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Commander|UI") TMap<FName, TObjectPtr<UTexture2D>> Icons;
	UPROPERTY(EditAnywhere, Category="Commander|UI") TMap<int32, TObjectPtr<UTexture2D>> Portraits;
	UPROPERTY(EditAnywhere, Category="Commander|UI") TObjectPtr<UTexture2D> Panel;
	UPROPERTY(EditAnywhere, Category="Commander|UI") TObjectPtr<UTexture2D> Button;
	UPROPERTY(EditAnywhere, Category="Commander|UI") TObjectPtr<UTexture2D> Bar;
	UPROPERTY(EditAnywhere, Category="Commander|UI") TObjectPtr<UTexture2D> ScrollThumb;
	UPROPERTY(EditAnywhere, Category="Commander|UI") TObjectPtr<UFont> NumericFont;
	UPROPERTY(EditAnywhere, Category="Commander|UI") FLinearColor Accent = FLinearColor(.08f,.72f,.86f);
	UTexture2D* FindIcon(FName Key) const;
	UTexture2D* FindPortrait(int32 UnitType) const;
};
