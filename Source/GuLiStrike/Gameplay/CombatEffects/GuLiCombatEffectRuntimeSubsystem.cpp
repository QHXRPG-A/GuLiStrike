#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Stats/Stats.h"
#include "Subsystems/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCombatEffects, Log, All);

namespace
{
	constexpr float StepSeconds = 1.0f / 30.0f;
	constexpr float CellSize = 2000.0f;
	FIntPoint Cell(const FVector& Point) { return FIntPoint(static_cast<int32>(FMath::FloorToInt(Point.X / CellSize)), static_cast<int32>(FMath::FloorToInt(Point.Y / CellSize))); }
}

bool UGuLiCombatEffectRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

void UGuLiCombatEffectRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiDamageLedgerSubsystem>();
	Collection.InitializeDependency<UGuLiProjectilePoolSubsystem>();
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	Collection.InitializeDependency<UGuLiSpellFieldDataSubsystem>();
	Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	ProjectilePool = GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();
	if (ProjectilePool) ProjectilePool->OnState.AddUObject(this, &ThisClass::HandlePooledState);
	CommanderData = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
	FieldData = GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
	RegisterAttackExecutor(TEXT("DirectSingleTarget"), [this](const auto& Request, auto& Damage, auto& Cues) { ExecuteDirect(Request, Damage, Cues); });
	RegisterAttackExecutor(TEXT("GroundMachineGun"), [this](const auto& Request, auto& Damage, auto& Cues) { ExecuteGroundMachineGun(Request, Cues); });
	RegisterAttackExecutor(TEXT("LaunchProjectile"), [this](const auto& Request, auto& Damage, auto& Cues) { ExecuteProjectile(Request, Damage, Cues); });
}

void UGuLiCombatEffectRuntimeSubsystem::Deinitialize()
{
	if (ProjectilePool) ProjectilePool->OnState.RemoveAll(this);
	ProjectilePool = nullptr;
	for (const auto& Pair : Effects) if (Ledger) Ledger->ReleaseEffectSource(Pair.Value.SourceLease);
	Effects.Reset(); Executors.Reset(); TargetSnapshots.Reset(); SpatialGrid.Reset(); StepIds.Reset();
	WingmanBurstCooldowns.Reset();
	OnState.Clear(); OnShots.Clear(); OnEpoch.Clear(); Catalog = nullptr; Ledger = nullptr; CommanderData = nullptr; FieldData = nullptr;
	WarnedFieldConfigs.Reset();
	Super::Deinitialize();
}

bool UGuLiCombatEffectRuntimeSubsystem::IsTickable() const { return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_Client; }
TStatId UGuLiCombatEffectRuntimeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiCombatEffectRuntimeSubsystem, STATGROUP_Tickables); }

bool UGuLiCombatEffectRuntimeSubsystem::SynchronizeEpoch()
{
	if (!Ledger || !GetWorld() || GetWorld()->GetNetMode() == NM_Client || Ledger->GetMatchEpoch() == 0) return false;
	if (Epoch != Ledger->GetMatchEpoch())
	{
		for (const auto& Pair : Effects) Ledger->ReleaseEffectSource(Pair.Value.SourceLease);
		Effects.Reset(); TargetSnapshots.Reset(); SpatialGrid.Reset(); StepIds.Reset();
		WingmanBurstCooldowns.Reset();
		Accumulator = 0; bIndexReady = false; Epoch = Ledger->GetMatchEpoch(); Counters = {};
		if (ProjectilePool) ProjectilePool->BeginEpoch(Epoch);
		OnEpoch.Broadcast(Epoch);
	}
	return true;
}

bool UGuLiCombatEffectRuntimeSubsystem::PrepareContext(const FGuLiCombatEffectContext& Input, FGuLiCombatEffectContext& Output)
{
	if (!SynchronizeEpoch() || !Input.Source.IsValid() || !FMath::IsFinite(Input.Damage) || Input.Damage <= 0
		|| (Input.MatchEpoch != 0 && Input.MatchEpoch != Epoch)) return false;
	FGuLiCombatTargetSnapshot Source;
	if (!Ledger->TryGetTargetSnapshot(Input.Source, Source) || !Source.bAlive) return false;
	if (Input.WeaponBinding.IsWellFormed() && (Input.WeaponBinding.MatchEpoch != Epoch || Input.WeaponBinding.Team != Source.Team)) return false;
	Output = Input; Output.MatchEpoch = Epoch;
	if (!Output.ShotId.IsValid()) Output.ShotId = FGuid::NewGuid();
	if (!Output.RootEventId.IsValid()) Output.RootEventId = Output.ShotId;
	return true;
}

bool UGuLiCombatEffectRuntimeSubsystem::ResolveFieldConfig(UGuLiSpellFieldDefinition* Definition,
	const FGuLiCombatEffectContext& Input, FGuLiSpellFieldConfig& OutConfig, FGuLiCombatEffectContext& OutContext)
{
	if (!Definition) return false;
	OutContext = Input;
	// Weapon-authored field references select gameplay data; the asset supplies visuals
	// and a default for callers that do not carry a weapon field reference.
	const FName ConfigId = Input.EffectConfigId.IsNone() ? Definition->ConfigId : Input.EffectConfigId;
	if (ConfigId.IsNone())
	{
		// Transient tests and prototypes may use the explicit inline fallback. Project gameplay assets use a table row.
		OutConfig.ConfigId = TEXT("InlineFallback"); OutConfig.Damage = Input.Damage;
		OutConfig.Radius = Definition->Radius; OutConfig.Timing = Definition->Timing;
		OutConfig.Delay = Definition->Delay; OutConfig.Duration = Definition->Timing == EGuLiSpellFieldTiming::Periodic ? Definition->Duration : 0.0f;
		OutConfig.PulseInterval = Definition->PulseInterval; OutConfig.DissipationSeconds = Definition->DissipationSeconds;
		return Definition->IsValidDefinition() && FMath::IsFinite(Input.Damage) && Input.Damage > 0.0f;
	}
	const FGuLiSpellFieldConfig* Config = FieldData && FieldData->IsCatalogValid()
		? FieldData->FindCombatField(ConfigId) : nullptr;
	if (!Config || !Config->IsValid())
	{
		if (!WarnedFieldConfigs.Contains(ConfigId))
		{
			UE_LOG(LogGuLiCombatEffects, Error, TEXT("Spell field '%s' requires a valid SpellFields row; creation was rejected."), *ConfigId.ToString());
			WarnedFieldConfigs.Add(ConfigId);
		}
		return false;
	}
	// A weapon caller already applied its skill modifiers to the selected row's base damage.
	if (Input.EffectConfigId.IsNone())
	{
		OutContext.Damage = Config->Damage;
	}
	OutConfig = *Config;
	return true;
}

