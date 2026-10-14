#include "Gameplay/CombatEffects/GuLiFlightAcceptanceSubsystem.h"
#include "Gameplay/CombatEffects/GuLiLinearFlightContract.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "EngineLogs.h"

bool UGuLiFlightAcceptanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode()!=NM_Client
        && !FParse::Param(FCommandLine::Get(),TEXT("GuLiAcceptanceStandby"));
}
bool UGuLiFlightAcceptanceSubsystem::IsTickable() const { return !IsTemplate() && (bRunning || bAutomaticLoad) && GetWorld(); }
TStatId UGuLiFlightAcceptanceSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiFlightAcceptance,STATGROUP_Tickables); }
void UGuLiFlightAcceptanceSubsystem::Deinitialize() { StopLoad(); Super::Deinitialize(); }
void UGuLiFlightAcceptanceSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
#if !UE_BUILD_SHIPPING
    if (!FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceExtraFlights="),AutomaticCount)) return;
    if (AutomaticCount==0) return;
    if (AutomaticCount!=500) { LoadFailure=TEXT("The additional-flight acceptance cohort must contain exactly 500 flights"); return; }
    FString Run;
    if (!FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),Run) || Run.IsEmpty())
    { LoadFailure=TEXT("Automatic flight load requires an explicit acceptance run"); return; }
    FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceSeed="),AutomaticSeed);
    if (UGuLiNetworkSettings::IsHybrid(&World))
    {
        FGuLiHybridManifestData Manifest; FString Error;
        if (!FGuLiHybridManifest::Read(UGuLiNetworkSettings::Resolve().ManifestFile,Manifest,Error)
            || !FGuLiHybridAdapterRegistry::Validate(Manifest,Error)
            || Manifest.Strings.FindRef(TEXT("startup/acceptance_flight_adapter"))!=TEXT("2"))
        { LoadFailure=TEXT("Hybrid additional flights require the versioned flight factory and handoff adapter"); return; }
    }
    bAutomaticLoad=true;
