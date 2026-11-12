#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "GuLiPioneerBarracksActor.generated.h"

class UChildActorComponent;

UCLASS()
class GULISTRIKE_API AGuLiPioneerBarracksActor final : public AGuLiPlacedBuilding
{
    GENERATED_BODY()
public:
    AGuLiPioneerBarracksActor();
    virtual bool InitializeFromDefinition(int32 InDefinitionId,EGuLiTeam InTeam,const FGuid& Builder,
        int32 Territory,EGuLiBuildingOrigin Origin,bool bCompleted) override;
    virtual void BeginPlay() override;
    FVector GetBirthLocation() const { return GetBuildingGroundLocation(); }
    FVector GetExitLocation(float UnitRadius) const;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UChildActorComponent> Presentation;
};
