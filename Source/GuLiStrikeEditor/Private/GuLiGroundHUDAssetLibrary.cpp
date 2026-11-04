#include "GuLiGroundHUDAssetLibrary.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDWidget.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkComponent.h"
#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGroundMechRocketComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/PlayerController.h"
#include "Editor.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/FontCache.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

FString UGuLiGroundHUDAssetLibrary::ReadMotionState(UGuLiGroundHUDWidget* HUD)
{return HUD?HUD->GetMotionDiagnostics():TEXT("{\"error\":\"No Ground HUD\"}");}

bool UGuLiGroundHUDAssetLibrary::MotionProbeAction(APlayerController* PC,FName Action)
{
    auto* P=PC?Cast<AGuLiGroundMechCharacter>(PC->GetPawn()):nullptr;
    if(!P||!PC->IsLocalController()||!PC->GetWorld()->IsPlayInEditor())return false;
    if(Action==TEXT("AimOn")||Action==TEXT("AimOff")){P->SetShoulderZoomHeld(Action==TEXT("AimOn"));return true;}
    if(Action==TEXT("InterruptBlink")){P->GetBlinkComponent()->Interrupt();return true;}
    if(Action==TEXT("PrepareNearEmptyFuel")&&UGuLiHybridActorComponent::HasSimulation(P))
    {
        P->GetRocketJump()->FinishMovement(.5f,false,false);
        CastChecked<UGuLiGroundMechMovementComponent>(P->GetCharacterMovement())->InitializeRocketFuel(.5f);
        return FMath::IsNearlyEqual(P->GetRocketJump()->GetFuel(),.5f);
    }
    return false;
}
FString UGuLiGroundHUDAssetLibrary::ReadMotionTargets(APlayerController* PC)
{
    if(!PC||!PC->GetPawn()||!PC->GetWorld()->IsPlayInEditor())return TEXT("{}");
    auto J=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Items;
    TArray<FGuLiHybridObjectState> Objects;PC->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjects(Objects);
    for(const auto& O:Objects)
    {
        if(O.Key.Kind!=EGuLiHybridObjectKind::Soldier||FVector::DistSquared2D(O.Location,FVector(-5000,53000,0))>10000.*10000.)continue;
        auto S=MakeShared<FJsonObject>();S->SetStringField(TEXT("id"),O.Key.StableId.ToString());
        S->SetNumberField(TEXT("team"),int32(O.Affiliation));S->SetNumberField(TEXT("health"),O.Health.Health);S->SetNumberField(TEXT("shield"),O.Health.Shield);
        S->SetBoolField(TEXT("dead"),O.Health.bDead);S->SetNumberField(TEXT("life"),O.LifeState);
        S->SetNumberField(TEXT("x"),O.Location.X);S->SetNumberField(TEXT("y"),O.Location.Y);S->SetNumberField(TEXT("z"),O.Location.Z);Items.Add(MakeShared<FJsonValueObject>(S));
    }
    J->SetArrayField(TEXT("targets"),Items);FString Result;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Result));return Result;
}

FString UGuLiGroundHUDAssetLibrary::ConfigureFont(UFont* Font, UFontFace* Medium, UFontFace* Bold)
{
    if (!GEditor || GEditor->PlayWorld) return TEXT("Stop PIE before authoring HUD font assets.");
    if (!Font || !Medium || !Bold) return TEXT("Font and both imported font faces are required.");
    Font->Modify();
    Font->FontCacheType = EFontCacheType::Runtime;
    auto& Composite = Font->GetMutableInternalCompositeFont();
    // Slate caches pointers into the typeface array. Invalidate those pointers
    // before replacing the array, including repeated imports in one session.
    if (FSlateApplication::IsInitialized())
    {
        const auto Cache = FSlateApplication::Get().GetRenderer()->GetFontCache();
        Cache->FlushCompositeFont(Composite);
        Cache->FlushObject(Font);
    }
    Composite = FCompositeFont();
    for (const FName Name : {FName(TEXT("Regular")), FName(TEXT("Medium")), FName(TEXT("Bold"))})
    {
        auto& Entry = Composite.DefaultTypeface.Fonts.AddDefaulted_GetRef();
        Entry.Name = Name;
        Entry.Font = FFontData(Name == TEXT("Bold") ? Bold : Medium);
    }
    Composite.MakeDirty();
    Font->PostEditChange();
    Font->MarkPackageDirty();
    return FString();
}