#endif
}
void UGuLiFlightAcceptanceSubsystem::AdvanceAutomaticLoad()
{
    if (!LoadFailure.IsEmpty()) return;
    auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
    auto* Army=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    auto* Data=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
    auto* Pool=GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Ledger || !Army || !Data || !Pool || !Ledger->GetMatchEpoch()
        || (Hybrid && (!Hybrid->IsWorldReady() || !Hybrid->CanPublishFacts()))) return;
    AActor* Marker=nullptr;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (It->ActorHasTag(TEXT("FlightEventsQAOrigin"))) { Marker=*It; break; }
    if (!Marker) { LoadFailure=TEXT("Additional flights require the saved FlightEventsQAOrigin marker"); return; }
    if (AutomaticEpoch!=Ledger->GetMatchEpoch())
    {
        AutomaticEpoch=Ledger->GetMatchEpoch();
        if (!Hybrid) { AutomaticFlights.Reset(); AutomaticPreparedFlights.Reset(); AutomaticSources.Reset(); }
        AutomaticMinimum=MAX_int32;
        Pool->BeginEpoch(AutomaticEpoch);
    }
    struct FProducer { FGuLiCombatTargetSnapshot Source; FGuLiUnitSkillConfig Skill; };
    TArray<FProducer> Producers; TArray<FGuLiCombatTargetSnapshot> Targets; Ledger->GetTargetSnapshots(Targets);
    for (const auto& Source:Targets)
    {
        if (!Source.bAlive || Source.Handle.Kind!=EGuLiTargetKind::CommanderSoldier) continue;
        EGuLiTeam Team; uint16 Unit=0; FVector Location;
        if (!Army->GetTaskSoldierInfo(FGuLiSoldierId(Source.Handle.LocalId),Team,Unit,Location)) continue;
        const auto* Skill=Data->GetUnitSkillConfigs().FindByPredicate([&](const FGuLiUnitSkillConfig& C)
        { return C.UnitTypeId==Unit && C.SkillId==TEXT("Strafe") && C.SlotId==TEXT("BasicAttack") && C.bDefault; });
        if (Skill) Producers.Add({Source,*Skill});
    }
    Producers.Sort([](const FProducer& A,const FProducer& B){return A.Source.Handle.LocalId<B.Source.Handle.LocalId;});
    AutomaticProducerCount=Producers.Num();
    if (Producers.IsEmpty() && AutomaticSources.IsEmpty()) return;
    if (Hybrid && AutomaticSources.IsEmpty())
    {
        int32 Population=0; FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptancePopulation="),Population);
        if (Population<=0 || Producers.Num()!=Population) return;
        TMap<int32,FGuLiHybridObjectKey> FrozenSources;
        for (int32 Index=0;Index<AutomaticCount;++Index)
        {
            FGuLiHybridObjectKey Source;
            if (!Hybrid->ResolveTarget(Producers[Index%Producers.Num()].Source.Handle,Source)) return;
            FrozenSources.Add(Index,Source);
        }
        AutomaticSources=MoveTemp(FrozenSources);
    }
    const auto Finished=[&](const FGuid& Id)
    {
        if (!Id.IsValid() || !Hybrid) return false;
        FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=Id; Key.LifeGeneration=1;
        FGuLiHybridObjectState State;
        return Hybrid->GetObject(Key,State) && State.LifeState>=2;
    };
    const float Now=Hybrid ? static_cast<float>(Hybrid->GetLogicalSeconds()) : GetWorld()->GetTimeSeconds();
    AutomaticLocalGoal=AutomaticLive=0;
    for (int32 Index=0;Index<AutomaticCount;++Index)
    {
        const FProducer* Selected=nullptr;
        if (Hybrid)
        {
            auto* Key=AutomaticSources.Find(Index); FGuLiTargetHandle Source;
            FGuLiHybridObjectState Previous;
            if (!Key || !Hybrid->HasSimulation(*Key,true) || !Hybrid->GetObject(*Key,Previous)) continue;
            if (Previous.LifeState>=2)
            {
                // A dead source may finish its already admitted work. Replace
                // only future load recipes, after both old identities resolve.
                const auto Resolved=[&](FGuid Id){return !Id.IsValid() || Pool->HasTerminalFlightResult(Id);};
                if (!Resolved(AutomaticFlights.FindRef(Index)) || !Resolved(AutomaticPreparedFlights.FindRef(Index))) continue;
                FGuLiHybridObjectKey Replacement;
                for (const auto& Candidate:Producers)
                {
                    FGuLiHybridObjectKey CandidateKey; FGuLiHybridObjectState State;
                    if (Candidate.Source.Team==Previous.Team && Hybrid->ResolveTarget(Candidate.Source.Handle,CandidateKey)
                        && Hybrid->HasSimulation(CandidateKey) && Hybrid->GetObject(CandidateKey,State)
                        && State.Definition==Previous.Definition)
                    {Replacement=CandidateKey;break;}
                }
                if (!Replacement.IsValid()) continue;
                AutomaticSourceReplacements.Add({Index,*Key,Replacement,Hybrid->GetLogicalTick()});
                if (AutomaticSourceReplacements.Num()>500) AutomaticSourceReplacements.RemoveAt(0);
                ++AutomaticReplacementCount;
                *Key=Replacement;
                AutomaticFlights.Remove(Index); AutomaticPreparedFlights.Remove(Index);
            }
            if (!Hybrid->HasSimulation(*Key) || !Hybrid->ResolveObjectTarget(*Key,Source)) continue;
            Selected=Producers.FindByPredicate([&](const auto& P){return P.Source.Handle==Source;});
        }
        else if (!Producers.IsEmpty()) Selected=&Producers[Index%Producers.Num()];
        if (!Selected) continue;
        const auto& Producer=*Selected;
        ++AutomaticLocalGoal;
        FGuid Current=AutomaticFlights.FindRef(Index); FGuLiCombatEffectState Existing;
        bool Local=Current.IsValid() && Pool->QueryById(Current,Existing);
        bool Complete=Finished(Current) || (Local && Existing.Phase==EGuLiCombatEffectPhase::Active && Now>=Existing.EndTime);
        if (!Current.IsValid() || Complete)
        {
            if (const FGuid Prepared=AutomaticPreparedFlights.FindRef(Index); Prepared.IsValid())
            {
                AutomaticFlights.Add(Index,Prepared); AutomaticPreparedFlights.Remove(Index); Current=Prepared;
                Local=Pool->QueryById(Current,Existing);
                Complete=Finished(Current) || (Local && Existing.Phase==EGuLiCombatEffectPhase::Active && Now>=Existing.EndTime);
            }
        }
        // A retained flight keeps its original simulator after the producer
        // moves. Receiver presentation time cannot decide when it has ended,
        // and a prepared recipe may have no public launch event yet. Keep both
        // identities until their actual owner commits retirement. Only our own
        // pool clock can schedule the next generation ahead of that boundary.
        const bool Prepare=Local && Existing.Phase==EGuLiCombatEffectPhase::Active && Now>=Existing.StartTime
            && Now<Existing.EndTime && !AutomaticPreparedFlights.Contains(Index);
        if (Current.IsValid() && !Complete && !Prepare) continue;
        const auto& Skill=Producer.Skill;
        const auto* Mount=Data->FindWeaponMountConfig(Skill.UnitTypeId,Skill.SlotId);
        if (!Mount || Mount->Muzzles.IsEmpty() || Skill.ProjectileSpeedCentimetersPerSecond!=4800
            || Skill.ProjectileLifetimeSeconds<=0 || Skill.Damage<=0)
        { LoadFailure=TEXT("Additional flight source lacks its formal Strafe mount or the expected 4800 cm/s configuration"); return; }
        FGuLiPooledProjectileLaunch Launch;
		Launch.bAcceptanceAdditional = true;
        Launch.Context.MatchEpoch=AutomaticEpoch; Launch.Context.Source=Producer.Source.Handle;
        Launch.Context.SkillId=Skill.SkillId; Launch.Context.Damage=Skill.Damage;
        Launch.Context.WeaponBinding=FGuLiWeaponBindingKey::Army(AutomaticEpoch,Producer.Source.Team,Skill.UnitTypeId,Skill.SlotId);
        auto& NextGeneration=AutomaticGenerations.FindOrAdd(Index);
        if (NextGeneration==MAX_uint64) { LoadFailure=TEXT("Additional-flight event generation exhausted"); return; }
        const uint64 Generation=NextGeneration;
        const FString Id=FString::Printf(TEXT("acceptance/%d/extra/%d/%llu"),AutomaticSeed,Index,Generation);
        Launch.Context.ShotId=FGuLiHybridManifest::MakeStableKey(GetWorld()->GetOutermost()->GetName(),EGuLiHybridObjectKind::Flight,Id).StableId;
        Launch.Context.RootEventId=Launch.Context.ShotId;
        Launch.MuzzleOffset=Mount->Muzzles[Index%Mount->Muzzles.Num()];
        // A separate, authored load lane avoids altering normal weapon targets,
        // damage, fire rate or acquisition range while exercising real sweeps.
        Launch.Position=Marker->GetActorLocation()+FVector((Index%25)*40,(Index/25)*40,3500)+Launch.MuzzleOffset;
        Launch.Direction=FVector::ForwardVector; Launch.Speed=Skill.ProjectileSpeedCentimetersPerSecond;
        Launch.Lifetime=Skill.ProjectileLifetimeSeconds; Launch.MaximumDistance=Launch.Speed*Launch.Lifetime;
        Launch.SweepRadius=Skill.ProjectileSweepRadiusCentimeters; Launch.ServerTime=Now;
        if (Prepare) Launch.ScheduledActivationSeconds=Existing.EndTime;
        if (!Pool->Launch(Launch).IsValid()) { ++Rejected; continue; }
        ++NextGeneration; ++AutomaticOrdinal;
        if (Prepare) AutomaticPreparedFlights.Add(Index,Launch.Context.ShotId);
        else
        {
            AutomaticFlights.Add(Index,Launch.Context.ShotId); FGuLiCombatEffectState AcceptedState;
            if (Pool->QueryById(Launch.Context.ShotId,AcceptedState) && AcceptedState.Phase==EGuLiCombatEffectPhase::Active) ++AutomaticLive;
        }
        ++Accepted;
    }
    TArray<FGuLiCombatEffectState> AllFlights; Pool->AppendActiveSnapshot(AllFlights);
    AutomaticOrdinary=AutomaticLive=0;
    for (const auto& Flight:AllFlights) if (Flight.Phase==EGuLiCombatEffectPhase::Active && Now>=Flight.StartTime && Now<Flight.EndTime)
    { if (Flight.bAcceptanceAdditional) ++AutomaticLive; else ++AutomaticOrdinary; }
    for (const auto& Pair:AutomaticFlights)
    {
        FGuLiCombatEffectState Sample;
        if (!Pool->QueryById(Pair.Value,Sample) || Sample.Phase!=EGuLiCombatEffectPhase::Active || Now<Sample.StartTime) continue;
        if (AutomaticSampleId==Pair.Value && Sample.SampleTime>AutomaticSampleTime)
        {
            const double Speed=(FVector(Sample.Location)-AutomaticSamplePosition).Size()/(Sample.SampleTime-AutomaticSampleTime);
            AutomaticMinimumSpeed=FMath::Min(AutomaticMinimumSpeed,Speed);
            AutomaticMaximumSpeed=FMath::Max(AutomaticMaximumSpeed,Speed); ++AutomaticSpeedSamples;
        }
        AutomaticSampleId=Pair.Value; AutomaticSamplePosition=Sample.Location; AutomaticSampleTime=Sample.SampleTime;
        break;
    }
    Peak=FMath::Max(Peak,AutomaticLive);
    if (Peak>=AutomaticLocalGoal) AutomaticMinimum=FMath::Min(AutomaticMinimum,AutomaticLive);
}

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
	Sources.SetNum(3);
	const EGuLiTargetKind Kinds[] = {EGuLiTargetKind::CommanderSoldier,EGuLiTargetKind::GroundActor,EGuLiTargetKind::Wingman};
	for (int32 Domain=0; Domain<3; ++Domain)
	{
		const auto* Source = All.FindByPredicate([&](const auto& Item){ return Item.Handle.Kind==Kinds[Domain] && Item.bAlive; });
		if (!Source) { UE_LOG(LogNet,Error,TEXT("Flight load missing live source domain %d; populate Commander, Ground and Wingman first"),Domain); return false; }
		Sources[Domain]=*Source;
	}
	const auto* Target = All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Handle.Kind==EGuLiTargetKind::Ship && Item.Team!=Sources[2].Team; });
	if (!Target) Target = All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Team!=Sources[2].Team && Item.Handle!=Sources[2].Handle; });
	if (!Target) return false;
	EnemyTarget=Target->Handle;
	if (const auto* Catalog=GetDefault<UGuLiCombatEffectSettings>()->Catalog.LoadSynchronous())
		for (const auto& Mount : Catalog->Mounts) if (auto* Definition=Mount.Projectile.LoadSynchronous()) { CurveDefinition=Definition; break; }
	if (!CurveDefinition) return false;
	Origin=Marker->GetActorLocation()+FVector(0,0,3500);
	Desired=FlightCount; FinishTime=GetWorld()->GetTimeSeconds()+Duration; NextRefill=0; Serial=0; Peak=0;
	for (auto& Domain : Flights) Domain.Reset();
	bRunning=true; bWingmanGround=false; Accepted=Completed=Rejected=0;
	UE_LOG(LogNet,Display,TEXT("Flight acceptance started: requested=%d duration=%.1fs; three server producers, no production cap change"),Desired,Duration);
	return true;
