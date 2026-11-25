#include "GuLiWingmanPlayRange.h"
#include "GuLiHybridAuthoringLibrary.h"
#include "GuLiNetManifest.h"
#include "LandscapeProxy.h"
#include "Gameplay/Data/Generated/GuLiStrikeShipTableRows.h"
#include "Gameplay/Performance/GuLiMovementCapture.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanTargeting.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Editor.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
using GuLiLocalPIERolePicker::ETrial;
const FName HangarNode(TEXT("08"));
constexpr int32 GroundTargetCount = 6;
constexpr int32 SectorCountPerAxis = 3;
constexpr int32 SoldiersPerSector = 4;

bool Reject(FString& Error, FString Reason)
{
    Error = MoveTemp(Reason);
    return false;
}

ATargetPoint* FindMarker(UWorld& World, const TCHAR* Suffix)
{
    const FName Tag(*(FString(TEXT("GuLi.WingmanRange.")) + Suffix));
    ATargetPoint* Found = nullptr;
    for (TActorIterator<ATargetPoint> It(&World); It; ++It)
    {
        if (!It->ActorHasTag(Tag)) continue;
        if (Found) return nullptr;
        Found = *It;
    }
    return Found;
}

FGuLiHybridStartupEntry* FindShip(FGuLiHybridManifestData& Manifest, EGuLiTeam Team)
{
    return Manifest.Objects.FindByPredicate([Team](const FGuLiHybridStartupEntry& Object)
    {
        return Object.Key.Kind == EGuLiHybridObjectKind::Ship && Object.Definition == TEXT("player/ship") && Object.Team == Team;
    });
}

void PlaceShip(FGuLiHybridStartupEntry& Ship, const ATargetPoint& Marker)
{
    Ship.Location = Marker.GetActorLocation();
    Ship.FacingYawDegrees = Marker.GetActorRotation().Yaw;
}

bool ReadConfiguration(const FGuLiHybridManifestData& Manifest, const FGuLiHybridStartupEntry& Ship,
    FGuLiGroupAbilityConfigSnapshot& Config, float& AcquireRadius)
{
    const FString* Asset = Manifest.Strings.Find(TEXT("wingman/ship/group_08/hangar/configuration"));
    const auto* Hangar = Asset ? LoadObject<UGuLiShipHangarDefinition>(nullptr, **Asset) : nullptr;
    const auto* ShipClass = Manifest.Strings.Find(TEXT("startup/ship_class"));
    const auto* Class = ShipClass ? LoadClass<AGuLiStrikeShip>(nullptr, **ShipClass) : nullptr;
    const auto* CDO = Class ? Class->GetDefaultObject<AGuLiStrikeShip>() : nullptr;
    const auto* Targeting = CDO ? CDO->WingmanTargetingRow.GetRow<FGuLiStrikeShipWingmanTargetingRow>(TEXT("WingmanPlayRange")) : nullptr;
    if (!Hangar || !Hangar->AbilitySet || !Targeting || Targeting->AcquireRadiusCentimeters <= 0
        || Manifest.Strings.FindRef(TEXT("wingman/ship/group_08/hangar/configuration_hash"))
            != LexToString(GetDefault<UGuLiShipHangarCapabilityComponent>()->GetConfigurationChecksum(Hangar))) return false;
    const auto* Assembly = CDO->GetShipAssembly();
    FGuLiShipCompiledBuildRules Rules;
    FGuLiShipRunBuildState InitialBuild;
    InitialBuild.MatchEpoch = InitialBuild.BuildRevision = 1;
    FString Reason;
    if (!Assembly || !Assembly->Catalog || !Assembly->Catalog->Compile(Rules)
        || !Assembly->Catalog->CanChoose(Rules, InitialBuild, HangarNode, Reason)) return false;

    AcquireRadius = Targeting->AcquireRadiusCentimeters;
    FGuLiShipAbilityProjectionContext Context;
    Context.ShipInstanceId = Context.OwnerPlayerGuid = Ship.Key.StableId;
    Context.ShipGeneration = Ship.Key.LifeGeneration;
    Context.GroupGeneration = Context.MatchEpoch = Context.FormationCommandRevision = Context.EffectiveClientSimTick = 1;
    Context.Team = Ship.Team;
    Context.WingmanTypeId = Hangar->AbilitySet->WingmanTypeId;
    return UGuLiShipHangarCapabilityComponent::BuildInitialConfiguration(*Hangar, Context, Config);
}