UGuLiCombatEffectCatalog* UGuLiCombatEffectRuntimeSubsystem::GetCatalog()
{
	if (!Catalog) Catalog = GetDefault<UGuLiCombatEffectSettings>()->Catalog.LoadSynchronous();
	if (!Catalog && !bCatalogWarning)
	{
		UE_LOG(LogGuLiCombatEffects, Warning, TEXT("Combat effect catalog unavailable; hitscan damage remains active, unconfigured visual/projectile work is suppressed."));
		bCatalogWarning = true;
	}
	return Catalog;
}

bool UGuLiCombatEffectRuntimeSubsystem::RegisterAttackExecutor(FName Id, FAttackExecutor Executor)
{
	if (Id.IsNone() || !Executor || Executors.Contains(Id)) return false;
	Executors.Add(Id, MoveTemp(Executor)); return true;
}

void UGuLiCombatEffectRuntimeSubsystem::ExecuteAttackBatch(TConstArrayView<FGuLiCombatAttackRequest> Requests)
{
	if (!SynchronizeEpoch()) return;
	TArray<FGuLiDamageRequest> Damage;
	TArray<FGuLiCombatShotCue> Cues;
	Damage.Reserve(Requests.Num()); Cues.Reserve(Requests.Num());
	for (const FGuLiCombatAttackRequest& Input : Requests)
	{
		FAttackExecutor* Executor = Executors.Find(Input.ExecutorId);
		if (!Executor) continue;
		FGuLiCombatAttackRequest Request = Input;
		if (PrepareContext(Input.Context, Request.Context)) (*Executor)(Request, Damage, Cues);
	}
	// All source/target fire decisions precede any health mutation, preserving the existing batch semantics.
	for (const FGuLiDamageRequest& Request : Damage)
	{
		if (Ledger->CommitDamage(Request).Status == EGuLiDamageCommitStatus::Committed) ++Counters.DamageCommits;
	}
	if (!Cues.IsEmpty()) { Counters.ShotsPublished += Cues.Num(); OnShots.Broadcast(Cues); }
}

FGuLiDamageRequest UGuLiCombatEffectRuntimeSubsystem::MakeDamage(const FGuLiCombatEffectContext& Context,
	FGuid DamageId, FGuLiTargetHandle Target, FVector Location)
{
	FGuLiDamageRequest Result;
	Result.MatchEpoch = Context.MatchEpoch; Result.DamageEventId = DamageId; Result.ShotId = Context.ShotId;
	Result.RootEventId = Context.RootEventId; Result.Source = Context.Source; Result.Target = Target;
	Result.Emitter = Context.Emitter;
	Result.WeaponBinding = Context.WeaponBinding; Result.SkillId = Context.SkillId;
	Result.ProfileRevision = Context.ProfileRevision; Result.LoadoutRevision = Context.LoadoutRevision;
	Result.Damage = Context.Damage; Result.HitLocation = Location;
	return Result;
}

void UGuLiCombatEffectRuntimeSubsystem::ExecuteDirect(const FGuLiCombatAttackRequest& Request,
	TArray<FGuLiDamageRequest>& OutDamage, TArray<FGuLiCombatShotCue>& OutCues)
{
	FGuLiCombatTargetSnapshot Target;
	if (!Ledger->TryGetTargetSnapshot(Request.Context.Target, Target) || !Target.bAlive) return;
	OutDamage.Add(MakeDamage(Request.Context, Request.Context.ShotId, Request.Context.Target, Target.Location));
	const FGuLiWeaponMountConfig* Mount = CommanderData && CommanderData->IsWeaponMountCatalogValid()
		? CommanderData->FindWeaponMountConfig(Request.UnitTypeId, Request.Context.WeaponBinding.SlotId) : nullptr;
	if (!Mount || !Mount->IsValid()) return;
	FGuLiCombatShotCue& Cue = OutCues.AddDefaulted_GetRef();
	Cue.MatchEpoch = Epoch; Cue.ShotId = Request.Context.ShotId; Cue.Source = Request.Context.Source; Cue.Target = Request.Context.Target;
	Cue.UnitTypeId = Request.UnitTypeId; Cue.SlotId = Request.Context.WeaponBinding.SlotId;
	Cue.MuzzleIndex = static_cast<uint8>(Request.ShotOrdinal % Mount->Muzzles.Num());
	Cue.MuzzleOffset = Mount->Muzzles[Cue.MuzzleIndex];
	Cue.Start = Request.SourceTransform.TransformPosition(Cue.MuzzleOffset);
	Cue.End = Target.Location; Cue.ServerTime = GetWorld()->GetTimeSeconds();
}

void UGuLiCombatEffectRuntimeSubsystem::ExecuteGroundMachineGun(const FGuLiCombatAttackRequest& Request,
	TArray<FGuLiCombatShotCue>& OutCues)
{
	if (!ProjectilePool || Request.Context.Source.Kind != EGuLiTargetKind::CommanderSoldier
		|| Request.Context.Emitter.IsValid() || Request.SourceTransform.ContainsNaN()) return;
	FGuLiCombatTargetSnapshot Target;
	if (!Ledger->TryGetTargetSnapshot(Request.Context.Target, Target) || !Target.bAlive) return;
	const FGuLiWeaponMountConfig* Mount = CommanderData && CommanderData->IsWeaponMountCatalogValid()
		? CommanderData->FindWeaponMountConfig(Request.UnitTypeId, Request.Context.WeaponBinding.SlotId) : nullptr;
	if (!Mount || !Mount->IsValid()) return;
	const uint8 MuzzleIndex = static_cast<uint8>(Request.ShotOrdinal % Mount->Muzzles.Num());
	FGuLiPooledProjectileLaunch Launch;
	Launch.Context = Request.Context; Launch.MuzzleOffset = Mount->Muzzles[MuzzleIndex];
	Launch.Position = Request.SourceTransform.TransformPosition(Launch.MuzzleOffset);
	Launch.Direction = (Target.Location - Launch.Position).GetSafeNormal();
	Launch.Speed = Request.Motion.Speed; Launch.Lifetime = Request.Motion.MaximumLifetime;
	// Acquisition range only gates firing. Once launched, the frozen lifetime governs straight flight.
	Launch.MaximumDistance = Launch.Speed * Launch.Lifetime;
	Launch.SweepRadius = Request.Motion.SweepRadius; Launch.ServerTime = GetWorld()->GetTimeSeconds();
	if (!ProjectilePool->Launch(Launch).IsValid()) return;
	FGuLiCombatShotCue& Cue = OutCues.AddDefaulted_GetRef();
	Cue.MatchEpoch = Epoch; Cue.ShotId = Request.Context.ShotId;
	Cue.Source = Request.Context.Source; Cue.Target = Request.Context.Target;
	Cue.UnitTypeId = Request.UnitTypeId; Cue.SlotId = Request.Context.WeaponBinding.SlotId;
	Cue.MuzzleIndex = MuzzleIndex; Cue.MuzzleOffset = Launch.MuzzleOffset;
	Cue.Start = Launch.Position; Cue.End = Target.Location; Cue.ServerTime = Launch.ServerTime;
	Cue.bMuzzleOnly = true;
}