#endif
}

bool UGuLiFlightAcceptanceSubsystem::StartWingmanGroundLoad(int32 FlightCount,float Duration,UGuLiGroundWarningStyle* WarningStyle)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (bRunning || !GetWorld() || GetWorld()->GetNetMode()==NM_Client
		|| (FlightCount!=125 && FlightCount!=250 && FlightCount!=500)
		|| !FMath::IsFinite(Duration) || Duration<1 || Duration>120) return false;
	AActor* Marker=nullptr;
	for (TActorIterator<AActor> It(GetWorld());It;++It)
		if (It->ActorHasTag(TEXT("WingmanGroundQAOrigin"))) { Marker=*It; break; }
	if (!Marker) for (TActorIterator<AActor> It(GetWorld());It;++It)
		if (It->ActorHasTag(TEXT("FlightEventsQAOrigin"))) { Marker=*It; break; }
	auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	auto* Fields=GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
	if (!Marker || !Ledger || !Ledger->GetMatchEpoch() || !Fields) return false;
	TArray<FGuLiCombatTargetSnapshot> All,SourceOnly;
	Ledger->GetTargetSnapshots(All); Ledger->GetSourceOnlySnapshots(SourceOnly); All.Append(SourceOnly);
	const auto* Source=All.FindByPredicate([](const auto& S){return S.bAlive && S.Handle.Kind==EGuLiTargetKind::Wingman;});
	if (!Source) { UE_LOG(LogNet,Error,TEXT("Ground missile load requires a live production Wingman source")); return false; }
	CurveDefinition=LoadObject<UGuLiProjectileEffectDefinition>(nullptr,TEXT("/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile.DA_WingmanGroundMissile"));
	if (!CurveDefinition || !CurveDefinition->ResolveMotionSettings(GroundMotion)) return false;
	const auto* FieldAsset=CurveDefinition->ImpactField.LoadSynchronous();
	const auto* Field=FieldAsset ? Fields->FindCombatField(FieldAsset->ConfigId) : nullptr;
	if (!Field || !Field->IsValid()) return false;
	GroundField=*Field; GroundField.Damage=.001f;
	GroundWarningStyle=WarningStyle ? WarningStyle : CurveDefinition->GroundWarningStyle.LoadSynchronous();
	if (GroundWarningStyle && !GroundWarningStyle->IsValidStyle()) return false;
	Sources.SetNum(3); Sources[2]=*Source;
	Origin=Marker->GetActorLocation();
	Desired=FlightCount; FinishTime=GetWorld()->GetTimeSeconds()+Duration;
	NextRefill=0; Serial=Peak=Accepted=Completed=Rejected=0;
	for (auto& Domain:Flights) Domain.Reset();
	bWingmanGround=bRunning=true;
	UE_LOG(LogNet,Display,TEXT("Ground missile acceptance: requested=%d duration=%.1fs definition=%s field=%s radius=%.1f source=%d"),
		Desired,Duration,*CurveDefinition->GetPathName(),*GroundField.ConfigId.ToString(),GroundField.Radius,int32(Source->Handle.Kind));
	return true;
