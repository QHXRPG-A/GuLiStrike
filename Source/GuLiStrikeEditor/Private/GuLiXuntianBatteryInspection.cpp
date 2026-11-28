#include "GuLiHybridAuthoringLibrary.h"
#include "Editor.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiXuntianBellyBattery.h"
#include "Gameplay/Ship/UI/GuLiXuntianBellyChoice.h"
#include "Gameplay/Ship/UI/GuLiXuntianHUDWidget.h"
#include "Gameplay/Ship/UI/GuLiXuntianSceneGuide.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Children.h"
#include "Gameplay/CombatEffects/GuLiProjectileFlightPresentationProfile.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialShared.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "ShaderCore.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString InspectionJson(const TSharedRef<FJsonObject>& Object)
{
    FString Json;
    FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Json));
    return Json;
}

TSharedPtr<SWidget> FindUpgradeButton(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetTag()==TEXT("XuntianBellyUpgrade")) return Widget;
    FChildren* Children=Widget->GetChildren();
    for (int32 Index=0;Index<Children->Num();++Index)
        if (auto Button=FindUpgradeButton(Children->GetChildAt(Index))) return Button;
    return nullptr;
}

bool ClickUpgradeButton(UGuLiXuntianBellyChoice* Menu)
{
    if (!Menu || !Menu->IsInViewport() || !Menu->GetCachedWidget()) return false;
    const auto Button=FindUpgradeButton(Menu->GetCachedWidget().ToSharedRef());
    if (!Button || !Button->IsEnabled() || !Button->GetVisibility().IsVisible()) return false;
    auto& Slate=FSlateApplication::Get();
    const FVector2D Position=Button->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2f(.5f,.5f));
    const FWidgetPath Path=Slate.LocateWindowUnderMouse(Position,Slate.GetInteractiveTopLevelWindows());
    if (!Path.ContainsWidget(Button.Get())) return false;
    // A docked PIE can be in the background during automation. SButton requires capture
    // between down and up, even when the two events hit the same widget.
    const bool AllowBackgroundInput=Slate.GetHandleDeviceInputWhenApplicationNotActive();
    Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
    ON_SCOPE_EXIT { Slate.SetHandleDeviceInputWhenApplicationNotActive(AllowBackgroundInput); };
    const TSet<FKey> Pressed{EKeys::LeftMouseButton}, Released;
    const FPointerEvent Hover(FSlateApplication::CursorPointerIndex,Position,Slate.GetCursorPos(),Released,EKeys::Invalid,0,FModifierKeysState());
    Slate.RoutePointerMoveEvent(Path,Hover,false);
    if (!Button->IsHovered()) return false;
    const auto Event=[&](const TSet<FKey>& Keys)
    {
        return FPointerEvent(FSlateApplication::CursorPointerIndex,Position,Position,Keys,EKeys::LeftMouseButton,0,FModifierKeysState());
    };
    const FReply Down=Slate.RoutePointerDownEvent(Path,Event(Pressed));
    const bool Captured=Button->HasMouseCapture();
    const FReply Up=Slate.RoutePointerUpEvent(Path,Event(Released));
    return Captured && Down.GetHandler()==Button && Up.GetHandler()==Button;
}
}

FString UGuLiHybridAuthoringLibrary::InspectXuntianMarkerMaterial(UMaterialInterface* Material)
{
    auto Result=MakeShared<FJsonObject>();
    UWorld* World=GEditor ? GEditor->PlayWorld.Get() : nullptr;
    if (!Material && World)
    {
        const auto* PC=World->GetFirstPlayerController();
        const APawn* Pawn=PC ? PC->GetPawn() : nullptr;
        const auto* Guide=Pawn ? Pawn->FindComponentByClass<UGuLiXuntianSceneGuide>() : nullptr;
        if (Guide && Guide->IsVisible() && Guide->IsRegistered())
        {
            Material=Guide->MarkerInk;
            Result->SetStringField(TEXT("guide"),Guide->GetPathName());
        }
    }
    Result->SetStringField(TEXT("material"),GetPathNameSafe(Material));
    auto* Resource=Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
    bool UsesOwnShader=false;
    if (Resource && Material->GetRenderProxy() && !GUsingNullRHI)
    {
        // A cold editor can defer shader jobs until the first draw requests them.
        Resource->FinishCacheShaders();
        Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
        Resource->FinishCompilation();
        Result->SetStringField(TEXT("errors"),FString::Join(Resource->GetCompileErrors(),TEXT("\n")));
        Result->SetBoolField(TEXT("shader_map"),Resource->GetGameThreadShaderMap()!=nullptr);
        const auto FeatureLevel=World ? World->GetFeatureLevel() : GMaxRHIFeatureLevel;
        ENQUEUE_RENDER_COMMAND(GuLiCheckMarkerShader)([Proxy=Material->GetRenderProxy(),Resource,FeatureLevel,&UsesOwnShader](FRHICommandListImmediate&)
        {
            const FMaterialRenderProxy* Fallback=nullptr;
            const FMaterial& Selected=Proxy->GetMaterialWithFallback(FeatureLevel,Fallback);
            UsesOwnShader=!Fallback && &Selected==Resource;
        });
        FlushRenderingCommands(); // Explicit editor check after compilation, never used by Draw.
    }
    Result->SetBoolField(TEXT("own_render_shader"),UsesOwnShader);
    Result->SetBoolField(TEXT("ready"),UsesOwnShader && Resource->IsCompilationFinished()
        && Resource->GetGameThreadShaderMap() && Resource->GetCompileErrors().IsEmpty());
    return InspectionJson(Result);
}

