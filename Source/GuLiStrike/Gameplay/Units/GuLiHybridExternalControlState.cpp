#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/GuLiHybridManifest.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
namespace
{
bool Archive(FGuLiHybridStateArchive& A,FGuLiExternalUnitControlState& S,FGuLiExternalControlRestoreSettings& Settings)
{
    uint32 V=1;A.Value(V);if (V!=1) A.Reject();A.Value(S.OwnerToken);A.Value(S.bPhased);A.Value(S.bActionsLocked);A.Value(S.Baseline);
    A.Value(S.Revision);A.Value(S.DisplacementRevision);A.Value(S.RestoreMovementMode);
    A.Value(Settings.Captured);A.Value(Settings.Collision);A.Value(Settings.CanBeDamaged);
    FString Material=S.PhaseMaterial?S.PhaseMaterial->GetPathName():FString();A.String(Material);
    if (A.IsReading()) {S.PhaseMaterial=Material.IsEmpty()?nullptr:LoadObject<UMaterialInterface>(nullptr,*Material);if (!Material.IsEmpty()&&!S.PhaseMaterial) A.Reject();}
    if (S.RestoreMovementMode>=MOVE_MAX || ((S.bActionsLocked||S.bPhased)&&(!S.OwnerToken.IsValid()||!Settings.Captured))) A.Reject();return A.Good();
}
const FGuLiHybridComponentCapabilityRegistration External({TEXT("ExternalControl"),1,
    []()->const UClass* {return UGuLiExternalUnitControlComponent::StaticClass();},
    [](const UActorComponent& C,TArray<uint8>& Out){const auto* V=CastChecked<UGuLiExternalUnitControlComponent>(&C);auto S=V->GetState();auto Settings=V->CaptureRestoreSettings();FGuLiHybridStateArchive A;return Archive(A,S,Settings)&&A.Finish(Out);},
    [](const UActorComponent&,TConstArrayView<uint8> Bytes){FGuLiExternalUnitControlState S;FGuLiExternalControlRestoreSettings Settings;FGuLiHybridStateArchive A(Bytes);return Archive(A,S,Settings)&&A.Done();},
    [](UActorComponent& C,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {FGuLiExternalUnitControlState S;FGuLiExternalControlRestoreSettings Settings;FGuLiHybridStateArchive A(Bytes);if (!Archive(A,S,Settings)||!A.Done()) return false;if (Stage==EGuLiHybridRestoreStage::Link) CastChecked<UGuLiExternalUnitControlComponent>(&C)->RestoreHybridState(S,Settings);return true;},
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
    [](const UActorComponent& C,FGuLiHybridManifestData& M,const FString& D,FString&)
    {const auto* V=CastChecked<UGuLiExternalUnitControlComponent>(&C);M.Numbers.Add(TEXT("relocation/target/")+D,(V->bPlayerVehicle?1:0)|(V->bAirborne?2:0));return true;}});
}
