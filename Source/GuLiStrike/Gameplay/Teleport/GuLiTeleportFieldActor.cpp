#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Gameplay/Navigation/GuLiLandingGround.h"
#include "Gameplay/Teleport/GuLiTeleportUnitAdapters.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

struct FGuLiTeleportFieldRuntime
{
	TWeakObjectPtr<AGuLiBattlePlayerState> Commander;
	TArray<FGuLiTeleportUnit> Units;
	double NextReturnAttempt = 0;
	bool bCollected = false;
	bool bLandingCommitted = false;
};

namespace
{
	bool CanCast(const AGuLiBattlePlayerState* PS)
	{
		const auto* PC = PS ? Cast<APlayerController>(PS->GetOwner()) : nullptr;
		return IsValid(PS) && IsValid(PC) && !PC->IsActorBeingDestroyed() && PS->IsCommander() && PS->IsBattleReady();
	}
	bool Overlaps(const FGuLiTeleportUnit& A, const FGuLiTeleportUnit& B)
	{
		const FVector P = A.Landing.GetLocation() + (A.bGroundPivot ? FVector(0,0,A.HalfHeight+0.6) : FVector::ZeroVector);
		const FVector Q = B.Landing.GetLocation() + (B.bGroundPivot ? FVector(0,0,B.HalfHeight+0.6) : FVector::ZeroVector);
		return FMath::Abs(P.Z-Q.Z) < A.HalfHeight+B.HalfHeight && FVector::DistSquared2D(P,Q) < FMath::Square(A.Radius+B.Radius+0.4);
	}
}
AGuLiTeleportFieldActor::~AGuLiTeleportFieldActor() = default;
void AGuLiTeleportFieldActor::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) { WorldTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&ThisClass::OnWorldPostActorTick); }
}
void AGuLiTeleportFieldActor::OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	// All Mass tick groups, including FrameEnd, have completed and joined here.
	if (World == GetWorld() && HasAuthority()) { TickAuthority(); }
}
void FGuLiTeleportFieldRuntimeDeleter::operator()(FGuLiTeleportFieldRuntime* Runtime) const { delete Runtime; }
void AGuLiTeleportFieldActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AGuLiTeleportFieldActor,State);
}
double AGuLiTeleportFieldActor::GetSynchronizedTime(const UWorld& World)
{
	const auto* GS = World.GetGameState(); return GS ? GS->GetServerWorldTimeSeconds() : World.GetTimeSeconds();
}
AGuLiTeleportFieldActor* AGuLiTeleportFieldActor::FindCast(UWorld& World, FGuid CommanderId)
{
	AGuLiTeleportFieldActor* Latest = nullptr;
	for (TActorIterator<AGuLiTeleportFieldActor> It(&World); It; ++It)
	{
		if (!It->bPreview && !It->IsActorBeingDestroyed() && It->State.CommanderId == CommanderId
			&& (!Latest || It->State.PhaseStartTime > Latest->State.PhaseStartTime)) { Latest = *It; }
	}
	return Latest;
}
AGuLiTeleportFieldActor* AGuLiTeleportFieldActor::StartCast(AGuLiBattlePlayerState& Commander, int32 Level, FVector Point, FString& Error)
{
	UWorld* World = Commander.GetWorld(); const auto* GS = World ? World->GetGameState<AGuLiBattleGameState>() : nullptr;
	const auto* Data = World ? World->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	const auto* Config = Data ? Data->FindTeleportFieldConfig(Level) : nullptr;
	if (!Commander.HasAuthority() || !CanCast(&Commander) || !GS || !GS->IsMatchInProgress())
	{ Error = GuLiGameText::Text(TEXT("UI.TeleportFieldActor.172")); return nullptr; }
	if (!Config || !Config->IsValid()) { Error = GuLiGameText::Text(TEXT("UI.TeleportFieldActor.173")); return nullptr; }
	if (const auto* Previous = FindCast(*World,Commander.GetPlayerGuid()); Previous && Previous->State.IsActive())
	{ Error = GuLiGameText::Text(TEXT("UI.TeleportFieldActor.174")); return nullptr; }
	FVector GroundLocation;
	if (!GuLiLandingGround::Resolve(*World,Point,GroundLocation)) { Error = GuLiGameText::Text(TEXT("UI.TeleportFieldActor.175")); return nullptr; }
	FActorSpawnParameters Params; Params.Owner = Commander.GetOwner();
	auto* Field = World->SpawnActor<AGuLiTeleportFieldActor>(GroundLocation,FRotator::ZeroRotator,Params);
	if (!Field) { Error = GuLiGameText::Text(TEXT("UI.TeleportFieldActor.176")); return nullptr; }
	Field->SetReplicates(true); Field->Runtime.Reset(new FGuLiTeleportFieldRuntime); Field->Runtime->Commander = &Commander;
	auto& S = Field->State; S.CastId = FGuid::NewGuid(); S.CommanderId = Commander.GetPlayerGuid(); S.MatchEpoch = GS->GetMatchEpoch();
	S.Config = *Config; S.Team = Commander.GetTeam(); S.Source = S.Destination = GroundLocation;
	S.Phase = EGuLiTeleportPhase::Windup; S.PhaseStartTime = GetSynchronizedTime(*World); S.Deadline = S.PhaseStartTime+Config->WindupSeconds;
	Field->Publish(); return Field;
}
void AGuLiTeleportFieldActor::PruneParticipants()
{
	if (!Runtime) { return; }
	Runtime->Units.RemoveAll([this](const auto& Unit)
	{ return Unit.Kind == EGuLiTeleportUnitKind::Mass ? !GuLiTeleportMassAdapter::IsAlive(*GetWorld(),Unit) : !GuLiTeleportActorAdapter::IsAlive(Unit); });
	TSet<AActor*> Parents;
	for (const auto& Unit : Runtime->Units) { if (Unit.Kind == EGuLiTeleportUnitKind::Actor) { Parents.Add(Unit.Actor.Get()); } }
	Runtime->Units.RemoveAll([&](const auto& Unit) { return Unit.Kind == EGuLiTeleportUnitKind::Wingman && !Parents.Contains(Unit.Carrier.Get()); });
	if (State.ParticipantCount != Runtime->Units.Num()) { State.ParticipantCount = Runtime->Units.Num(); Publish(); }
}
bool AGuLiTeleportFieldActor::ApplyParticipants(bool bPhased, bool bLocked, bool bDisplace)
{
	if (!Runtime || !HasAuthority()) { return false; }
	if (!GuLiTeleportMassAdapter::CanApply(*GetWorld(),Runtime->Units,State.CastId)
		|| !GuLiTeleportActorAdapter::CanApply(Runtime->Units,State.CastId,bPhased && !Runtime->bCollected)) { return false; }
	auto* Material = bPhased ? GuLiVfx::Load<UMaterialInterface>(this, GuLiVfxIds::TeleportBody) : nullptr;
	// Membership and every endpoint are checked above. No asynchronous work occurs between these commits.
	const bool bActors = GuLiTeleportActorAdapter::Apply(*GetWorld(),Runtime->Units,State.CastId,bPhased,bLocked,bDisplace,Material);
	const bool bMass = bActors && GuLiTeleportMassAdapter::Apply(*GetWorld(),Runtime->Units,State.CastId,bPhased,bLocked,bDisplace);
	ensureMsgf(bActors && bMass,TEXT("Prevalidated external unit transaction failed for %s"),*State.CastId.ToString());
	return bActors && bMass;
}
void AGuLiTeleportFieldActor::Capture()
{
	GuLiTeleportMassAdapter::Collect(*GetWorld(),State,Runtime->Units);
	GuLiTeleportActorAdapter::Collect(*GetWorld(),State,Runtime->Units);
	PruneParticipants(); State.ParticipantCount = Runtime->Units.Num();
	if (Runtime->Units.IsEmpty()) { Finish(GuLiGameText::Text(TEXT("UI.TeleportFieldActor.177"))); return; }
	if (!ApplyParticipants(true,true,true)) { ReturnToSource(GuLiGameText::Text(TEXT("UI.TeleportFieldActor.178"))); return; }
	Runtime->bCollected = true;
	State.Phase = EGuLiTeleportPhase::AwaitingDestination; State.PhaseStartTime = GetSynchronizedTime(*GetWorld());
	State.Deadline = State.PhaseStartTime+State.Config.MaxTargetWaitSeconds; Publish();
}
bool AGuLiTeleportFieldActor::PlanLanding(FVector Center, bool bReturning)
{
	PruneParticipants();
	const float Radius = State.Config.RadiusCentimeters * (bReturning ? 4.f : 1.f);
	TArray<FGuLiTeleportUnit> Reserved; GuLiTeleportMassAdapter::GetObstacles(*GetWorld(),Reserved);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TeleportLanding),false);
	GuLiTeleportActorAdapter::ConfigureCollisionQuery(*GetWorld(),Runtime->Units,Params);
	auto IsBlocked = [&](const FGuLiTeleportUnit& Unit)
	{
		const FVector P = Unit.Landing.GetLocation() + (Unit.bGroundPivot ? FVector(0,0,Unit.HalfHeight+0.6) : FVector::ZeroVector);
		return Reserved.ContainsByPredicate([&](const auto& Other) { return Overlaps(Unit,Other); })
			|| GetWorld()->OverlapBlockingTestByChannel(P,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Unit.Radius,FMath::Max(Unit.HalfHeight,Unit.Radius)),Params);
	};
	TArray<int32> Order;
	for (int32 I = 0; I < Runtime->Units.Num(); ++I) { if (Runtime->Units[I].Kind != EGuLiTeleportUnitKind::Wingman) { Order.Add(I); } }
	Order.StableSort([&](int32 A,int32 B) { return Runtime->Units[A].Radius > Runtime->Units[B].Radius; });
	for (int32 I : Order)
	{
		auto& Unit = Runtime->Units[I]; const FVector Offset = Unit.Original.GetLocation()-State.Source; bool bFound = false;
		for (int32 Attempt = 0; Attempt < 257; ++Attempt)
		{
			const float D = Attempt ? FMath::Sqrt(float(Attempt))*FMath::Max(Unit.Radius*2.1f,20.f) : 0;
			const float Angle = Attempt*2.39996323f;
			FVector GroundLocation; double SurfaceHeight = 0;
			const FVector Desired = Center + FVector(Offset.X+FMath::Cos(Angle)*D,Offset.Y+FMath::Sin(Angle)*D,0);
			const bool bValidGround = Unit.bPreserveGroundClearance
				? GuLiSkillTargeting::ResolveGround(*GetWorld(),Desired,GroundLocation,&SurfaceHeight)
				: GuLiLandingGround::Resolve(*GetWorld(),Desired,GroundLocation,&SurfaceHeight,Cast<APawn>(Unit.Actor.Get()));
			if (!bValidGround || !GuLiTeleport::IsInsideDisc(GroundLocation,Center,Radius)) { continue; }
			// Actor capsules rest on physical support; Mass keeps its navigation pivot.
			if (!Unit.bGroundPivot) { GroundLocation.Z = SurfaceHeight; }
			Unit.Landing = FTransform(Unit.Original.GetRotation(),GroundLocation+FVector(0,0,Unit.Altitude),Unit.Original.GetScale3D());
			if (!IsBlocked(Unit)) { Reserved.Add(Unit); bFound = true; break; }
		}
		if (!bFound) { return false; }
	}
	if (!GuLiTeleportActorAdapter::PrepareFollowers(*GetWorld(),Runtime->Units)) { return false; }
	for (const auto& Unit : Runtime->Units)
	{
		if (Unit.Kind == EGuLiTeleportUnitKind::Wingman)
		{ if (IsBlocked(Unit)) { return false; } Reserved.Add(Unit); }
	}
	return GuLiTeleportMassAdapter::CanApply(*GetWorld(),Runtime->Units,State.CastId)
		&& GuLiTeleportActorAdapter::CanApply(Runtime->Units,State.CastId,false);
}
bool AGuLiTeleportFieldActor::CommitLanding(bool bReturning)
{
	if (!ApplyParticipants(false,true,true)) { return false; }
	Runtime->bLandingCommitted = true;
	State.bLanded = true;
	State.Phase = EGuLiTeleportPhase::Recovery; State.PhaseStartTime = GetSynchronizedTime(*GetWorld());
	State.Deadline = State.PhaseStartTime+State.Config.RecoverySeconds;
	State.Message = bReturning ? GuLiGameText::Text(TEXT("UI.TeleportFieldActor.179")) : GuLiGameText::Text(TEXT("UI.TeleportFieldActor.180")); Publish(); return true;
}
bool AGuLiTeleportFieldActor::SubmitDestination(AGuLiBattlePlayerState& Commander, FVector Point, FString& Error)
{
	if (!HasAuthority() || !Runtime || Runtime->Commander != &Commander || !CanCast(&Commander) || State.Phase != EGuLiTeleportPhase::AwaitingDestination)
	{ Error = GuLiGameText::Text(TEXT("UI.TeleportFieldActor.181")); return false; }
	if (GetSynchronizedTime(*GetWorld()) >= State.Deadline) { ReturnToSource(GuLiGameText::Text(TEXT("UI.TeleportFieldActor.182"))); return false; }
	FVector GroundLocation;
	if (!GuLiLandingGround::Resolve(*GetWorld(),Point,GroundLocation) || !PlanLanding(GroundLocation,false))
	{ Error = State.Message = GuLiGameText::Text(TEXT("UI.TeleportFieldActor.183")); Publish(); return false; }
	State.Destination = GroundLocation; return CommitLanding(false);
}
void AGuLiTeleportFieldActor::ReturnToSource(const FString& Reason)
{
	if (!Runtime || Runtime->bLandingCommitted || State.Phase == EGuLiTeleportPhase::Finished) { return; }
	if (!Runtime->bCollected) { Finish(Reason); return; }
	State.Phase = EGuLiTeleportPhase::Returning; State.Message = Reason; State.Destination = State.Source;
	if (PlanLanding(State.Source,true) && CommitLanding(true)) { return; }
	Runtime->NextReturnAttempt = GetSynchronizedTime(*GetWorld())+.25; Publish();
}
void AGuLiTeleportFieldActor::Cancel(AGuLiBattlePlayerState& Commander)
{
	if (HasAuthority() && Runtime && Runtime->Commander == &Commander) { ReturnToSource(GuLiGameText::Text(TEXT("UI.TeleportFieldActor.184"))); }
}
void AGuLiTeleportFieldActor::Finish(const FString& Message)
{
	State.FinalProgress = State.Phase == EGuLiTeleportPhase::Windup ? GuLiTeleport::WindupProgress(State,GetSynchronizedTime(*GetWorld())) : 1.f;
	State.Phase = EGuLiTeleportPhase::Finished; State.Message = Message; State.PhaseStartTime = GetSynchronizedTime(*GetWorld());
	State.Deadline = State.PhaseStartTime+.6; Publish();
}
void AGuLiTeleportFieldActor::Publish() { ++State.Revision; ForceNetUpdate(); }
void AGuLiTeleportFieldActor::TickAuthority()
{
	if (!Runtime || bPreview) { return; }
	const double Now = GetSynchronizedTime(*GetWorld());
	const auto* GS = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (State.Phase == EGuLiTeleportPhase::Finished) { if (Now >= State.Deadline) { Destroy(); } return; }
	if (State.Phase == EGuLiTeleportPhase::Recovery)
	{
		if (Now >= State.Deadline) { PruneParticipants(); ApplyParticipants(false,false,false); Finish(State.Message); }
		return;
	}
	if (State.Phase == EGuLiTeleportPhase::Returning)
	{ if (Now >= Runtime->NextReturnAttempt) { ReturnToSource(State.Message); } return; }
	if (!CanCast(Runtime->Commander.Get()) || !GS || GS->GetMatchEpoch() != uint32(State.MatchEpoch) || !GS->IsMatchInProgress())
	{ ReturnToSource(GuLiGameText::Text(TEXT("UI.TeleportFieldActor.185"))); return; }
	if (State.Phase == EGuLiTeleportPhase::Windup && Now >= State.Deadline) { Capture(); }
	else if (State.Phase == EGuLiTeleportPhase::AwaitingDestination && Now >= State.Deadline) { ReturnToSource(GuLiGameText::Text(TEXT("UI.TeleportFieldActor.182"))); }
}
void AGuLiTeleportFieldActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); TickVisuals();
}
void AGuLiTeleportFieldActor::EndPlay(const EEndPlayReason::Type Reason)
{
	FWorldDelegates::OnWorldPostActorTick.Remove(WorldTickHandle);
	if (HasAuthority() && Runtime && Runtime->bCollected && State.Phase != EGuLiTeleportPhase::Finished)
	{
		PruneParticipants();
		if (!Runtime->bLandingCommitted) { for (auto& Unit : Runtime->Units) { Unit.Landing = Unit.Original; } }
		ApplyParticipants(false,false,!Runtime->bLandingCommitted);
	}
	Runtime.Reset(); Super::EndPlay(Reason);
}