const FGuLiWingmanWeaponChannelConfig* FindWeapon(const FGuLiGroupAbilityConfigSnapshot& Config, EGuLiWingmanAttackPattern Pattern)
{
    return Config.WeaponChannels.FindByPredicate([Pattern](const FGuLiWingmanWeaponChannelConfig& Weapon)
    {
        return Weapon.bEnabled && Weapon.Kind == EGuLiWingmanWeaponKind::BasicAutomatic && Weapon.Runtime.Attack.Pattern == Pattern;
    });
}

void PrepareRangeRoster(FGuLiHybridManifestData& Manifest, const TArray<FGuLiHybridObjectKey>& Targets)
{
    // Keep both control seats, the resource ledger, and the original map infrastructure.
    Manifest.Objects.RemoveAll([&Targets](const FGuLiHybridStartupEntry& Object)
    {
        if (Object.Key.Kind == EGuLiHybridObjectKind::Soldier) return !Targets.Contains(Object.Key);
        return Object.Key.Kind == EGuLiHybridObjectKind::Engineer || Object.Key.Kind == EGuLiHybridObjectKind::Miner;
    });
    int32 Infantry = 0;
    for (const auto& Key : Targets)
    {
        if (Key.Kind != EGuLiHybridObjectKind::Soldier) continue;
        ++Infantry;
        const FString Prefix = TEXT("deployment/") + Key.StableId.ToString(EGuidFormats::Digits);
        Manifest.Numbers.Add(Prefix + TEXT("/start_idle"), 1);
        Manifest.Numbers.Add(Prefix + TEXT("/automatic_fire"), 0);
    }
    Manifest.Numbers.Add(TEXT("acceptance/population"), Infantry);
    Manifest.Numbers.Add(TEXT("startup/engineer_count"), 0);
    Manifest.Numbers.Add(TEXT("startup/miner_count"), 0);
    TArray<FString> Coverage;
    Manifest.Strings.FindRef(TEXT("startup/coverage")).ParseIntoArray(Coverage, TEXT(","));
    Coverage.Remove(TEXT("Engineer"));
    Coverage.Remove(TEXT("Miner"));
    if (!Infantry) Coverage.Remove(TEXT("Mass"));
    Manifest.Strings.Add(TEXT("startup/coverage"), FString::Join(Coverage, TEXT(",")));
    Manifest.Numbers.Add(GuLiMovementCapture::RoomHoldMaskKey,
        GuLiMovementCapture::TerritoryCapture | GuLiMovementCapture::AutomaticEconomy);
}

bool SelectGroundTargets(UWorld& World, const FGuLiHybridManifestData& Manifest, const FVector& Start, const FVector& Center,
    const TArray<FVector>& Slots, const FGuLiWingmanFormationRuntimeConfig& Formation,
    const FGuLiWingmanAttackProfile& Profile, float AcquireRadius, TArray<FGuLiHybridObjectKey>& Targets)
{
    TArray<const FGuLiHybridStartupEntry*> Candidates;
    for (const auto& Object : Manifest.Objects)
    {
        if (Object.Team == EGuLiTeam::Blue && Object.Key.Kind == EGuLiHybridObjectKind::Soldier
            && Object.Definition == TEXT("unit/1") && Object.Health > 0
            && FVector::DistSquared2D(Object.Location, Center) <= FMath::Square(6500.)
            && FVector::DistSquared(Object.Location, Start) <= FMath::Square(AcquireRadius)) Candidates.Add(&Object);
    }
    Candidates.Sort([Center](const FGuLiHybridStartupEntry& A, const FGuLiHybridStartupEntry& B)
    {
        const double A2 = FVector::DistSquared2D(A.Location, Center), B2 = FVector::DistSquared2D(B.Location, Center);
        return A2 != B2 ? A2 < B2 : A.Ordinal < B.Ordinal;
    });
    for (const auto* Object : Candidates)
    {
        FHitResult Ground;
        if (!World.LineTraceSingleByObjectType(Ground, Object->Location + FVector(0, 0, 5000),
            Object->Location - FVector(0, 0, 500000), FCollisionObjectQueryParams(ECC_WorldStatic))) continue;
        const bool bCanEnter = Slots.ContainsByPredicate([&](const FVector& Slot)
        {
            return GuLiWingmanTargeting::HasClearGroundBombingCorridor(&World, Ground.ImpactPoint,
                (Ground.ImpactPoint - Slot).GetSafeNormal2D(), Profile, Formation, &Slot);
        });
        if (bCanEnter) Targets.Add(Object->Key);
        if (Targets.Num() == GroundTargetCount) return true;
    }
    return false;
}

