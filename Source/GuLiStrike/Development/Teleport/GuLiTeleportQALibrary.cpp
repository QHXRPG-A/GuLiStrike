#include "Development/Teleport/GuLiTeleportQALibrary.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "GameFramework/Pawn.h"
#include "Battle/Framework/GuLiBattleGameMode.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "UObject/UnrealType.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Serialization/JsonSerializer.h"

bool UGuLiTeleportQALibrary::ConfigurePIE(int32 Mode,int32 Clients)
{
	if (!GEditor || GEditor->PlayWorld || Mode<0 || Mode>2 || Clients<1 || Clients>4) return false;
	auto* Settings=GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(EPlayNetMode(Mode)); Settings->SetPlayNumberOfClients(Clients); Settings->SetRunUnderOneProcess(true);
	return true;
}
void UGuLiTeleportQALibrary::RequestLateJoin() { if (GEditor && GEditor->PlayWorld) GEditor->RequestLateJoin(); }
bool UGuLiTeleportQALibrary::SubmitIntent(UGuLiTeleportInputComponent* Input,EGuLiTeleportCommand Command,FGuid CastId,FVector Point)
{
	const auto* Controller=Input?Cast<APlayerController>(Input->GetOwner()):nullptr;
	if (!Controller || !Controller->IsLocalController() || !Controller->GetWorld()->IsPlayInEditor()) return false;
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor,false);
	Input->ServerSubmit(Command,CastId,Point);
	return true;
}
bool UGuLiTeleportQALibrary::PreferAirForNextJoin(UObject* Context)
{
	UWorld* World=Context?Context->GetWorld():nullptr;
	auto* Mode=World?World->GetAuthGameMode<AGuLiBattleGameMode>():nullptr;
	if (!Mode || !World->IsPlayInEditor()) return false;
	// Alter only this test world's next login; never save a CDO or map override.
	auto* Property=FindFProperty<FArrayProperty>(Mode->GetClass(),TEXT("InitialRolePriority"));
	if (!Property) return false;
	*Property->ContainerPtrToValuePtr<TArray<EGuLiCommanderRole>>(Mode)={EGuLiCommanderRole::Air,EGuLiCommanderRole::Commander,EGuLiCommanderRole::Ground};
	return true;
}
bool UGuLiTeleportQALibrary::RevokeCommanderRole(APlayerController* Controller)
{
	auto* State=Controller?Controller->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
	if (!State || !State->HasAuthority() || !State->GetWorld()->IsPlayInEditor()) return false;
	State->SetServerObserver(); return true;
}
ASceneCapture2D* UGuLiTeleportQALibrary::CreateCapture(UObject* Context)
{
	UWorld* World=Context?Context->GetWorld():nullptr;
	if (!World || !World->IsGameWorld()) return nullptr;
	FActorSpawnParameters P; P.ObjectFlags|=RF_Transient;
	return World->SpawnActor<ASceneCapture2D>(P);
}
AActor* UGuLiTeleportQALibrary::CreateBlocker(UObject* Context,FVector Center,FVector Extent)
{
	UWorld* World=Context?Context->GetWorld():nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client || Center.ContainsNaN() || Extent.ContainsNaN()) return nullptr;
	FActorSpawnParameters P; P.ObjectFlags|=RF_Transient;
	auto* Actor=World->SpawnActor<AActor>(P); auto* Box=NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Box); Box->SetBoxExtent(Extent); Box->SetCollisionObjectType(ECC_WorldDynamic);
	Box->SetCollisionResponseToAllChannels(ECR_Block); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCanEverAffectNavigation(false); Box->RegisterComponent(); Actor->SetActorLocation(Center); return Actor;
}
bool UGuLiTeleportQALibrary::DamageMass(UObject* Context,int64 Id,float Amount)
{
	UWorld* World=Context?Context->GetWorld():nullptr;
	auto* Authority=World?World->GetSubsystem<UGuLiBattleAuthoritySubsystem>():nullptr;
	FGuLiSoldierId Soldier; Soldier.Value=uint32(Id);
	return Id>0 && Id<=MAX_uint32 && Authority && Authority->ApplyDamage(Soldier,Amount);
}
FString UGuLiTeleportQALibrary::Snapshot(UObject* Context)
{
	UWorld* World=Context?Context->GetWorld():nullptr; if (!World) return TEXT("{}");
	auto Root=MakeShared<FJsonObject>(); Root->SetStringField(TEXT("world"),World->GetPathName());
	Root->SetNumberField(TEXT("net_mode"),World->GetNetMode()); Root->SetNumberField(TEXT("time"),World->GetTimeSeconds());
	TArray<TSharedPtr<FJsonValue>> Units;
	const auto* Authority=World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
	{
		for (const auto& S : It->GetItems())
		{
			auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("id"),S.SoldierId.Value); Item->SetNumberField(TEXT("team"),int32(S.Team));
			Item->SetNumberField(TEXT("type"),S.UnitTypeId); Item->SetNumberField(TEXT("health"),S.Health);
			Item->SetBoolField(TEXT("phased"),S.bPhased); Item->SetBoolField(TEXT("locked"),S.bExternalActionsLocked);
			FGuLiSoldierCombatDebug Debug; FVector Location=S.DisplacementLocation;
			if (Authority && Authority->TryGetSoldierCombatDebug(S.SoldierId,Debug))
			{ Location=Debug.Location; Item->SetNumberField(TEXT("cooldown"),Debug.CooldownRemaining); Item->SetNumberField(TEXT("shots"),double(Debug.ShotsFired)); }
			Item->SetNumberField(TEXT("x"),Location.X); Item->SetNumberField(TEXT("y"),Location.Y); Item->SetNumberField(TEXT("z"),Location.Z);
			Item->SetNumberField(TEXT("floor"),S.DisplacementFrameFloor); Units.Add(MakeShared<FJsonValueObject>(Item));
		}
	}
	Root->SetArrayField(TEXT("units"),Units);
	TArray<TSharedPtr<FJsonValue>> Wingmen;
	for (TActorIterator<AGuLiWingmanPawn> It(World); It; ++It)
	{
		const auto& Handle=It->GetWingmanHandle(); if (!Handle.IsValid()) continue;
		auto Item=MakeShared<FJsonObject>(); const FVector P=It->GetActorLocation();
		Item->SetStringField(TEXT("ship"),Handle.Flight.Group.ShipInstanceId.ToString());
		Item->SetNumberField(TEXT("slot"),Handle.GetGroupMemberIndex());
		Item->SetNumberField(TEXT("generation"),Handle.EntityGeneration);
		Item->SetBoolField(TEXT("alive"),It->GetRuntimeState().Dynamics.bAlive);
		Item->SetBoolField(TEXT("visible"),!It->IsHidden());
		Item->SetBoolField(TEXT("phased"),UGuLiExternalUnitControlComponent::IsActorPhased(*It));
		Item->SetNumberField(TEXT("x"),P.X); Item->SetNumberField(TEXT("y"),P.Y); Item->SetNumberField(TEXT("z"),P.Z);
		Wingmen.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("wingmen"),Wingmen);
	TArray<TSharedPtr<FJsonValue>> Groups;
	for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
	{
		const auto* PS=It->GetPlayerState<AGuLiBattlePlayerState>(); const auto* Relay=It->GetWingmanRelay();
		const auto* Core=Relay?Relay->GetServerRelay():nullptr; if (!PS || !Core) continue;
		auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("player_slot"),PS->GetBattleSlotIndex());
		Item->SetStringField(TEXT("ship"),Core->GetLeaseState().Group.ShipInstanceId.ToString());
		Item->SetNumberField(TEXT("living"),Core->GetRoster().FilterByPredicate([](const auto& R){return !R.bDead;}).Num());
		Item->SetStringField(TEXT("health_hash"),LexToString(GuLiWingmanRelayHash::Health(Core->GetHealth())));
		Item->SetStringField(TEXT("config_hash"),LexToString(Core->GetAbilityConfig().SnapshotHash));
		Item->SetNumberField(TEXT("movement_writes"),double(Core->GetServerWingmanMovementWriteCount()));
		Item->SetBoolField(TEXT("locked"),Core->IsExternallyControlled());
		Groups.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("wingman_groups"),Groups);
	FString Result; auto Writer=TJsonWriterFactory<>::Create(&Result); FJsonSerializer::Serialize(Root,Writer); return Result;
}
#endif
