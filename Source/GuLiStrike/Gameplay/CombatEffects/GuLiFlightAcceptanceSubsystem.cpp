#include "Gameplay/CombatEffects/GuLiFlightAcceptanceSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/GuLiStrikeProjectile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "EngineLogs.h"

bool UGuLiFlightAcceptanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode()!=NM_Client;
}
bool UGuLiFlightAcceptanceSubsystem::IsTickable() const { return !IsTemplate() && bRunning && GetWorld(); }
TStatId UGuLiFlightAcceptanceSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiFlightAcceptance,STATGROUP_Tickables); }
void UGuLiFlightAcceptanceSubsystem::Deinitialize() { StopLoad(); Super::Deinitialize(); }

bool UGuLiFlightAcceptanceSubsystem::StartLoad(int32 FlightCount,float Duration)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (bRunning || !GetWorld() || GetWorld()->GetNetMode()==NM_Client || FlightCount<1 || FlightCount>5000
		|| !FMath::IsFinite(Duration) || Duration<1 || Duration>120) return false;
	AActor* Marker = nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) if (It->ActorHasTag(TEXT("FlightEventsQAOrigin"))) { Marker=*It; break; }
	if (!Marker) { UE_LOG(LogNet,Error,TEXT("Flight load requires saved FlightEventsQAOrigin marker in LVL_CommanderMassPrototype")); return false; }
	auto* Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!Ledger || !Ledger->GetMatchEpoch()) return false;
	TArray<FGuLiCombatTargetSnapshot> All, SourceOnly; Ledger->GetTargetSnapshots(All);
	Ledger->GetSourceOnlySnapshots(SourceOnly); All.Append(SourceOnly);
	Sources.SetNum(4);
	const EGuLiTargetKind Kinds[] = {EGuLiTargetKind::CommanderSoldier,EGuLiTargetKind::GroundActor,EGuLiTargetKind::Ship,EGuLiTargetKind::Wingman};
	for (int32 Domain=0; Domain<4; ++Domain)
	{
		const auto* Source = All.FindByPredicate([&](const auto& Item){ return Item.Handle.Kind==Kinds[Domain] && Item.bAlive; });
		if (!Source) { UE_LOG(LogNet,Error,TEXT("Flight load missing live source domain %d; populate Commander, Ground, Ship and Wingman first"),Domain); return false; }
		Sources[Domain]=*Source;
	}
	const auto* Target = All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Handle.Kind==EGuLiTargetKind::Ship && Item.Team!=Sources[3].Team; });
	if (!Target) Target = All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Team!=Sources[3].Team && Item.Handle!=Sources[3].Handle; });
	if (!Target) return false;
	EnemyTarget=Target->Handle;
	ShipClass=LoadClass<AGuLiStrikeProjectile>(nullptr,TEXT("/Game/GuLiStrike/Ship/BP_ShipProjectile.BP_ShipProjectile_C"));
	if (const auto* Catalog=GetDefault<UGuLiCombatEffectSettings>()->Catalog.LoadSynchronous())
		for (const auto& Mount : Catalog->Mounts) if (auto* Definition=Mount.Projectile.LoadSynchronous()) { CurveDefinition=Definition; break; }
	if (!ShipClass || !CurveDefinition || !Sources[2].CollisionActor.IsValid()) return false;
	Origin=Marker->GetActorLocation()+FVector(0,0,3500);
	Desired=FlightCount; FinishTime=GetWorld()->GetTimeSeconds()+Duration; NextRefill=0; Serial=0; Peak=0;
	for (auto& Domain : Flights) Domain.Reset();
	bRunning=true;
	UE_LOG(LogNet,Display,TEXT("Flight acceptance started: requested=%d duration=%.1fs; four server producers, no production cap change"),Desired,Duration);
	return true;
#endif
}

void UGuLiFlightAcceptanceSubsystem::StopLoad()
{
	if (bRunning) UE_LOG(LogNet,Display,TEXT("Flight acceptance stopped: requested=%d peakFixture=%d; existing flights finish under normal authority rules"),Desired,Peak);
	bRunning=false; Sources.Reset(); CurveDefinition=nullptr; ShipClass=nullptr;
	for (auto& Domain : Flights) Domain.Reset();
}