FString UGuLiHybridAuthoringLibrary::InspectXuntianBattery(FName Action)
{
    auto Result = MakeShared<FJsonObject>();
    UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
    auto* PC = World ? World->GetFirstPlayerController() : nullptr;
    auto* Ship = PC ? Cast<AGuLiStrikeShip>(PC->GetPawn()) : nullptr;
    auto* Gun = Ship && Ship->IsXuntian() ? Ship->GetXuntianGunnery() : nullptr;
    Result->SetBoolField(TEXT("ready"), Gun && Ship->IsShipReady());
    if (Action==TEXT("click_upgrade"))
    {
        Result->SetBoolField(TEXT("clicked"),Ship && ClickUpgradeButton(Ship->BellyChoice));
        return InspectionJson(Result);
    }
    if (Action==TEXT("marker_states"))
    {
        const auto* Guide=Ship ? Ship->FindComponentByClass<UGuLiXuntianSceneGuide>() : nullptr;
        if (Guide)
        {
            auto States=MakeShared<FJsonObject>();
            for (const auto& [Key,Track]:Guide->AirContacts)
            {
                if (Track.PendingSince<0) continue;
                const FString Id=FString::Printf(TEXT("%d/%s/%u/%u"),int32(Key.Kind),*Key.AuthorityId.ToString(EGuidFormats::Digits),Key.Generation,Key.LocalId);
                States->SetNumberField(Id,int32(Track.Badge));
            }
            Result->SetObjectField(TEXT("states"),States);
            Result->SetNumberField(TEXT("submitted_markers"),Guide->AirMarkers.Num());
            Result->SetNumberField(TEXT("frame"),double(GFrameCounter));
        }
        return InspectionJson(Result);
    }
    if (Gun && Action==TEXT("build"))
    {
        const auto* Assembly = Ship->GetShipAssembly();
        const auto& Applied = Assembly->GetAppliedBuild();
        const auto* Player = Ship->GetPlayerState<AGuLiBattlePlayerState>();
        const auto* Binding = UGuLiHybridActorComponent::Find(Ship);
        auto* Hybrid = World->GetSubsystem<UGuLiHybridWorldSubsystem>();
        auto Names = [](const TArray<FName>& Values)
        {
            TArray<TSharedPtr<FJsonValue>> Array;
            for (const FName Value : Values) Array.Add(MakeShared<FJsonValueString>(Value.ToString()));
            return Array;
        };
        Result->SetArrayField(TEXT("installed_nodes"), Names(Applied.ChosenNodeIds));
        Result->SetNumberField(TEXT("assembly_revision"), Applied.BuildRevision);
        Result->SetBoolField(TEXT("available"), Assembly->IsAvailableForChoice());
        Result->SetBoolField(TEXT("control"), Binding && Binding->CanControl());
        Result->SetBoolField(TEXT("simulation"), Binding && Binding->CanSimulate());
        Result->SetBoolField(TEXT("publish"), Hybrid && Hybrid->CanPublishFacts());
        const auto* Hangar = Ship->GetHangarCapability();
        Result->SetBoolField(TEXT("hangar_enabled"), Hangar && Hangar->IsCapabilityEnabled());
        if (Player)
        {
            auto* Build = Player->GetShipBuild();
            Result->SetArrayField(TEXT("player_nodes"), Names(Build->GetBuildState().ChosenNodeIds));
            auto Choices = MakeShared<FJsonObject>();
            for (const auto& Choice : Build->QueryChoices())
                Choices->SetStringField(Choice.NodeId.ToString(), Choice.bAvailable ? FString(TEXT("available")) : Choice.Reason);
            Result->SetObjectField(TEXT("choices"), Choices);
        }
        FGuLiHybridObjectState Object;
        if (Binding && Hybrid && Hybrid->GetObject(Binding->GetObjectKey(), Object))
            if (const auto* Selection = Object.Upgrades.Find(Assembly->UpgradeDomain))
                Result->SetArrayField(TEXT("ledger_nodes"), Names(Selection->Chosen));
    }
    if (Gun && Action==TEXT("hud"))
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UGuLiXuntianHUDWidget::StaticClass(),false);
        for (auto* Widget:Widgets)
        {
            if (Widget->GetOwningPlayer()!=PC || !Widget->IsInViewport()) continue;
            const auto& Paint=CastChecked<UGuLiXuntianHUDWidget>(Widget)->RequestPaintInspection();
            Result->SetNumberField(TEXT("frame"),double(Paint.Frame));
            Result->SetNumberField(TEXT("warning_strokes"),Paint.WarningStrokes);
            Result->SetBoolField(TEXT("ground"),Paint.bGround);
            Result->SetNumberField(TEXT("width"),Paint.DesignSize.X);
            Result->SetNumberField(TEXT("height"),Paint.DesignSize.Y);
            Result->SetNumberField(TEXT("hull_x"),Paint.HullBarOrigin.X);
            Result->SetNumberField(TEXT("hull_y"),Paint.HullBarOrigin.Y);
            TArray<TSharedPtr<FJsonValue>> Soldiers;
            for (const auto& Bar:Paint.Soldiers)
            {
                auto Row=MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("id"),Bar.SoldierId);
                Row->SetNumberField(TEXT("hp"),Bar.Health);
                Row->SetBoolField(TEXT("has_shield"),Bar.bHasShield);
                Row->SetNumberField(TEXT("shield_fraction"),Bar.ShieldFraction);
                Row->SetBoolField(TEXT("vital_change"),Bar.bVitalChange);
                Row->SetNumberField(TEXT("x"),Bar.Origin.X);
                Row->SetNumberField(TEXT("y"),Bar.Origin.Y);
                Row->SetNumberField(TEXT("width"),Bar.Size.X);
                Row->SetNumberField(TEXT("height"),Bar.Size.Y);
                Row->SetNumberField(TEXT("camera_depth_cm"),Bar.Depth);
                Row->SetNumberField(TEXT("horizontal_cm"),Bar.HorizontalCm);
                Soldiers.Add(MakeShared<FJsonValueObject>(Row));
            }
            Result->SetArrayField(TEXT("soldiers"),MoveTemp(Soldiers));
            break;
        }
    }
    else if (Gun)
    {
        if (Action == TEXT("press")) Gun->SetTrigger(true);
        if (Action == TEXT("release")) Gun->SetTrigger(false);
        if (Action == TEXT("open")) Ship->ToggleBellyChoice();
        if (Action == TEXT("confirm") && Ship->BellyChoice && Ship->BellyChoice->IsInViewport()) Ship->BellyChoice->ConfirmUpgrade();
        if (Action == TEXT("ground")) Gun->Orbit(FVector2D(0,-1500));
        if (Action == TEXT("low") || Action == TEXT("inside") || Action == TEXT("high"))
        {
            const FName StationTag(*(FString(TEXT("GuLi.XuntianHUD."))+Action.ToString()));
            for (TActorIterator<ATargetPoint> It(World); It; ++It)
                if (It->ActorHasTag(StationTag))
                {
                    for (int32 Step=0; Step<14; ++Step)
                    {
                        FMinimalViewInfo View; Gun->GetCameraView(View);
                        const FRotator Delta = ((It->GetActorLocation()-View.Location).Rotation()-View.Rotation).GetNormalized();
                        Gun->Orbit(FVector2D(Delta.Yaw,Delta.Pitch) / FMath::RadiansToDegrees(.0024));
                    }
                    break;
                }
        }
        Result->SetNumberField(TEXT("tier"),Gun->GetBellyLevel());
        Result->SetBoolField(TEXT("modal"), Ship->BellyChoice && Ship->BellyChoice->IsInViewport());
        Result->SetNumberField(TEXT("coverage"),Gun->CoverageAt(Gun->AimPoint));
        Result->SetBoolField(TEXT("ground_bank"),Gun->bGround);
        Result->SetBoolField(TEXT("terrain_aim"),Gun->bAimingAtSurface);
        Result->SetStringField(TEXT("aim"),Gun->AimPoint.ToString());
        TArray<TSharedPtr<FJsonValue>> Sockets;
        for (const auto& Mount : Gun->GetMounts()) if (Mount.bGround) Sockets.Add(MakeShared<FJsonValueString>(Mount.Socket.ToString()));
        Result->SetArrayField(TEXT("sockets"),Sockets);
        if (auto* Feedback = World->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
            Result->SetNumberField(TEXT("pending_compiles"),Feedback->GetPendingExplosionCompileCount());
        if (auto* Reg = World->GetSubsystem<UGuLiVfxRegistrySubsystem>())
        {
            TArray<TSharedPtr<FJsonValue>> Ready;
            for (const auto& Ref : GetDefault<UGuLiCombatEffectSettings>()->StartupProjectiles)
            {
                const auto* Shell = Cast<UGuLiXuntianShellDefinition>(Ref.Get());
                auto* Profile = Shell ? Shell->FlightPresentationProfile.Get() : nullptr;
                auto* Field = Shell ? Shell->ImpactField.Get() : nullptr;
                auto* Ring = Shell ? Shell->GroundWarningStyle.Get() : nullptr;
                auto* Blast = Field && !Field->ActivationVariants.IsEmpty() ? GuLiVfx::Load<UNiagaraSystem>(World,Field->ActivationVariants[0].VfxId,false) : nullptr;
                const bool Valid = Profile && Profile->BatchSystem.IsValid() && Profile->WorldSmoke.IsValid() &&
                    (Shell->Level!=3 || Profile->HeadGlow.IsValid()) && Blast && Ring &&
                    GuLiVfx::Load<UMaterialInterface>(World,Ring->MaterialVfxId,false) &&
                    GuLiVfx::Load<UNiagaraSystem>(World,Shell->FlightVfxId,false);
                Ready.Add(MakeShared<FJsonValueBoolean>(Valid));
            }
            Result->SetArrayField(TEXT("tiers_resident"),Ready);
        }
        Result->SetNumberField(TEXT("world_seconds"),World->GetTimeSeconds());
        if (const auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>())
            Result->SetNumberField(TEXT("battle_seconds"),Hybrid->GetLocalLogicalSeconds());
        if (const auto* Warning = World->GetSubsystem<UGuLiGroundWarningSubsystem>())
            Result->SetNumberField(TEXT("warnings"),Warning->GetActiveWarningCount());
        if (const auto* Visuals = World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
            Result->SetNumberField(TEXT("bursts"),Visuals->GetCounters().BurstsPlayed);
        if (const auto* Runtime = World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>())
        {
            TArray<FGuLiCombatEffectState> States; Runtime->BuildActiveSnapshot(States,false);
            TArray<TSharedPtr<FJsonValue>> Samples;
            for (const auto& S : States)
            {
                const auto* Field = S.FieldDefinition.Get();
                if (!Field || !Field->ConfigId.ToString().StartsWith(TEXT("XuntianBelly"))) continue;
                auto E = MakeShared<FJsonObject>();
                E->SetStringField(TEXT("id"),S.EffectId.ToString()); E->SetStringField(TEXT("field"),Field->ConfigId.ToString());
                E->SetBoolField(TEXT("projectile"),S.Kind == EGuLiCombatEffectKind::Projectile);
                E->SetNumberField(TEXT("radius"),S.Radius); E->SetNumberField(TEXT("speed"),S.Motion.Speed);
                E->SetNumberField(TEXT("time"),S.StartTime); E->SetBoolField(TEXT("ground_warning"),S.bHasWarningGroundPoint);
                Samples.Add(MakeShared<FJsonValueObject>(E));
            }
            Result->SetArrayField(TEXT("effects"),Samples);
            Result->SetNumberField(TEXT("commits"),Runtime->GetCounters().DamageCommits);
        }
        auto Targets=MakeShared<FJsonObject>();
        for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
            if (*It != Ship && It->GetCombatHealthComponent()) Targets->SetNumberField(It->GetName(),It->GetCombatHealthComponent()->GetHealthState().Health);
        Result->SetObjectField(TEXT("other_ship_health"),Targets);
    }
    return InspectionJson(Result);
}