void UGuLiCombatEffectRuntimeSubsystem::ExecuteProjectile(const FGuLiCombatAttackRequest& Request,
	TArray<FGuLiDamageRequest>& OutDamage, TArray<FGuLiCombatShotCue>& OutCues)
{
	const FGuLiWeaponMountConfig* Mount = CommanderData && CommanderData->IsWeaponMountCatalogValid()
		? CommanderData->FindWeaponMountConfig(Request.UnitTypeId, Request.Context.WeaponBinding.SlotId) : nullptr;
	const UGuLiCombatEffectCatalog* Definitions = GetCatalog();
	const FGuLiWeaponEffectMount* Visual = Definitions
		? Definitions->FindMount(Request.UnitTypeId, Request.Context.WeaponBinding.SlotId) : nullptr;
	if (!Mount || !Mount->IsValid() || !Visual) return;
	UGuLiProjectileEffectDefinition* Definition = Visual->Projectile.LoadSynchronous();
	const FVector Start = Request.SourceTransform.TransformPosition(Mount->Muzzles[Request.ShotOrdinal % Mount->Muzzles.Num()]);
	const FVector Direction = (Request.TargetLocation - Start).GetSafeNormal2D(UE_SMALL_NUMBER, Request.SourceTransform.GetUnitAxis(EAxis::X));
	LaunchProjectile(Definition, Request.Context, FTransform(Direction.Rotation(), Start));
}

FGuid UGuLiCombatEffectRuntimeSubsystem::LaunchProjectile(UGuLiProjectileEffectDefinition* Definition,
	const FGuLiCombatEffectContext& Context, const FTransform& LaunchTransform)
{
	if (!Definition || !Definition->IsValidDefinition() || LaunchTransform.ContainsNaN()) return {};
	FGuLiProjectileMotionSettings AuthoredMotion;
	if (!Definition->ResolveMotionSettings(AuthoredMotion)) return {};
	UGuLiSpellFieldDefinition* Field = Definition->ImpactField.LoadSynchronous();
	FGuLiSpellFieldConfig FieldConfig; FGuLiCombatEffectContext Configured, Prepared;
	if (!Field || !ResolveFieldConfig(Field, Context, FieldConfig, Configured) || !PrepareContext(Configured, Prepared)) return {};
	FGuLiCombatTargetSnapshot Target;
	if (!Ledger->TryGetTargetSnapshot(Prepared.Target, Target) || !Target.bAlive) return {};
	const FGuid Lease = Ledger->AcquireEffectSource(Prepared.Source);
	if (!Lease.IsValid()) return {};
	FGuLiRuntimeCombatEffect Instance;
	Instance.Context = Prepared; Instance.SourceLease = Lease; Instance.Projectile = Definition; Instance.Field = Field;
	Instance.Timing = FieldConfig.Timing; Instance.Interval = FieldConfig.PulseInterval;
	Instance.Duration = FieldConfig.Timing == EGuLiSpellFieldTiming::Periodic ? FieldConfig.Duration : 0.0f;
	Instance.FadeSeconds = FieldConfig.DissipationSeconds; Instance.FrozenRadius = FieldConfig.Radius;
	Instance.FrozenDelay = FieldConfig.Delay; Instance.VariantCount = Field->ActivationVariants.Num();
	auto& State = Instance.State;
	State.MatchEpoch = Epoch; State.EffectId = FGuid::NewGuid(); State.Source = Prepared.Source; State.Target = Prepared.Target;
	State.ProjectileDefinition = Definition; State.FieldDefinition = Field; State.Motion = AuthoredMotion;
	State.Location = LaunchTransform.GetLocation(); State.LaunchLocation = State.Location;
	State.LaunchDirection = LaunchTransform.GetUnitAxis(EAxis::X); State.Velocity = FVector(State.LaunchDirection) * State.Motion.Speed;
	State.LastTargetLocation = Target.Location; State.RandomSeed = static_cast<int32>(State.EffectId.A ^ State.EffectId.C);
	State.StartTime = State.SampleTime = State.ActivationTime = GetWorld()->GetTimeSeconds();
	State.EndTime = State.StartTime + State.Motion.MaximumLifetime; State.Sequence = 1;
	Instance.LastCorrection = State.StartTime;
	const FGuid Id = State.EffectId;
	Effects.Add(Id, MoveTemp(Instance)); ++Counters.ProjectilesLaunched;
	Publish(Id, true); return Id;
}

bool UGuLiCombatEffectRuntimeSubsystem::ExecuteWingmanAttack(const FGuLiCombatAttackRequest& Request)
{
    if (!SynchronizeEpoch() || !Request.Context.Emitter.IsValid()) return false;
    if (Request.ExecutorId == TEXT("WingmanGroundMissile")) return LaunchPointProjectile(Request).IsValid();
    if (Request.ExecutorId != TEXT("WingmanMachineGun")) return false;
    FGuLiCombatEffectContext Prepared;
    FGuLiCombatTargetSnapshot Target;
    if (!PrepareContext(Request.Context, Prepared) || !Ledger->TryGetTargetSnapshot(Prepared.Target, Target) || !Target.bAlive) return false;
    if (!ProjectilePool) return false;
    FGuLiPooledProjectileLaunch Launch;
    Launch.Context = Prepared; Launch.MuzzleOffset = Request.MuzzleOffset;
    Launch.Position = Request.SourceTransform.TransformPosition(Request.MuzzleOffset);
    Launch.Direction = (Target.Location - Launch.Position).GetSafeNormal();
    Launch.Speed = Request.Motion.Speed; Launch.Lifetime = Request.Motion.MaximumLifetime;
    Launch.SweepRadius = Request.Motion.SweepRadius; Launch.MaximumDistance = Request.MaximumTravelDistance;
    Launch.ServerTime = GetWorld()->GetTimeSeconds();
    return ProjectilePool->Launch(Launch).IsValid();
}