#endif
}

void UGuLiFlightAcceptanceSubsystem::StopLoad()
{
	if (bRunning) UE_LOG(LogNet,Display,TEXT("Flight acceptance stopped: requested=%d peakFixture=%d; existing flights finish under normal authority rules"),Desired,Peak);
	bRunning=false; Sources.Reset(); CurveDefinition=nullptr; GroundWarningStyle=nullptr;
    bAutomaticLoad=false; AutomaticFlights.Reset(); AutomaticPreparedFlights.Reset(); AutomaticGenerations.Reset(); AutomaticSources.Reset();
    AutomaticSourceReplacements.Reset(); AutomaticReplacementCount=0;
	for (auto& Domain : Flights) Domain.Reset();
}

void UGuLiFlightAcceptanceSubsystem::Tick(float DeltaSeconds)
{
    if (bAutomaticLoad) { AdvanceAutomaticLoad(); return; }
	const float Now=GetWorld()->GetTimeSeconds();
	if (Now>=FinishTime) { StopLoad(); return; }
	if (Now<NextRefill) return;
	NextRefill=Now+.1f;
	auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	TArray<FGuLiCombatTargetSnapshot> All, SourceOnly;
	Ledger->GetTargetSnapshots(All); Ledger->GetSourceOnlySnapshots(SourceOnly); All.Append(SourceOnly);
	if (bWingmanGround)
	{
		const auto* Source=All.FindByPredicate([](const auto& S){return S.bAlive && S.Handle.Kind==EGuLiTargetKind::Wingman;});
		if (!Source) return;
		Sources[2]=*Source;
		Completed+=Flights[2].RemoveAll([&](const FGuid& Id){return !UGuLiCombatEffectReplicationComponent::IsFlightActive(GetWorld(),Id);});
		for (int32 Count=Flights[2].Num();Count<Desired;++Count)
			if (!SpawnWingmanGroundFlight(Serial++)) { ++Rejected; break; }
		Peak=FMath::Max(Peak,Flights[2].Num());
		return;
	}
	// A normal respawn changes the source identity. Replenishment uses the new
	// real producer; accepted flights retain their original frozen source lease.
	for (int32 Domain=0; Domain<3; ++Domain)
		if (const auto* Current=All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Handle.Kind==Sources[Domain].Handle.Kind; })) Sources[Domain]=*Current;
	FGuLiCombatTargetSnapshot Target;
	if (!Ledger->TryGetTargetSnapshot(EnemyTarget,Target) || !Target.bAlive || Target.Team==Sources[2].Team)
	{
		const auto* Replacement=All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Handle.Kind==EGuLiTargetKind::Ship && Item.Team!=Sources[2].Team; });
		if (!Replacement) Replacement=All.FindByPredicate([&](const auto& Item){ return Item.bAlive && Item.Team!=Sources[2].Team; });
		if (Replacement) EnemyTarget=Replacement->Handle;
	}
	int32 Total=0;
	for (int32 Domain=0; Domain<3; ++Domain)
	{
		auto& Ids=Flights[Domain];
		Ids.RemoveAll([&](const FGuid& Id){ return !UGuLiCombatEffectReplicationComponent::IsFlightActive(GetWorld(),Id); });
		const int32 Goal=Desired/3+(Domain<Desired%3 ? 1 : 0);
		for (int32 Count=Ids.Num(); Count<Goal; ++Count) if (!SpawnFlight(Domain,Serial++)) break;
		Total+=Ids.Num();
	}
	Peak=FMath::Max(Peak,Total);
}

