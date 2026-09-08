#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GuLiMapAuthoringSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings,DefaultConfig,meta=(DisplayName="GuLi Map Authoring"))
class GULIMAPAUTHORINGEDITOR_API UGuLiMapAuthoringSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UGuLiMapAuthoringSettings();
    UPROPERTY(Config,EditAnywhere,Category="Types",meta=(LongPackageName)) FString TypeAssetPath=TEXT("/Game/GuLiStrike/Editor/MapAuthoring/Types");
    UPROPERTY(Config,EditAnywhere,Category="Placement",meta=(Units="cm")) double WorkPlaneZ=0;
    UPROPERTY(Config,EditAnywhere,Category="Placement") bool bSurfacePlacement=true;
};