FGuid UGuLiCombatEffectRuntimeSubsystem::StartWingmanGunBurst(
	const FGuLiCombatAttackRequest& Request, const float ShotIntervalSeconds,
	const float DurationSeconds, const float StopDistanceCentimeters,
	const float OrbitRadiusCentimeters, const float CooldownSeconds)
{
	if (!SynchronizeEpoch() || Request.ExecutorId != TEXT("WingmanMachineGun")
		|| !Request.Context.Emitter.IsValid() || Request.Context.WeaponBinding.SlotId.IsNone()
		|| Request.SourceTransform.ContainsNaN() || Request.TargetLocation.ContainsNaN()
		|| Request.MuzzleOffset.ContainsNaN()
		|| !ProjectilePool || !FMath::IsFinite(Request.Motion.Speed) || Request.Motion.Speed <= 0
		|| !FMath::IsFinite(Request.Motion.MaximumLifetime) || Request.Motion.MaximumLifetime < 0.01f
		|| !FMath::IsFinite(Request.Motion.SweepRadius) || Request.Motion.SweepRadius < 0
		|| !FMath::IsFinite(Request.MaximumTravelDistance) || Request.MaximumTravelDistance <= 0
		|| !FMath::IsFinite(ShotIntervalSeconds)
		|| ShotIntervalSeconds < GuLiWingmanAttack::MinimumAirShotIntervalSeconds
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f
		|| !FMath::IsFinite(StopDistanceCentimeters) || StopDistanceCentimeters <= 0.0f
		|| !FMath::IsFinite(OrbitRadiusCentimeters) || OrbitRadiusCentimeters <= 0.0f
		|| !FMath::IsFinite(CooldownSeconds) || CooldownSeconds <= 0.0f)
	{
		return {};
	}
	const float Now = GetWorld()->GetTimeSeconds();
	StepWingmanBurstCooldowns(Now);
	if (const auto* BySlot = WingmanBurstCooldowns.Find(Request.Context.Emitter);
		BySlot && BySlot->Contains(Request.Context.WeaponBinding.SlotId))
	{
		return {};
	}
	for (const auto& Pair : Effects)
	{
		if (Pair.Value.State.Kind == EGuLiCombatEffectKind::SustainedHitscan
			&& Pair.Value.Context.Emitter == Request.Context.Emitter
			&& Pair.Value.State.SlotId == Request.Context.WeaponBinding.SlotId)
		{
			return {};
		}
	}

	FGuLiCombatEffectContext Prepared;
	FGuLiCombatTargetSnapshot Emitter;
	FGuLiCombatTargetSnapshot Target;
	const FGuLiTargetHandle EmitterHandle = GuLiCombatTargets::MakeWingmanTargetHandle(Request.Context.Emitter);
	if (!PrepareContext(Request.Context, Prepared)
		|| !Ledger->TryGetTargetSnapshot(EmitterHandle, Emitter) || !Emitter.bAlive
		|| !Ledger->TryGetTargetSnapshot(Prepared.Target, Target) || !Target.bAlive)
	{
		return {};
	}
	const FGuid Lease = Ledger->AcquireEffectSource(Prepared.Source);
	if (!Lease.IsValid()) return {};
	if (Effects.Contains(Prepared.ShotId))
	{
		Ledger->ReleaseEffectSource(Lease);
		return {};
	}

	FGuLiRuntimeCombatEffect Instance;
	Instance.Context = Prepared;
	Instance.SourceLease = Lease;
	Instance.Interval = ShotIntervalSeconds;
	Instance.Duration = DurationSeconds;
	Instance.StopDistance = StopDistanceCentimeters;
	Instance.OrbitRadius = OrbitRadiusCentimeters;
	Instance.CooldownSeconds = CooldownSeconds;
	Instance.GunSpeed = Request.Motion.Speed; Instance.GunLifetime = Request.Motion.MaximumLifetime;
	Instance.GunSweepRadius = Request.Motion.SweepRadius; Instance.GunRange = Request.MaximumTravelDistance;
	auto& State = Instance.State;
	State.Kind = EGuLiCombatEffectKind::SustainedHitscan;
	State.MatchEpoch = Epoch;
	State.EffectId = Prepared.ShotId;
	State.Source = EmitterHandle;
	State.Target = Prepared.Target;
	State.SlotId = Prepared.WeaponBinding.SlotId;
	State.MuzzleOffset = Request.MuzzleOffset;
	State.FireRateHz = 1.0f / ShotIntervalSeconds;
	State.Location = Request.SourceTransform.TransformPosition(Request.MuzzleOffset);
	State.LaunchLocation = State.Location;
	State.LastTargetLocation = Target.Location;
	State.StartTime = State.SampleTime = State.ActivationTime = Now;
	State.EndTime = Now + DurationSeconds;
	State.Sequence = 1;
	const FGuid Id = State.EffectId;
	Effects.Add(Id, MoveTemp(Instance));
	++Counters.GunBurstsStarted;
	Publish(Id, true);
	StepSustainedHitscan(Id, Now);
	return Id;
}

int32 UGuLiCombatEffectRuntimeSubsystem::CancelWingmanGunBurst(
	const FGuLiWingmanHandle& Emitter, const bool bClearCooldown)
{
	if (!SynchronizeEpoch() || !Emitter.IsValid()) return 0;
	TArray<FGuid> Matches;
	for (const auto& Pair : Effects)
	{
		if (Pair.Value.State.Kind == EGuLiCombatEffectKind::SustainedHitscan
			&& Pair.Value.Context.Emitter == Emitter)
		{
			Matches.Add(Pair.Key);
		}
	}
	const float Now = GetWorld()->GetTimeSeconds();
	for (const FGuid& Id : Matches) Finish(Id, EGuLiCombatEffectEndReason::Cancelled, Now);
	if (bClearCooldown) WingmanBurstCooldowns.Remove(Emitter);
	return Matches.Num();
}

