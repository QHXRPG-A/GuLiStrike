#include "Commander/UI/GuLiSceneUISourceRegistry.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Gameplay/Stronghold/GuLiOutpostPresentationComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool UGuLiSceneUISourceRegistry::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}
void UGuLiSceneUISourceRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSceneUI_InitialDiscovery);
	// Exactly one seed per World. Later actors and component rebuilds register through lifecycle hooks.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) { ++DiscoveryIterations; ActorSpawned(*It); }
	SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::ActorSpawned));
}
void UGuLiSceneUISourceRegistry::Deinitialize()
{
	GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
	GetWorld()->GetTimerManager().ClearTimer(PendingTimer);
	for (const auto& Entry : Sources) if (auto* Actor = Cast<AActor>(Entry.Object.Get()))
		Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::ActorEnded);
	Sources.Reset(); Pending.Reset(); Super::Deinitialize();
}
void UGuLiSceneUISourceRegistry::ActorSpawned(AActor* Actor)
{
	if (!IsValid(Actor)) return;
	Actor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::ActorEnded);
	// A pawn is retained even before its replicated identity arrives. The view decides whether to draw it.
	if (Actor->IsA<APawn>()) RegisterSource(Actor, EGuLiSceneUISourceKind::PawnRing);
	if (Actor->IsA<AGuLiCommanderPresentationActor>()) RegisterSource(Actor, EGuLiSceneUISourceKind::Presentation);
	RegisterSource(Actor, EGuLiSceneUISourceKind::OutlineActor);
	Pending.AddUnique(Actor);
	if (!GetWorld()->GetTimerManager().TimerExists(PendingTimer))
		PendingTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::DiscoverPending));
}
void UGuLiSceneUISourceRegistry::DiscoverActor(AActor* Actor)
{
	if (!IsValid(Actor)) return;
	if (auto* Halo = Actor->FindComponentByClass<UGuLiOutpostPresentationComponent>())
		RegisterSource(Halo, EGuLiSceneUISourceKind::OutpostHalo);
}
void UGuLiSceneUISourceRegistry::DiscoverPending()
{
	TArray<TWeakObjectPtr<AActor>> Work = MoveTemp(Pending); Pending.Reset(); PendingTimer.Invalidate();
	for (const auto& Actor : Work) DiscoverActor(Actor.Get());
}
void UGuLiSceneUISourceRegistry::ActorEnded(AActor* Actor, EEndPlayReason::Type)
{
	Sources.RemoveAllSwap([Actor](const FGuLiSceneUISource& S)
	{ const auto* Component = Cast<UActorComponent>(S.Object.Get()); return S.Object.Get() == Actor || (Component && Component->GetOwner() == Actor) || !S.Object.IsValid(); });
	++MembershipRevision; ++ContentRevision; ++PoseRevision;
}
void UGuLiSceneUISourceRegistry::RegisterSource(UObject* Source, const EGuLiSceneUISourceKind Kind)
{
	if (!IsValid(Source) || Source->IsTemplate() || Source->GetWorld() != GetWorld()) return;
	if (Sources.ContainsByPredicate([&](const auto& S) { return S.Object.Get() == Source && S.Kind == Kind; })) return;
	Sources.Add({Source, Kind, NextGeneration++, ++ContentRevision}); ++MembershipRevision;
}
void UGuLiSceneUISourceRegistry::UnregisterSource(const UObject* Source)
{
	if (Sources.RemoveAllSwap([Source](const auto& S) { return S.Object.Get() == Source || !S.Object.IsValid(); }))
	{ ++MembershipRevision; ++ContentRevision; ++PoseRevision; }
}
void UGuLiSceneUISourceRegistry::NotifyChanged(UObject* Source, const EGuLiSceneUIChange Change)
{
	if (!IsValid(Source) || Source->IsTemplate() || Source->GetWorld() != GetWorld()) return;
	if (Change == EGuLiSceneUIChange::Membership) ++MembershipRevision;
	if (Change == EGuLiSceneUIChange::Pose) ++PoseRevision; else ++ContentRevision;
	for (auto& Entry : Sources) if (Entry.Object.Get() == Source) ++Entry.Revision;
}
void UGuLiSceneUISourceRegistry::GetSnapshot(TArray<FGuLiSceneUISource>& Out) const
{
	Out.Reset(Sources.Num()); for (const auto& S : Sources) if (S.Object.IsValid()) Out.Add(S);
}
void UGuLiSceneUISourceRegistry::Notify(UObject* Source, const EGuLiSceneUIChange Change)
{
	if (Source && Source->GetWorld()) if (auto* Registry = Source->GetWorld()->GetSubsystem<UGuLiSceneUISourceRegistry>())
		Registry->NotifyChanged(Source, Change);
}

uint64 UGuLiSceneUISourceRegistry::GetSourceRevision(const UObject* Source) const
{
	for (const auto& Entry : Sources) if (Entry.Object.Get() == Source) return Entry.Revision;
	return 0;
}

FString UGuLiSceneUISourceRegistry::GetRegistryStatsJson() const
{
 return FString::Printf(TEXT("{\"sources\":%d,\"discovery_iterations\":%llu,\"membership_revision\":%llu,\"content_revision\":%llu,\"pose_revision\":%llu}"),
 Sources.Num(), DiscoveryIterations, MembershipRevision, ContentRevision, PoseRevision);
}