bool UGuLiFlightAcceptanceSubsystem::SpawnWingmanGroundFlight(int32 Ordinal)
{
	FGuLiCombatAttackRequest Request;
	Request.ExecutorId=TEXT("WingmanGroundMissile"); Request.Projectile=CurveDefinition;
	Request.Motion=GroundMotion; Request.FrozenField=GroundField; Request.GroundWarningStyle=GroundWarningStyle;
	Request.Context.MatchEpoch=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->GetMatchEpoch();
	Request.Context.Source=Sources[2].Handle; Request.Context.ShotId=FGuid::NewGuid();
	Request.Context.RootEventId=Request.Context.ShotId; Request.Context.Damage=GroundField.Damage;
	Request.Context.EffectConfigId=GroundField.ConfigId; Request.Context.WeaponBinding.SlotId=TEXT("WingmanGroundAcceptance");
	// Frozen real ground landing points; repeated cells exercise the existing warning merge key.
	const FVector Landing=Origin+FVector((Ordinal%8)*350,(Ordinal/8%8)*350,0);
	const FVector Start=Landing+FVector(-6000,0,8000);
	Request.SourceTransform=FTransform((Landing-Start).Rotation(),Start);
	Request.TargetLocation=Landing; Request.MaximumTravelDistance=20000;
	const FGuid Id=GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>()->LaunchPointProjectile(Request);
	if (!Id.IsValid()) return false;
	Flights[2].Add(Id); ++Accepted; return true;
}