FGuid UGuLiCombatEffectRuntimeSubsystem::LaunchPointProjectile(const FGuLiCombatAttackRequest& Request)
{
    UGuLiProjectileEffectDefinition* Definition = Request.Projectile;
    FGuLiCombatEffectContext Prepared;
    if (!SynchronizeEpoch() || !Definition || !Definition->IsValidDefinition() || !Request.FrozenField.IsValid()
        || !Request.Motion.IsValid() || Request.SourceTransform.ContainsNaN() || Request.TargetLocation.ContainsNaN()
        || (Request.GroundWarningStyle && !Request.GroundWarningStyle->IsValidStyle())
        || !PrepareContext(Request.Context, Prepared)) return {};
    UGuLiSpellFieldDefinition* Field = Definition->ImpactField.LoadSynchronous();
    if (!Field || !Field->IsValidDefinition()) return {};
    const FName ConfigId = Prepared.EffectConfigId.IsNone() ? Field->ConfigId : Prepared.EffectConfigId;
    if (!ConfigId.IsNone() && ConfigId != Request.FrozenField.ConfigId) return {};
    const FGuid Lease = Ledger->AcquireEffectSource(Prepared.Source);
    if (!Lease.IsValid()) return {};
    FGuLiRuntimeCombatEffect Instance;
    Instance.Context = Prepared; Instance.SourceLease = Lease; Instance.Projectile = Definition; Instance.Field = Field;
    Instance.GroundWarningStyle = Request.GroundWarningStyle;
    Instance.Timing = Request.FrozenField.Timing; Instance.Interval = Request.FrozenField.PulseInterval;
    Instance.Duration = Request.FrozenField.Timing == EGuLiSpellFieldTiming::Periodic ? Request.FrozenField.Duration : 0.0f;
    Instance.FrozenDelay = Request.FrozenField.Delay;
    Instance.FadeSeconds = Request.FrozenField.DissipationSeconds;
    Instance.FrozenRadius = Request.FrozenField.Radius; Instance.VariantCount = Field->ActivationVariants.Num();
    auto& State = Instance.State;
    State.MatchEpoch = Epoch; State.EffectId = Prepared.ShotId; State.Source = Prepared.Source; State.Target = Prepared.Target;
    State.ProjectileDefinition = Definition; State.FieldDefinition = Field; State.Motion = Request.Motion;
    State.bFixedPoint = true;
    if (!Request.bUseAuthoredPointTrajectory) { State.Motion.LiftSeconds = 0; State.Motion.LateralOffset = 0; }
    State.GroundWarningStyle = Request.GroundWarningStyle;
    State.Radius = Request.FrozenField.Radius;
    State.Location = Request.SourceTransform.TransformPosition(Request.MuzzleOffset); State.LaunchLocation = State.Location;
    State.LastTargetLocation = Request.TargetLocation;
    State.LaunchDirection = Request.bUseAuthoredPointTrajectory
        ? (Request.TargetLocation - FVector(State.Location)).GetSafeNormal2D(UE_SMALL_NUMBER, Request.SourceTransform.GetUnitAxis(EAxis::X))
        : (Request.TargetLocation - FVector(State.Location)).GetSafeNormal();
    State.Velocity = FVector(State.LaunchDirection) * State.Motion.Speed;
    State.StartTime = State.SampleTime = GetWorld()->GetTimeSeconds();
    State.EndTime = State.StartTime + State.Motion.MaximumLifetime; State.Sequence = 1;
    State.RandomSeed = static_cast<int32>(State.EffectId.A ^ State.EffectId.B);
    const FGuid Id = State.EffectId;
    if (Effects.Contains(Id)) { Ledger->ReleaseEffectSource(Lease); return {}; }
    Effects.Add(Id, MoveTemp(Instance)); ++Counters.ProjectilesLaunched; Publish(Id, true);
    return Id;
}

FGuid UGuLiCombatEffectRuntimeSubsystem::CreateSpellField(UGuLiSpellFieldDefinition* Definition,
	const FGuLiCombatEffectContext& Context, FVector Location)
{
	FGuLiSpellFieldConfig Config; FGuLiCombatEffectContext Configured, Prepared;
	if (!Definition || Location.ContainsNaN() || !ResolveFieldConfig(Definition, Context, Config, Configured)
		|| !PrepareContext(Configured, Prepared)) return {};
	const FGuid Lease = Ledger->AcquireEffectSource(Prepared.Source);
	if (!Lease.IsValid()) return {};
	return CreateFieldInternal(Definition, Prepared, Location, Lease, nullptr, &Config);
}

FGuid UGuLiCombatEffectRuntimeSubsystem::CreateFieldInternal(UGuLiSpellFieldDefinition* Definition,
	const FGuLiCombatEffectContext& Context, FVector Location, const FGuid& Lease,
	const FGuLiRuntimeCombatEffect* Frozen, const FGuLiSpellFieldConfig* Config)
{
	FGuLiRuntimeCombatEffect Instance;
	Instance.Context = Context; Instance.SourceLease = Lease; Instance.Field = Definition;
	Instance.Timing = Frozen ? Frozen->Timing : (Config ? Config->Timing : Definition->Timing);
	Instance.Interval = Frozen ? Frozen->Interval : (Config ? Config->PulseInterval : Definition->PulseInterval);
	Instance.Duration = Frozen ? Frozen->Duration : (Config ? (Config->Timing == EGuLiSpellFieldTiming::Periodic ? Config->Duration : 0.0f)
		: (Definition->Timing == EGuLiSpellFieldTiming::Periodic ? Definition->Duration : 0.0f));
	Instance.FadeSeconds = Frozen ? Frozen->FadeSeconds : (Config ? Config->DissipationSeconds : Definition->DissipationSeconds);
	Instance.FrozenRadius = Frozen ? Frozen->FrozenRadius : (Config ? Config->Radius : Definition->Radius);
	Instance.FrozenDelay = Frozen ? Frozen->FrozenDelay : (Config ? Config->Delay : Definition->Delay);
	Instance.VariantCount = Frozen ? Frozen->VariantCount : Definition->ActivationVariants.Num();
	auto& State = Instance.State;
	State.Kind = EGuLiCombatEffectKind::SpellField; State.MatchEpoch = Epoch; State.EffectId = FGuid::NewGuid();
	State.Source = Context.Source; State.Target = Context.Target; State.FieldDefinition = Definition;
	State.Location = Location; State.LaunchLocation = Location; State.LastTargetLocation = Location;
	State.RandomSeed = static_cast<int32>(State.EffectId.A ^ State.EffectId.B);
	FRandomStream Random(State.RandomSeed);
	State.VariantIndex = Instance.VariantCount == 0 ? INDEX_NONE : Random.RandRange(0, Instance.VariantCount - 1);
	State.Radius = Instance.FrozenRadius; State.StartTime = State.SampleTime = GetWorld()->GetTimeSeconds();
	State.ActivationTime = State.StartTime + (Instance.Timing == EGuLiSpellFieldTiming::Instant ? 0.0f : Instance.FrozenDelay);
	State.EndTime = State.ActivationTime + Instance.Duration;
	State.Phase = State.ActivationTime > State.StartTime ? EGuLiCombatEffectPhase::Waiting : EGuLiCombatEffectPhase::Active;
	State.Sequence = 1;
	const FGuid Id = State.EffectId; const float Now = State.StartTime;
	Effects.Add(Id, MoveTemp(Instance)); ++Counters.FieldsCreated; Publish(Id, true);
	if (!bInsideStep) bIndexReady = false;
	StepField(Id, Now);
	return Id;
}