void UGuLiFlightAcceptanceSubsystem::Tick(float DeltaSeconds)
{
	const float Now=GetWorld()->GetTimeSeconds();
	if (Now>=FinishTime) { StopLoad(); return; }
	if (Now<NextRefill) return;
	NextRefill=Now+.1f;
	auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	TArray<FGuLiCombatTargetSnapshot> All, SourceOnly;
	Ledger->GetTargetSnapshots(All); Ledger->GetSourceOnlySnapshots(SourceOnly); All.Append(SourceOnly);
	// A normal respawn changes the source identity. Replenishment uses the new
	// real producer; accepted flights retain their original frozen source lease.
	for (int32 Domain=0; Domain<4; ++Domain)
		if (const auto* Current=All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Handle.Kind==Sources[Domain].Handle.Kind; })) Sources[Domain]=*Current;
	FGuLiCombatTargetSnapshot Target;
	if (!Ledger->TryGetTargetSnapshot(EnemyTarget,Target) || !Target.bAlive || Target.Team==Sources[3].Team)
	{
		const auto* Replacement=All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Handle.Kind==EGuLiTargetKind::Ship && Item.Team!=Sources[3].Team; });
		if (!Replacement) Replacement=All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Team!=Sources[3].Team; });
		if (Replacement) EnemyTarget=Replacement->Handle;
	}
	int32 Total=0;
	for (int32 Domain=0; Domain<4; ++Domain)
	{
		auto& Ids=Flights[Domain];
		Ids.RemoveAll([&](const FGuid& Id){ return !UGuLiCombatEffectReplicationComponent::IsFlightActive(GetWorld(),Id); });
		const int32 Goal=Desired/4+(Domain<Desired%4 ? 1 : 0);
		for (int32 Count=Ids.Num(); Count<Goal; ++Count) if (!SpawnFlight(Domain,Serial++)) break;
		Total+=Ids.Num();
	}
	Peak=FMath::Max(Peak,Total);
}

bool UGuLiFlightAcceptanceSubsystem::SpawnFlight(int32 Domain,int32 Ordinal)
{
	const FVector Position=Origin+FVector((Ordinal%20)*90,(Ordinal/20%20)*90,Domain*120);
	if (Domain==2)
	{
		auto* Source=Sources[Domain].CollisionActor.Get(); if (!Source) return false;
		const FTransform Transform(FRotator(0,0,0),Position);
		auto* Projectile=GetWorld()->SpawnActorDeferred<AGuLiStrikeProjectile>(ShipClass,Transform,Source,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Projectile) return false;
		if (!Projectile->ConfigureServerDamageLedger(*Source,.001f)) { Projectile->Destroy(); return false; }
		Projectile->FinishSpawning(Transform); Flights[Domain].Add(Projectile->GetFlightId()); return true;
	}
	FGuLiCombatEffectContext Context;
	Context.MatchEpoch=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->GetMatchEpoch();
	Context.Source=Sources[Domain].Handle; Context.ShotId=FGuid::NewGuid(); Context.RootEventId=Context.ShotId;
	Context.Damage=.001f; Context.WeaponBinding.SlotId=TEXT("NetworkAcceptance");
	if (Domain==3)
	{
		Context.Target=EnemyTarget;
		const FGuid Id=GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>()->LaunchProjectile(CurveDefinition,Context,FTransform(FRotator(0,0,0),Position));
		if (!Id.IsValid()) return false;
		Flights[Domain].Add(Id); return true;
	}
	FGuLiPooledProjectileLaunch Launch; Launch.Context=Context; Launch.Position=Position;
	Launch.Direction=FVector::ForwardVector; Launch.Speed=6000; Launch.Lifetime=8; Launch.MaximumDistance=48000;
	Launch.ServerTime=GetWorld()->GetTimeSeconds();
	if (!GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>()->Launch(Launch).IsValid()) return false;
	Flights[Domain].Add(Context.ShotId); return true;
}

#if !UE_BUILD_SHIPPING
namespace
{
	void Load(const TArray<FString>& Args,UWorld* World)
	{
		int32 Count=500; float Seconds=30;
		if (!World || (Args.Num()>0 && !LexTryParseString(Count,*Args[0])) || (Args.Num()>1 && !LexTryParseString(Seconds,*Args[1]))) return;
		if (auto* Fixture=World->GetSubsystem<UGuLiFlightAcceptanceSubsystem>()) Fixture->StartLoad(Count,Seconds);
	}
	void Stop(UWorld* World) { if (World) if (auto* Fixture=World->GetSubsystem<UGuLiFlightAcceptanceSubsystem>()) Fixture->StopLoad(); }
	FAutoConsoleCommandWithWorldAndArgs LoadCommand(TEXT("gs.Flights.Load"),TEXT("Explicit server-only four-source load: [500] [30 seconds]. Requires saved map marker and live sources."),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Load));
	FAutoConsoleCommandWithWorld StopCommand(TEXT("gs.Flights.Stop"),TEXT("Stop acceptance replenishment; existing flights retain authority lifetime."),FConsoleCommandWithWorldDelegate::CreateStatic(&Stop));
}
#endif