bool PlaceSector(UWorld& World, const FVector2D& Center, const FGuLiWingmanAttackProfile& Profile,
    const FGuLiWingmanFormationRuntimeConfig& Formation, TArray<FVector>& Positions)
{
    // Retain a compact squad while routing around cliffs, buildings and obstructed dive paths.
    const FVector2D Squad[] = {{-1100,-750},{850,-950},{-850,900},{1050,1150}};
    const FCollisionObjectQueryParams StaticWorld(ECC_WorldStatic);
    for (int32 Ring = 0; Ring <= 3; ++Ring)
    {
        for (int32 Direction = 0; Direction < (Ring ? 8 : 1); ++Direction)
        {
            const FVector2D Offset = FVector2D(3000. * Ring, 0).GetRotated(Direction * 45.);
            Positions.Reset(SoldiersPerSector);
            for (const FVector2D& Slot : Squad)
            {
                const FVector2D XY = Center + Offset + Slot;
                FHitResult Hit;
                if (!World.LineTraceSingleByObjectType(Hit, FVector(XY, 110000), FVector(XY, -110000), StaticWorld)
                    || !Cast<ALandscapeProxy>(Hit.GetActor()) || Hit.ImpactNormal.Z < 0.78
                    || !GuLiWingmanTargeting::HasClearGroundBombingCorridor(&World, Hit.ImpactPoint,
                        FVector::ForwardVector, Profile, Formation)) break;
                Positions.Add(Hit.ImpactPoint);
            }
            if (Positions.Num() == SoldiersPerSector) return true;
        }
    }
    return false;
}

bool DistributeGroundSquads(UWorld& World, FGuLiHybridManifestData& Manifest,
    const FGuLiWingmanAttackProfile& Profile, const FGuLiWingmanFormationRuntimeConfig& Formation,
    TArray<FGuLiHybridObjectKey>& Targets, FString& Error)
{
    FBox Bounds(ForceInit);
    TArray<FGuLiHybridStartupEntry*> Infantry;
    for (auto& Object : Manifest.Objects)
    {
        if (Object.Key.Kind == EGuLiHybridObjectKind::Territory) Bounds += Object.Location;
        if (Object.Key.Kind == EGuLiHybridObjectKind::Soldier && Object.Team == EGuLiTeam::Blue
            && Object.Definition == TEXT("unit/1") && Object.Health > 0) Infantry.Add(&Object);
    }
    if (!Bounds.IsValid || Infantry.Num() < SoldiersPerSector * SectorCountPerAxis * SectorCountPerAxis)
        return Reject(Error, TEXT("The combined range requires the map's territories and enough live Blue infantry."));
    Infantry.Sort([](const FGuLiHybridStartupEntry& A, const FGuLiHybridStartupEntry& B){ return A.Ordinal < B.Ordinal; });
    int32 Index = 0;
    for (int32 Row = 0; Row < SectorCountPerAxis; ++Row)
    {
        for (int32 Column = 0; Column < SectorCountPerAxis; ++Column)
        {
            const FVector2D Center(FMath::Lerp(Bounds.Min.X, Bounds.Max.X, .06 + .44 * Column),
                FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, .06 + .44 * Row));
            TArray<FVector> Positions;
            if (!PlaceSector(World, Center, Profile, Formation, Positions))
                return Reject(Error, FString::Printf(TEXT("No grounded squad and clear bombing corridor in sector (%d,%d)."), Column, Row));
            for (const FVector& Position : Positions)
            {
                auto& Soldier = *Infantry[Index++];
                Soldier.Location = Position;
                Targets.Add(Soldier.Key);
            }
        }
    }
    return true;
}
}