void UGuLiCombatEffectRuntimeSubsystem::Tick(float DeltaTime)
{
	if (!SynchronizeEpoch() || (Effects.IsEmpty() && WingmanBurstCooldowns.IsEmpty() && (!ProjectilePool || ProjectilePool->GetActiveCount() == 0))
		|| !FMath::IsFinite(DeltaTime) || DeltaTime <= 0) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCombatEffects_Runtime);
	const double Started = FPlatformTime::Seconds();
	Accumulator = FMath::Min(Accumulator + DeltaTime, static_cast<double>(StepSeconds * 4));
	while (Accumulator + 1.e-8 >= StepSeconds)
	{
		Accumulator -= StepSeconds;
		const float Now = GetWorld()->GetTimeSeconds() - static_cast<float>(Accumulator);
		bInsideStep = true; bIndexReady = false;
		Effects.GenerateKeyArray(StepIds);
		for (const FGuid Id : StepIds)
		{
			const auto* Instance = Effects.Find(Id);
			if (!Instance) continue;
			if (Instance->State.Kind == EGuLiCombatEffectKind::Projectile) StepProjectile(Id, StepSeconds, Now);
			else if (Instance->State.Kind == EGuLiCombatEffectKind::SpellField) StepField(Id, Now);
			else StepSustainedHitscan(Id, Now);
		}
		StepWingmanBurstCooldowns(Now);
		if (ProjectilePool) ProjectilePool->Step(Now);
		bInsideStep = false;
	}
	Counters.LastStepMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
}

void UGuLiCombatEffectRuntimeSubsystem::StepSustainedHitscan(const FGuid& Id, const float Now)
{
	const FGuLiRuntimeCombatEffect* Found = Effects.Find(Id);
	if (!Found || Now < Found->State.StartTime) return;
	const FGuLiRuntimeCombatEffect Instance = *Found;
	FGuLiCombatTargetSnapshot Emitter;
	FGuLiCombatTargetSnapshot Target;
	if (!Ledger->TryGetTargetSnapshot(Instance.State.Source, Emitter) || !Emitter.bAlive
		|| !Ledger->TryGetTargetSnapshot(Instance.State.Target, Target) || !Target.bAlive
		|| FVector::Distance(Emitter.Location, Target.Location) < Instance.StopDistance)
	{
		Finish(Id, EGuLiCombatEffectEndReason::Cancelled, Now);
		return;
	}

	FGuLiRuntimeCombatEffect* Live = Effects.Find(Id);
	if (!Live) return;
	Live->State.Location = FTransform(Emitter.Rotation, Emitter.Location).TransformPosition(Live->State.MuzzleOffset);
	Live->State.LastTargetLocation = Target.Location;
	Live->State.SampleTime = FMath::Max(Now, Live->State.StartTime);
	++Live->State.Sequence;
	const int32 Due = GuLiWingmanAttack::AirBurstShotsDue(
		Instance.Interval, Instance.Duration, Now - Instance.State.StartTime);
	if (Due > Instance.DeliveredPulses)
	{
		const int32 LogicalShots = Due - Instance.DeliveredPulses;
		// Advance the logical cursor before damage callbacks so re-entrant work cannot replay shots.
		Live->DeliveredPulses = Due;
		Counters.LogicalGunShots += LogicalShots;
		for (int32 Ordinal = Instance.DeliveredPulses; Ordinal < Due; ++Ordinal)
		{
			FGuLiPooledProjectileLaunch Launch;
			Launch.Context = Instance.Context;
			Launch.Context.ShotId = GuLiCombatEffects::DamageId(Id, Ordinal, Instance.State.Target);
			Launch.SourceLease = Instance.SourceLease; Launch.ServerTime = Now;
			Launch.MuzzleOffset = Instance.State.MuzzleOffset;
			Launch.Position = FTransform(Emitter.Rotation, Emitter.Location).TransformPosition(Launch.MuzzleOffset);
			Launch.Direction = (Target.Location - Launch.Position).GetSafeNormal();
			Launch.Speed = Instance.GunSpeed; Launch.Lifetime = Instance.GunLifetime;
			Launch.SweepRadius = Instance.GunSweepRadius; Launch.MaximumDistance = Instance.GunRange;
			if (!ProjectilePool || !ProjectilePool->Launch(Launch).IsValid())
			{ Finish(Id, EGuLiCombatEffectEndReason::Cancelled, Now); return; }
		}
	}
	if (Effects.Contains(Id) && Now >= Instance.State.EndTime)
	{
		Finish(Id, EGuLiCombatEffectEndReason::Completed, Now);
	}
}

void UGuLiCombatEffectRuntimeSubsystem::BeginWingmanBurstCooldown(const FGuLiRuntimeCombatEffect& Instance)
{
	if (!Instance.Context.Emitter.IsValid() || Instance.State.SlotId.IsNone()
		|| Instance.CooldownSeconds <= 0.0f)
	{
		return;
	}
	FGuLiWingmanBurstCooldown& Cooldown =
		WingmanBurstCooldowns.FindOrAdd(Instance.Context.Emitter).FindOrAdd(Instance.State.SlotId);
	Cooldown.Carrier = Instance.Context.Source;
	Cooldown.OrbitRadius = Instance.OrbitRadius;
	Cooldown.DurationSeconds = Instance.CooldownSeconds;
	Cooldown.OrbitEntryTime = -1.0;
}