bool UGuLiFlightAcceptanceSubsystem::SpawnFlight(int32 Domain,int32 Ordinal)
{
	const FVector Position=Origin+FVector((Ordinal%20)*90,(Ordinal/20%20)*90,Domain*120);

	FGuLiCombatEffectContext Context;
	Context.MatchEpoch=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->GetMatchEpoch();
	Context.Source=Sources[Domain].Handle; Context.ShotId=FGuid::NewGuid(); Context.RootEventId=Context.ShotId;
	Context.Damage=.001f; Context.WeaponBinding.SlotId=TEXT("NetworkAcceptance");
	if (Domain==2)
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
	void GroundLoad(const TArray<FString>& Args,UWorld* World)
	{
		int32 Count=125; float Seconds=45;
		if (!World || (Args.Num()>0 && !LexTryParseString(Count,*Args[0])) || (Args.Num()>1 && !LexTryParseString(Seconds,*Args[1]))) return;
		if (auto* Fixture=World->GetSubsystem<UGuLiFlightAcceptanceSubsystem>()) Fixture->StartWingmanGroundLoad(Count,Seconds);
	}
	FAutoConsoleCommandWithWorldAndArgs GroundLoadCommand(TEXT("gs.WingmanGround.Load"),TEXT("Real ground-missile load: [125|250|500] [45 seconds]. Requires a live Wingman and saved QA marker."),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GroundLoad));
	FAutoConsoleCommandWithWorldAndArgs LoadCommand(TEXT("gs.Flights.Load"),TEXT("Explicit server-only three-source load: [500] [30 seconds]. Requires saved map marker and live sources."),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Load));
	FAutoConsoleCommandWithWorld StopCommand(TEXT("gs.Flights.Stop"),TEXT("Stop acceptance replenishment; existing flights retain authority lifetime."),FConsoleCommandWithWorldDelegate::CreateStatic(&Stop));
}
#endif