bool FGuLiWingmanPlayRange::Prepare(UWorld& World, ETrial Trial, FGuLiHybridManifestData& Manifest,
    const FString& Destination, FString& Error)
{
    if (!GuLiLocalPIERolePicker::IsWingmanTrial(Trial) || Manifest.MapPackage != World.GetOutermost()->GetName())
        return Reject(Error, TEXT("Wingman range requires its matching editor map and startup manifest."));
    const bool bAirOpponent = GuLiLocalPIERolePicker::HasAirOpponent(Trial);
    const TCHAR* StartName = Trial == ETrial::WingmanFlight ? TEXT("FlightStart")
        : bAirOpponent ? TEXT("AirStart") : TEXT("GroundStart");
    const auto* Start = FindMarker(World, StartName);
    auto* Ship = FindShip(Manifest, EGuLiTeam::Red);
    if (!Start || !Ship) return Reject(Error, FString::Printf(TEXT("Missing or duplicate wingman range start %s, or missing Red ship."), StartName));
    PlaceShip(*Ship, *Start);
    Carrier = Ship->Key;
    Targets.Reset();

    FGuLiGroupAbilityConfigSnapshot Config;
    float AcquireRadius = 0;
    if (!ReadConfiguration(Manifest, *Ship, Config, AcquireRadius))
        return Reject(Error, TEXT("Cannot prepare the production ship targeting profile or acquire its 08 hangar from an empty build."));
    TArray<FVector> Slots;
    if (!UGuLiWingmanSimulationSubsystem::PlanExternalFormationPositions(&World, Start->GetActorTransform(), Config.FormationRuntime, Slots, {}, &Error))
        return false;
    ClearSpawnSlots = Slots.Num();
    if (ClearSpawnSlots != GULI_WINGMAN_GROUP_SIZE) return Reject(Error, TEXT("Incomplete wingman launch formation."));

    if (bAirOpponent)
    {
        const auto* Marker = FindMarker(World, TEXT("AirTarget"));
        auto* Enemy = FindShip(Manifest, EGuLiTeam::Blue);
        const float Clearance = Config.FormationRuntime.AgentRadiusCentimeters + Config.FormationRuntime.SafetyMarginCentimeters();
        if (!Marker || !Enemy || !FindWeapon(Config, EGuLiWingmanAttackPattern::AirBurstOrbit)
            || FVector::Distance(Ship->Location, Marker->GetActorLocation()) > AcquireRadius
            || !GuLiWingmanAttack::AirSegmentClearsWorld(&World, Ship->Location, Marker->GetActorLocation(), Clearance))
            return Reject(Error, TEXT("The air range needs an opposing ship in range, a machine gun, and a clear engagement lane."));
        TArray<FVector> EnemySlots;
        if (!UGuLiWingmanSimulationSubsystem::PlanExternalFormationPositions(&World, Marker->GetActorTransform(), Config.FormationRuntime,
            EnemySlots, {}, &Error)) return false;
        EnemySpawnSlots = EnemySlots.Num();
        if (EnemySpawnSlots != GULI_WINGMAN_GROUP_SIZE) return Reject(Error, TEXT("Incomplete opposing launch formation."));
        PlaceShip(*Enemy, *Marker);
        Targets.Add(Enemy->Key);
        Manifest.Strings.Add(UTF8_TO_TCHAR(GuLiNet::WingmanRangeOpponentHangar), HangarNode.ToString());
        Manifest.Numbers.Add(UTF8_TO_TCHAR(GuLiNet::WingmanRangeProtectShipHulls), 1);
    }
    if (Trial == ETrial::WingmanCombined)
    {
        const auto* Missile = FindWeapon(Config, EGuLiWingmanAttackPattern::GroundDive);
        if (!Missile) return Reject(Error, TEXT("Combined range requires the ground missile."));
        if (!DistributeGroundSquads(World, Manifest, Missile->Runtime.Attack, Config.FormationRuntime, Targets, Error)) return false;
    }
    else if (Trial == ETrial::WingmanGround)
    {
        const auto* Marker = FindMarker(World, TEXT("GroundTarget"));
        const auto* Missile = FindWeapon(Config, EGuLiWingmanAttackPattern::GroundDive);
        if (!Marker || !Missile || !SelectGroundTargets(World, Manifest, Ship->Location, Marker->GetActorLocation(), Slots,
            Config.FormationRuntime, Missile->Runtime.Attack, AcquireRadius, Targets))
            return Reject(Error, FString::Printf(TEXT("Ground range found %d/%d Blue infantry with clear ingress, dive and climb."), Targets.Num(), GroundTargetCount));
    }
    if ((Trial == ETrial::WingmanFlight || Trial == ETrial::WingmanCombined)
        && (!FindMarker(World, TEXT("RidgeCrest")) || !FindMarker(World, TEXT("RidgeExit"))))
        return Reject(Error, TEXT("Missing or duplicate wingman ridge crest or exit."));

    TArray<FString> Participants{Carrier.StableId.ToString(EGuidFormats::Digits)};
    for (const auto& Target : Targets) Participants.Add(Target.StableId.ToString(EGuidFormats::Digits));
    Manifest.Strings.Add(UGuLiWingmanSimulationSubsystem::EditorRangeParticipants, FString::Join(Participants, TEXT(",")));
    PrepareRangeRoster(Manifest, Targets);
    return FGuLiHybridManifest::Write(Manifest, Destination, Error);
}