void UGuLiCombatEffectRuntimeSubsystem::StepWingmanBurstCooldowns(const float Now)
{
	TArray<FGuLiWingmanHandle> Emitters;
	WingmanBurstCooldowns.GenerateKeyArray(Emitters);
	for (const FGuLiWingmanHandle& EmitterHandle : Emitters)
	{
		TMap<FName, FGuLiWingmanBurstCooldown>* BySlot = WingmanBurstCooldowns.Find(EmitterHandle);
		if (!BySlot) continue;
		FGuLiCombatTargetSnapshot Emitter;
		if (!Ledger->TryGetTargetSnapshot(GuLiCombatTargets::MakeWingmanTargetHandle(EmitterHandle), Emitter)
			|| !Emitter.bAlive)
		{
			WingmanBurstCooldowns.Remove(EmitterHandle);
			continue;
		}
		TArray<FName> Slots;
		BySlot->GenerateKeyArray(Slots);
		for (const FName Slot : Slots)
		{
			FGuLiWingmanBurstCooldown* Cooldown = BySlot->Find(Slot);
			FGuLiCombatTargetSnapshot Carrier;
			if (!Cooldown || !Ledger->TryGetTargetSnapshot(Cooldown->Carrier, Carrier) || !Carrier.bAlive)
			{
				BySlot->Remove(Slot);
				continue;
			}
			if (Cooldown->OrbitEntryTime < 0.0)
			{
				if (FVector::Distance(Emitter.Location, Carrier.Location) <= Cooldown->OrbitRadius)
					Cooldown->OrbitEntryTime = Now;
			}
			else if (Now - Cooldown->OrbitEntryTime >= Cooldown->DurationSeconds)
			{
				BySlot->Remove(Slot);
			}
		}
		if (BySlot->IsEmpty()) WingmanBurstCooldowns.Remove(EmitterHandle);
	}
}

void UGuLiCombatEffectRuntimeSubsystem::StepProjectile(const FGuid& Id, float DeltaTime, float Now)
{
	const auto* Found = Effects.Find(Id); if (!Found) return;
	// Callbacks may add/remove entries. Never retain references into Effects over a callback or child spawn.
	FGuLiRuntimeCombatEffect Instance = *Found;
	auto& State = Instance.State;
	if (Now < State.StartTime) return;
	if (Now >= State.EndTime) { Finish(Id, EGuLiCombatEffectEndReason::Expired, Now); return; }
	FGuLiCombatTargetSnapshot Target;
	const bool bTargetAlive = !State.bFixedPoint && Ledger->TryGetTargetSnapshot(State.Target, Target) && Target.bAlive;
	if (bTargetAlive) State.LastTargetLocation = Target.Location;
	const FVector Previous = State.Location;
	GuLiCombatEffects::AdvanceProjectile(State, Now - State.StartTime, DeltaTime);
	State.SampleTime = Now; ++State.Sequence;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiCombatProjectile), false);
	FGuLiCombatTargetSnapshot Source;
	if (Ledger->TryGetTargetSnapshot(State.Source, Source) && Source.CollisionActor.IsValid()) Params.AddIgnoredActor(Source.CollisionActor.Get());
	if (Target.CollisionActor.IsValid()) Params.AddIgnoredActor(Target.CollisionActor.Get());
	const bool bBlocked = GetWorld()->SweepSingleByObjectType(Hit, Previous, State.Location, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(FMath::Max(1.0f, State.Motion.SweepRadius)), Params);
	const float ArrivalRadius = State.Motion.SweepRadius + (bTargetAlive ? FMath::Max(0.0f, Target.CollisionRadius) : 30.0f);
	const bool bArrived = Now - State.StartTime > State.Motion.LiftSeconds
		&& FMath::PointDistToSegmentSquared(FVector(State.LastTargetLocation), Previous, State.Location) <= FMath::Square(static_cast<double>(ArrivalRadius));
	Effects[Id] = Instance;
	if (bBlocked || bArrived)
	{
		const FVector Impact = bBlocked ? Hit.ImpactPoint : FVector(State.LastTargetLocation);
		Effects[Id].State.Location = Impact;
		if (Instance.Field && Ledger->RetainEffectSource(Instance.SourceLease))
		{
			CreateFieldInternal(Instance.Field, Instance.Context, Impact, Instance.SourceLease, &Instance);
		}
		Finish(Id, EGuLiCombatEffectEndReason::Impact, Now);
	}
	else if (Now - Instance.LastCorrection >= 0.2f)
	{
		Effects[Id].LastCorrection = Now; Publish(Id, false);
	}
}

void UGuLiCombatEffectRuntimeSubsystem::BuildSpatialIndex()
{
	Ledger->GetTargetSnapshots(TargetSnapshots); SpatialGrid.Reset(); MaximumTargetRadius = 0;
	for (int32 Index = 0; Index < TargetSnapshots.Num(); ++Index)
	{
		const auto& Target = TargetSnapshots[Index];
		if (!Target.bAlive || !FMath::IsFinite(Target.CollisionRadius) || Target.CollisionRadius < 0) continue;
		MaximumTargetRadius = FMath::Max(MaximumTargetRadius, Target.CollisionRadius);
		SpatialGrid.FindOrAdd(Cell(Target.Location)).Add(Index);
	}
	bIndexReady = true;
}

void UGuLiCombatEffectRuntimeSubsystem::QuerySphere(FVector Center, float Radius, TArray<FGuLiCombatTargetSnapshot>& OutTargets)
{
	if (!bIndexReady) BuildSpatialIndex();
	OutTargets.Reset();
	const float Extent = Radius + MaximumTargetRadius;
	const FIntPoint Min = Cell(Center - FVector(Extent, Extent, 0));
	const FIntPoint Max = Cell(Center + FVector(Extent, Extent, 0));
	// An unusually large registered target must not expand the grid iteration without bound.
	if (static_cast<int64>(Max.X - Min.X + 1) * (Max.Y - Min.Y + 1) > 4096)
	{
		for (const auto& Target : TargetSnapshots)
		{
			++Counters.CandidateChecks;
			if (GuLiCombatEffects::IntersectsSphere(Center, Radius, Target)) OutTargets.Add(Target);
		}
		return;
	}
	for (int32 X = Min.X; X <= Max.X; ++X) for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
	{
		if (const auto* Indices = SpatialGrid.Find(FIntPoint(X, Y))) for (const int32 Index : *Indices)
		{
			++Counters.CandidateChecks;
			if (GuLiCombatEffects::IntersectsSphere(Center, Radius, TargetSnapshots[Index])) OutTargets.Add(TargetSnapshots[Index]);
		}
	}
}

