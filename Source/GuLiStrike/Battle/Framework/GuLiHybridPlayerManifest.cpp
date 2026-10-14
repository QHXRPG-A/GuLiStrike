#include "Gameplay/GroundMech/GuLiHybridGroundUpgrade.h"
#include "Network/Private/GuLiHybridUpgradeCatalog.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Battle/Framework/GuLiBattleGameMode.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/Build/Private/GuLiShipSharedBuildRules.h"
#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/WorldSettings.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

static bool AppendGroundStartup(UWorld& World,FGuLiHybridManifestData& Data,FString& Error)
{
    if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,TEXT("player/ground"),TEXT("GroundPlayer"),Error)) return false;
    const auto* ModeClass=World.GetWorldSettings()->DefaultGameMode.Get();
    const auto* Mode=ModeClass?Cast<AGuLiBattleGameMode>(ModeClass->GetDefaultObject()):nullptr;
    auto* GroundClass=Mode?Mode->GetConfiguredRolePawnClass(EGuLiCommanderRole::Ground):nullptr;
    const auto* Ground=GroundClass?Cast<AGuLiGroundMechCharacter>(GroundClass->GetDefaultObject()):nullptr;
    const auto* Weapon=Ground?Ground->GetWeapon():nullptr;
    const auto* Health=Ground?Ground->GetCombatHealthComponent():nullptr;
    if (!Ground || !Health || !FMath::IsFinite(Health->GetConfiguredMaxHealth()) || Health->GetConfiguredMaxHealth()<=0
        || !Weapon || !Weapon->bWeaponEnabled || !Weapon->UpgradeTable || !Weapon->SkillTable
        || Weapon->UpgradeTable->GetRowStruct()!=FGuLiStrikeMechUpgradesRow::StaticStruct()
        || Weapon->SkillTable->GetRowStruct()!=FGuLiStrikeMechSkillsRow::StaticStruct())
    { Error=TEXT("Ground role has no configured production character/weapon tables"); return false; }
    const FGuLiStrikeMechUpgradesRow* Upgrade=nullptr;
    for (const auto& Pair : Weapon->UpgradeTable->GetRowMap())
    { const auto* Row=reinterpret_cast<const FGuLiStrikeMechUpgradesRow*>(Pair.Value); if (Row->Id==Weapon->CurrentUpgradeId) Upgrade=Row; }
    const FGuLiStrikeMechSkillsRow* Skill=nullptr;
    if (Upgrade) for (const auto& Pair : Weapon->SkillTable->GetRowMap())
    { const auto* Row=reinterpret_cast<const FGuLiStrikeMechSkillsRow*>(Pair.Value); if (Row->Id==Upgrade->SkillId) Skill=Row; }
    if (!Upgrade || !Skill || Upgrade->FireRate<=0 || Upgrade->FireRate>30 || Upgrade->Damage<=0 || Skill->ProjectileLifetime<=0)
    { Error=TEXT("Invalid initial Ground weapon configuration"); return false; }
    Data.Strings.Add(TEXT("startup/ground_class"),GroundClass->GetPathName());
    if (!FGuLiHybridComponentState::BindDefinitionLayout(Data,TEXT("player/ground"),*GroundClass,Error)) return false;
    Data.Strings.Add(TEXT("startup/ground_upgrade"),Weapon->CurrentUpgradeId);
    Data.Numbers.Add(TEXT("startup/ground_health"),Health->GetConfiguredMaxHealth());
    if (!GuLiHybridGroundUpgrade::Append(*Weapon,Data,Error)) return false;
    const float Radius=Ground->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight=Ground->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
    const auto* NavData=Navigation?Navigation->GetNavDataForAgentName(TEXT("CommanderSoldier")):nullptr;
    for (const auto Team : {EGuLiTeam::Red,EGuLiTeam::Blue})
    {
        FVector Center=FVector::ZeroVector; int32 Count=0;
        for (const auto& O : Data.Objects) if (O.Team==Team && O.Key.Kind==EGuLiHybridObjectKind::Soldier) { Center+=O.Location; ++Count; }
        if (!Count || !Navigation || !NavData) { Error=TEXT("Ground spawn requires the authored team deployment and navigation"); return false; }
        Center/=Count; bool Found=false; FVector Position=FVector::ZeroVector;
        // Resolve space once in the editor. The cloud consumes only the resulting spawn facts.
        for (int32 Ring=1; Ring<=16 && !Found; ++Ring) for (int32 Step=0; Step<16 && !Found; ++Step)
        {
            const double Angle=Step*2.*PI/16.; const FVector Candidate=Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius*4.*Ring;
            FNavLocation Projected;
            if (!Navigation->ProjectPointToNavigation(Candidate,Projected,FVector(Radius,Radius,10000),NavData)) continue;
            Position=Projected.Location+FVector(0,0,HalfHeight+2.f);
            const bool Occupied=Data.Objects.ContainsByPredicate([&](const auto& O)
            { return FVector::DistSquared2D(O.Location,Position)<FMath::Square(Radius+300.f); });
            FCollisionQueryParams Query(SCENE_QUERY_STAT(HybridGroundSpawn),false);
            if (!Occupied && !World.OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(Radius,HalfHeight),Query)) Found=true;
        }
        if (!Found) { Error=TEXT("No validated capsule space for Ground player"); return false; }
        auto& O=Data.Objects.AddDefaulted_GetRef(); O.Team=Team; O.Ordinal=Team==EGuLiTeam::Red?1:2;
        O.Definition=TEXT("player/ground"); O.Location=Position; O.FacingYawDegrees=Team==EGuLiTeam::Red?-90:90;
        O.Key=FGuLiHybridManifest::MakeStableKey(Data.MapPackage,EGuLiHybridObjectKind::GroundPlayer,FString::Printf(TEXT("player/%u/ground"),uint8(Team)));
        O.Health=Health->GetConfiguredMaxHealth();
    }
    Data.Strings.FindOrAdd(TEXT("startup/coverage"))+=TEXT(",Ground");
    Data.Strings.Add(TEXT("authority/ground"),TEXT("ControllerSimulation/v1"));
    return true;
}