bool FGuLiWingmanPlayRange::Advance(UWorld& World, FString& Error)
{
    const double Now = FPlatformTime::Seconds();
    if (FirstReadyTime == 0) FirstReadyTime = Now;
    if (Now - FirstReadyTime > 120)
        return Reject(Error, TEXT("Wingman range timed out waiting for the controlled Red ship and its 08 hangar."));
    const auto* PC = World.GetFirstPlayerController();
    auto* Ship = PC ? Cast<AGuLiStrikeShip>(PC->GetPawn()) : nullptr;
    auto* Build = PC && PC->PlayerState ? PC->PlayerState->FindComponentByClass<UGuLiShipBuildComponent>() : nullptr;
    const auto* Binding = Ship ? Ship->FindComponentByClass<UGuLiHybridActorComponent>() : nullptr;
    if (!Binding || Binding->GetObjectKey() != Carrier || !Build
        || !UGuLiHybridActorComponent::HasSimulation(Ship)) return true;
    const auto* Hangar = Ship->FindComponentByClass<UGuLiShipHangarCapabilityComponent>();
    if (Hangar && Hangar->IsCapabilityEnabled())
    {
        bReady = true;
        UE_LOG(LogTemp, Display, TEXT("[GuLiLocalPIE] Wingman range ready: hangar=08 targets=%d spawn_slots=%d"), Targets.Num(), ClearSpawnSlots);
        return true;
    }
    if (EquipmentRequest.IsValid())
    {
        FGuLiShipChoiceResult Result;
        if (Build->FindChoiceResult(EquipmentRequest, Result) && !Result.bCommitted)
            return Reject(Error, TEXT("Wingman range could not equip 08: ") + Result.Reason);
        return true;
    }
    const auto& State = Build->GetBuildState();
    if (State.MatchEpoch == 0 || State.ChosenNodeIds.Contains(HangarNode)) return true;
    const auto Choices = Build->QueryChoices();
    if (Choices.ContainsByPredicate([](const auto& Choice){ return Choice.NodeId == HangarNode && Choice.bAvailable; }))
        EquipmentRequest = Build->RequestConfirmedChoice(FGuid::NewGuid(), HangarNode, State.MatchEpoch, State.BuildRevision);
    return true;
}