FString UGuLiFlightAcceptanceSubsystem::GetLoadStatsJson() const
{
    if (bAutomaticLoad || AutomaticCount>0)
    {
        const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        const float Now=H?H->GetLogicalSeconds():GetWorld()->GetTimeSeconds();
        TArray<FGuLiCombatEffectState> States;
        if (const auto* Pool=GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>()) Pool->AppendActiveSnapshot(States);
        int32 ActualAdditional=0,ActualOrdinary=0,ActualPrepared=0,ActualGoal=AutomaticLocalGoal;
        for (const auto& Flight:States) if (Flight.Phase==EGuLiCombatEffectPhase::Active && Now>=Flight.StartTime && Now<Flight.EndTime)
        { if (Flight.bAcceptanceAdditional) ++ActualAdditional; else ++ActualOrdinary; }
        for (const auto& Flight:States) if (Flight.bAcceptanceAdditional && Now<Flight.StartTime) ++ActualPrepared;
        if (H)
        {
            ActualGoal=0;
            for (const auto& Source:AutomaticSources) if (H->HasSimulation(Source.Value,true)) ++ActualGoal;
        }
        auto Root=MakeShared<FJsonObject>(); Root->SetBoolField(TEXT("automatic"),bAutomaticLoad);
        Root->SetStringField(TEXT("failure"),LoadFailure); Root->SetStringField(TEXT("path"),TEXT("GuLiProjectilePoolSubsystem/Strafe"));
        Root->SetNumberField(TEXT("desired_global"),AutomaticCount); Root->SetNumberField(TEXT("local_goal"),ActualGoal);
        Root->SetNumberField(TEXT("prepared_replacements"),ActualPrepared);
        Root->SetNumberField(TEXT("tracked_future_recipes"),AutomaticPreparedFlights.Num());
        Root->SetNumberField(TEXT("source_candidates"),AutomaticProducerCount); Root->SetNumberField(TEXT("source_bindings"),AutomaticSources.Num());
        Root->SetNumberField(TEXT("source_replacement_count"),double(AutomaticReplacementCount));
        TArray<TSharedPtr<FJsonValue>> Replacements;
        for (const auto& Entry:AutomaticSourceReplacements)
        {
            auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("index"),Entry.Index);
            Item->SetStringField(TEXT("previous"),Entry.Previous.StableId.ToString(EGuidFormats::Digits));
            Item->SetStringField(TEXT("next"),Entry.Next.StableId.ToString(EGuidFormats::Digits));
            Item->SetNumberField(TEXT("logical_tick"),double(Entry.At)); Replacements.Add(MakeShared<FJsonValueObject>(Item));
        }
        Root->SetArrayField(TEXT("source_replacements"),Replacements);
        Root->SetNumberField(TEXT("live_additional"),ActualAdditional); Root->SetNumberField(TEXT("live_ordinary"),ActualOrdinary);
        Root->SetNumberField(TEXT("minimum_after_fill"),AutomaticMinimum==MAX_int32 ? 0 : AutomaticMinimum);
        Root->SetNumberField(TEXT("peak_additional"),Peak); Root->SetNumberField(TEXT("accepted"),Accepted);
        Root->SetNumberField(TEXT("rejected"),Rejected); Root->SetNumberField(TEXT("seed"),AutomaticSeed);
        Root->SetNumberField(TEXT("configured_speed_cm_s"),4800);
        Root->SetNumberField(TEXT("speed_samples"),double(AutomaticSpeedSamples));
        Root->SetNumberField(TEXT("minimum_measured_speed_cm_s"),AutomaticSpeedSamples ? AutomaticMinimumSpeed : 0);
        Root->SetNumberField(TEXT("maximum_measured_speed_cm_s"),AutomaticMaximumSpeed);
        FString ReviewCase; FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceCase="),ReviewCase);
        if (ReviewCase==TEXT("flights") || ReviewCase==TEXT("flights_recovery")
            || ReviewCase.StartsWith(TEXT("gameplay_")) || ReviewCase.StartsWith(TEXT("fault_")))
        {
            TArray<TSharedPtr<FJsonValue>> Objects;
            for (const auto& Flight:States) if (Flight.bAcceptanceAdditional && Flight.Phase==EGuLiCombatEffectPhase::Active && Now>=Flight.StartTime && Now<Flight.EndTime)
            {
                auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("id"),Flight.EffectId.ToString(EGuidFormats::Digits));
                if (H)
                {
                    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=Flight.EffectId; Key.LifeGeneration=1;
                    FGuLiHybridObjectState Object;
                    if (!H->GetObject(Key,Object)) { Root->SetStringField(TEXT("failure"),TEXT("An active load flight is missing its admitted object")); continue; }
                    Item->SetStringField(TEXT("simulator"),Object.SimulationOwner.ToString(EGuidFormats::Digits));
                    Item->SetStringField(TEXT("source"),Object.Parent.StableId.ToString(EGuidFormats::Digits));
                    Item->SetBoolField(TEXT("local_simulation"),H->HasSimulation(Key));
                    Item->SetNumberField(TEXT("owner_version"),double(Object.OwnerEpoch));
                }
                Objects.Add(MakeShared<FJsonValueObject>(Item));
            }
            Root->SetArrayField(TEXT("objects"),Objects);
            TArray<TSharedPtr<FJsonValue>> Waiting;
            const auto* Pool=GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();
            if (H && Pool) for (const auto& Source:AutomaticSources) if (H->HasSimulation(Source.Value,true))
            {
                FGuLiCombatEffectState Current;
                const FGuid Id=AutomaticFlights.FindRef(Source.Key);
                if (Pool->QueryById(Id,Current) && Current.Phase==EGuLiCombatEffectPhase::Active
                    && Now>=Current.StartTime && Now<Current.EndTime) continue;
                auto Lane=MakeShared<FJsonObject>(); Lane->SetNumberField(TEXT("index"),Source.Key);
                Lane->SetStringField(TEXT("source"),Source.Value.StableId.ToString(EGuidFormats::Digits));
                Lane->SetNumberField(TEXT("next_generation"),double(AutomaticGenerations.FindRef(Source.Key)));
                const auto Describe=[&](FGuid FlightId)
                {
                    auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("id"),FlightId.ToString(EGuidFormats::Digits));
                    V->SetBoolField(TEXT("terminal"),Pool->HasTerminalFlightResult(FlightId));
                    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=FlightId; Key.LifeGeneration=1;
                    FGuLiHybridObjectState Object;
                    V->SetBoolField(TEXT("registered"),H->GetObject(Key,Object));
                    if (Object.Key.IsValid())
                    {V->SetNumberField(TEXT("life"),Object.LifeState); V->SetStringField(TEXT("owner"),Object.SimulationOwner.ToString(EGuidFormats::Digits));}
                    FGuLiCombatEffectState Sample; V->SetBoolField(TEXT("local_pool"),Pool->QueryById(FlightId,Sample));
                    if (Sample.EffectId.IsValid())
                    {V->SetNumberField(TEXT("phase"),uint8(Sample.Phase));V->SetNumberField(TEXT("start"),Sample.StartTime);V->SetNumberField(TEXT("end"),Sample.EndTime);}
                    return V;
                };
                Lane->SetObjectField(TEXT("current"),Describe(Id));
                Lane->SetObjectField(TEXT("prepared"),Describe(AutomaticPreparedFlights.FindRef(Source.Key)));
                Waiting.Add(MakeShared<FJsonValueObject>(Lane));
            }
            Root->SetArrayField(TEXT("waiting_lanes"),Waiting);
        }
        FString Text; FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text)); return Text;
    }
 return FString::Printf(TEXT("{\"running\":%s,\"groundMissile\":%s,\"desired\":%d,\"peak\":%d,\"accepted\":%d,\"completed\":%d,\"rejected\":%d,\"domains\":[%d,%d,%d]}"),
 bRunning ? TEXT("true"):TEXT("false"),bWingmanGround ? TEXT("true"):TEXT("false"), Desired, Peak,Accepted,Completed,Rejected,Flights[0].Num(),Flights[1].Num(),Flights[2].Num());
}