void UGuLiCombatEffectRuntimeSubsystem::StepField(const FGuid& Id, float Now)
{
	const auto* Found = Effects.Find(Id); if (!Found) return;
	FGuLiRuntimeCombatEffect Instance = *Found;
	if (Now < Instance.State.ActivationTime) return;
	const int32 Due = GuLiCombatEffects::PulsesDue(Instance.Timing, Instance.State.ActivationTime, Instance.Duration, Instance.Interval, Now);
	const bool bPeriodicEnded=Instance.Timing==EGuLiSpellFieldTiming::Periodic && Now>=Instance.State.EndTime;
	if (bPeriodicEnded)
	{
		// A server hitch cannot cash out missed periodic damage after the field has
		// already ended. Instant/delayed one-shots still settle on their first due step.
		Effects[Id].DeliveredPulses=Due;
	}
	else if (Due > Instance.DeliveredPulses)
	{
		TArray<FGuLiCombatTargetSnapshot> Candidates;
		for (int32 Pulse = Instance.DeliveredPulses; Pulse < Due; ++Pulse)
		{
			// Damage callbacks can move/register/remove targets between catch-up pulses.
			if (Pulse>Instance.DeliveredPulses) bIndexReady=false;
			QuerySphere(Instance.State.Location, Instance.State.Radius, Candidates);
			auto* Live = Effects.Find(Id); if (!Live) return;
			// Advance before damage notifications: re-entrant queries/cancel cannot repeat this pulse.
			Live->DeliveredPulses = Pulse + 1; ++Counters.Pulses;
			for (const auto& Target : Candidates)
			{
				if (!Effects.Contains(Id)) return;
				if (Target.Handle == Instance.Context.Source) continue;
				const auto Request = MakeDamage(Instance.Context, GuLiCombatEffects::DamageId(Id, Pulse, Target.Handle), Target.Handle, Target.Location);
				if (Ledger->CommitEffectDamage(Request, Instance.SourceLease).Status == EGuLiDamageCommitStatus::Committed) ++Counters.DamageCommits;
			}
		}
	}
	Found = Effects.Find(Id); if (!Found) return;
	const EGuLiCombatEffectPhase Phase = Now >= Instance.State.EndTime ? EGuLiCombatEffectPhase::Dissipating : EGuLiCombatEffectPhase::Active;
	if (Found->State.Phase != Phase)
	{
		auto& State = Effects[Id].State; State.Phase = Phase; State.SampleTime = FMath::Max(Now, State.StartTime); ++State.Sequence;
		Publish(Id, true);
	}
	if (Now >= Instance.State.EndTime + Instance.FadeSeconds) Finish(Id, EGuLiCombatEffectEndReason::Completed, Now);
}

void UGuLiCombatEffectRuntimeSubsystem::Publish(const FGuid& Id, bool bReliable)
{
	if (const auto* Instance = Effects.Find(Id))
	{
		const FGuLiCombatEffectState Snapshot = Instance->State;
		OnState.Broadcast(Snapshot, bReliable);
	}
}

void UGuLiCombatEffectRuntimeSubsystem::Finish(const FGuid& Id, EGuLiCombatEffectEndReason Reason, float Now)
{
	FGuLiRuntimeCombatEffect Instance;
	if (!Effects.RemoveAndCopyValue(Id, Instance)) return;
	if (Instance.State.Kind == EGuLiCombatEffectKind::SustainedHitscan) BeginWingmanBurstCooldown(Instance);
	Ledger->ReleaseEffectSource(Instance.SourceLease);
	Instance.State.Phase = EGuLiCombatEffectPhase::Finished; Instance.State.EndReason = Reason;
	Instance.State.SampleTime = FMath::Max(Now, Instance.State.StartTime); ++Instance.State.Sequence;
	OnState.Broadcast(Instance.State, true);
}

bool UGuLiCombatEffectRuntimeSubsystem::CancelEffect(FGuid EffectId)
{
	if (!SynchronizeEpoch() || !Effects.Contains(EffectId)) return false;
	Finish(EffectId, EGuLiCombatEffectEndReason::Cancelled, GetWorld()->GetTimeSeconds()); return true;
}

bool UGuLiCombatEffectRuntimeSubsystem::QueryEffect(FGuid EffectId, FGuLiCombatEffectState& OutState) const
{
	if (const auto* Instance = Effects.Find(EffectId)) { OutState = Instance->State; return true; }
	if (ProjectilePool && ProjectilePool->QueryById(EffectId, OutState)) return true;
	OutState = {}; return false;
}

void UGuLiCombatEffectRuntimeSubsystem::BuildActiveSnapshot(TArray<FGuLiCombatEffectState>& OutStates, const bool bIncludeGroundProjectiles) const
{
	OutStates.Reset(Effects.Num());
	for (const auto& Pair : Effects) OutStates.Add(Pair.Value.State);
	if (ProjectilePool) ProjectilePool->AppendActiveSnapshot(OutStates,
		bIncludeGroundProjectiles ? EGuLiTargetKind::None : EGuLiTargetKind::Wingman);
}

void UGuLiCombatEffectRuntimeSubsystem::BuildGroundProjectileSnapshot(TArray<FGuLiCombatEffectState>& OutStates) const
{
	OutStates.Reset();
	if (ProjectilePool) ProjectilePool->AppendActiveSnapshot(OutStates, EGuLiTargetKind::CommanderSoldier);
}

void UGuLiCombatEffectRuntimeSubsystem::HandlePooledState(const FGuLiCombatEffectState& State, const bool bReliable)
{
	if (!Ledger || State.MatchEpoch != Ledger->GetMatchEpoch()) return;
	if (State.Phase != EGuLiCombatEffectPhase::Finished) ++Counters.ProjectilesLaunched;
	else if (State.EndReason == EGuLiCombatEffectEndReason::Impact) ++Counters.DamageCommits;
	OnState.Broadcast(State, bReliable);
}

int32 UGuLiCombatEffectRuntimeSubsystem::GetActiveEffectCount() const
{
	return Effects.Num() + (ProjectilePool ? ProjectilePool->GetActiveCount() : 0);
}