FString UGuLiHybridAuthoringLibrary::InspectWingmanPlayRange()
{
    auto Report = MakeShared<FJsonObject>();
    auto Runs = MakeShared<FJsonObject>();
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    const FString Source = FPaths::ProjectConfigDir() / TEXT("Hybrid/RuntimeManifest-600.json");
    FString Error;
    FGuLiHybridManifestData Production;
    bool bOK = World && FGuLiHybridManifest::Read(Source, Production, Error);
    if (!World) Error = TEXT("Editor world unavailable.");
    auto Inspect = [&](ETrial Trial, const TCHAR* Name)
    {
        if (!bOK) return;
        FGuLiWingmanPlayRange Range;
        FGuLiHybridManifestData Manifest = Production, Reloaded;
        const FString File = FPaths::Combine(FPlatformProcess::UserTempDir(),
            TEXT("GuLiWingmanRange-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
        FString ConfigHash, MapHash, Package;
        bOK = Range.Prepare(*World, Trial, Manifest, File, Error)
            && FGuLiHybridManifest::ValidateFile(File, ConfigHash, MapHash, Package, Error)
            && FGuLiHybridManifest::Read(File, Reloaded, Error);
        IFileManager::Get().Delete(*File);
        if (!bOK) return;
        auto* Ship = FindShip(Reloaded, EGuLiTeam::Red);
        const auto* Participants = Reloaded.Strings.Find(UGuLiWingmanSimulationSubsystem::EditorRangeParticipants);
        TArray<FString> Ids;
        if (Participants) Participants->ParseIntoArray(Ids, TEXT(","));
        if (Package != World->GetOutermost()->GetName() || !Ship || Ship->Key != Range.Carrier || Ids.Num() != Range.Targets.Num() + 1
            || Ids[0] != Range.Carrier.StableId.ToString(EGuidFormats::Digits))
        {
            bOK = Reject(Error, TEXT("Wingman range participants or map changed in manifest round trip."));
            return;
        }
        auto Run = MakeShared<FJsonObject>();
        Run->SetStringField(TEXT("carrier"), Ids[0]);
        Run->SetNumberField(TEXT("start_x_cm"), Ship->Location.X);
        Run->SetNumberField(TEXT("start_y_cm"), Ship->Location.Y);
        Run->SetNumberField(TEXT("start_z_cm"), Ship->Location.Z);
        Run->SetNumberField(TEXT("yaw_degrees"), Ship->FacingYawDegrees);
        Run->SetNumberField(TEXT("clear_spawn_slots"), Range.ClearSpawnSlots);
        Run->SetNumberField(TEXT("enemy_spawn_slots"), Range.EnemySpawnSlots);
        Run->SetStringField(TEXT("opponent_hangar_node"), Reloaded.Strings.FindRef(UTF8_TO_TCHAR(GuLiNet::WingmanRangeOpponentHangar)));
        Run->SetBoolField(TEXT("protected_ship_hulls"), Reloaded.Numbers.FindRef(UTF8_TO_TCHAR(GuLiNet::WingmanRangeProtectShipHulls)) == 1);
        int32 Infantry = 0;
        int32 EconomyVehicles = 0;
        for (const auto& Object : Reloaded.Objects)
        {
            if (Object.Key.Kind == EGuLiHybridObjectKind::Soldier) ++Infantry;
            if (Object.Key.Kind == EGuLiHybridObjectKind::Engineer || Object.Key.Kind == EGuLiHybridObjectKind::Miner) ++EconomyVehicles;
        }
        Run->SetNumberField(TEXT("startup_objects"), Reloaded.Objects.Num());
        Run->SetNumberField(TEXT("startup_infantry"), Infantry);
        Run->SetNumberField(TEXT("startup_economy_vehicles"), EconomyVehicles);
        Run->SetNumberField(TEXT("world_activity_hold"), Reloaded.Numbers.FindRef(GuLiMovementCapture::RoomHoldMaskKey));
        Run->SetStringField(TEXT("config_hash"), ConfigHash);
        TArray<TSharedPtr<FJsonValue>> Targets;
        for (const auto& Key : Range.Targets)
        {
            const auto* Enemy = Reloaded.Objects.FindByPredicate([&](const auto& Object){ return Object.Key == Key; });
            if (!Enemy || Enemy->Team != EGuLiTeam::Blue || !Ids.Contains(Key.StableId.ToString(EGuidFormats::Digits)))
            {
                bOK = Reject(Error, TEXT("Missing live-roster Blue target in the reloaded range manifest."));
                return;
            }
            auto Target = MakeShared<FJsonObject>();
            Target->SetStringField(TEXT("id"), Key.StableId.ToString(EGuidFormats::Digits));
            Target->SetStringField(TEXT("definition"), Enemy->Definition);
            Target->SetNumberField(TEXT("start_health"), Enemy->Health);
            if (Key.Kind == EGuLiHybridObjectKind::Soldier)
            {
                const FString Prefix = TEXT("deployment/") + Key.StableId.ToString(EGuidFormats::Digits);
                Target->SetBoolField(TEXT("starts_idle"), Reloaded.Numbers.FindRef(Prefix + TEXT("/start_idle")) == 1);
                const double* Fire = Reloaded.Numbers.Find(Prefix + TEXT("/automatic_fire"));
                Target->SetBoolField(TEXT("automatic_fire"), !Fire || *Fire != 0);
            }
            Target->SetNumberField(TEXT("x_cm"), Enemy->Location.X);
            Target->SetNumberField(TEXT("y_cm"), Enemy->Location.Y);
            Target->SetNumberField(TEXT("z_cm"), Enemy->Location.Z);
            Target->SetNumberField(TEXT("distance_m"), FVector::Distance(Ship->Location, Enemy->Location) / 100.);
            Targets.Add(MakeShared<FJsonValueObject>(Target));
        }
        Run->SetArrayField(TEXT("blue_targets"), Targets);
        Runs->SetObjectField(Name, Run);
    };
    Inspect(ETrial::WingmanFlight, TEXT("flight"));
    Inspect(ETrial::WingmanAir, TEXT("air"));
    Inspect(ETrial::WingmanGround, TEXT("ground"));
    Inspect(ETrial::WingmanCombined, TEXT("combined"));
    Report->SetBoolField(TEXT("ok"), bOK);
    Report->SetStringField(TEXT("error"), Error);
    Report->SetObjectField(TEXT("trials"), Runs);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