static bool AppendShipStartup(UWorld& World,FGuLiHybridManifestData& Data,FString& Error)
{
    if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,TEXT("player/ship"),TEXT("ShipPlayer"),Error)) return false;
    const auto* ModeClass=World.GetWorldSettings()->DefaultGameMode.Get();
    const auto* Mode=ModeClass?Cast<AGuLiBattleGameMode>(ModeClass->GetDefaultObject()):nullptr;
    UClass* ShipClass=Mode?Mode->GetConfiguredRolePawnClass(EGuLiCommanderRole::Air):nullptr;
    const auto* Ship=ShipClass?Cast<AGuLiStrikeShip>(ShipClass->GetDefaultObject()):nullptr;
    const auto* Hull=Ship?Ship->GetHullMeshComponent():nullptr;
    const UStaticMesh* Mesh=Hull?Hull->GetStaticMesh().Get():nullptr;
    const auto* Health=Ship?Ship->GetCombatHealthComponent():nullptr;
    if (!Ship || !Ship->GetShipMovement() || !Mesh || !Health || Health->GetConfiguredMaxHealth()<=0)
    { Error=TEXT("Air role has no configured production Ship, movement, hull or health"); return false; }
    Data.Strings.Add(TEXT("startup/ship_class"),ShipClass->GetPathName());
    if (!FGuLiHybridComponentState::BindDefinitionLayout(Data,TEXT("player/ship"),*ShipClass,Error)) return false;
    Data.Strings.Add(TEXT("startup/ship_hull"),Mesh->GetPathName());
    Data.Numbers.Add(TEXT("startup/ship_health"),Health->GetConfiguredMaxHealth());
    // A CDO's unregistered component world transform does not include the
    // model parent's authored scale. Resolve the attachment chain explicitly,
    // matching the registered Ship hull instead of exporting source-mesh size.
    FVector HullScale(1);
    for (const USceneComponent* Part=Hull;Part;Part=Part->GetAttachParent()) HullScale*=Part->GetRelativeScale3D();
    const float Radius=FMath::Max(Ship->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Mesh->GetBounds().SphereRadius*HullScale.GetAbsMax());
    if (!FMath::IsFinite(Radius) || Radius<=0 || Radius>50000)
    { Error=TEXT("Invalid production Ship collision bounds"); return false; }
    Data.Numbers.Add(TEXT("startup/ship_bounds_radius"),Radius);
    const auto* Assembly=Ship->GetShipAssembly();const auto* Catalog=Assembly?Assembly->Catalog.Get():nullptr;
    FGuLiShipCompiledBuildRules UpgradeRules;
    if (!Catalog || !Catalog->Compile(UpgradeRules) || !UpgradeRules.Shared)
    {Error=TEXT("Invalid production Ship upgrade catalog: ")+UpgradeRules.Error;return false;}
    if (!Assembly->ExportHybridCapabilityContracts(Data,Error)) return false;
    auto& UpgradeRule=Data.Rules.AddDefaulted_GetRef();UpgradeRule.Id=Assembly->UpgradeRule.ToString().ToLower();
    UpgradeRule.Kind=EGuLiHybridRuleKind::Upgrade;UpgradeRule.SourceDefinition=UpgradeRule.TargetDefinition=TEXT("player/ship");
    UpgradeRule.UpgradeCatalog=Assembly->UpgradeDomain.ToString().ToLower();
    if (!GuLiHybridWingman::AppendCatalog(*Catalog,Assembly->UpgradeDomain,Data,Error)) return false;
    Data.Strings.Add(TEXT("startup/ship_upgrade_catalog"),Assembly->UpgradeDomain.ToString().ToLower());
    if (!GuLiHybridUpgradeCatalog::Append(Data,Assembly->UpgradeDomain.ToString().ToLower(),UpgradeRules.Shared->Rules.Data,Error)) return false;
    for (const auto Team : {EGuLiTeam::Red,EGuLiTeam::Blue})
    {
        const auto* Ground=Data.Objects.FindByPredicate([Team](const auto& O){ return O.Team==Team && O.Key.Kind==EGuLiHybridObjectKind::GroundPlayer; });
        if (!Ground) { Error=TEXT("Ship spawn requires the validated team spawn anchor"); return false; }
        FVector Position=Ground->Location; bool Found=false;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(HybridShipSpawn),false);
        for (int32 Level=1;Level<=16 && !Found;++Level)
        {
            Position=Ground->Location+FVector(0,0,Radius*2*Level+2000);
            Found=!World.OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(Radius),Query);
        }
        if (!Found) { Error=TEXT("No validated space for production Ship hull"); return false; }
        auto& O=Data.Objects.AddDefaulted_GetRef(); O.Team=Team; O.Ordinal=Team==EGuLiTeam::Red?1:2;
        O.Definition=TEXT("player/ship"); O.Location=Position; O.FacingYawDegrees=Team==EGuLiTeam::Red?-90:90;
        O.Key=FGuLiHybridManifest::MakeStableKey(Data.MapPackage,EGuLiHybridObjectKind::Ship,FString::Printf(TEXT("player/%u/ship"),uint8(Team)));
        O.Health=Health->GetConfiguredMaxHealth();
    }
    Data.Strings.FindOrAdd(TEXT("startup/coverage"))+=TEXT(",Ship");
    Data.Strings.Add(TEXT("authority/ship"),TEXT("ControllerSimulation/v1")); return true;
}

static const FGuLiHybridManifestRegistration GroundManifestProvider({TEXT("Ground"),EGuLiHybridManifestPhase::Definitions,{},AppendGroundStartup});

static const FGuLiHybridManifestRegistration ShipManifestProvider({TEXT("Ship"),EGuLiHybridManifestPhase::Definitions,{TEXT("Ground")},AppendShipStartup});
