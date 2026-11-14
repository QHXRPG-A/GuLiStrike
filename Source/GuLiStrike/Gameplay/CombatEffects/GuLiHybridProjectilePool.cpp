#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "Gameplay/Data/Generated/GuLiVfxIds.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/Private/GuLiHybridReflectedState.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "GuLiNetLinearFlightRecipe.h"
#include "GuLiNetAsyncClient.h"
#include "GuLiNetGroundRogue.h"
#include "CollisionQueryParams.h"
#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Gameplay/CombatEffects/GuLiLinearFlightContract.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/SubsystemCollection.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace GuLiLinearState
{
TAutoConsoleVariable<int32> FlightServiceFastPath(TEXT("gs.Hybrid.FlightServiceFastPath"),1,
    TEXT("Read only current applied flight lifetime/owner until local transition work needs the full view; reuse nested service key buffers. 0 selects the comparison path."));
TAutoConsoleVariable<int32> CacheFlightLifecycleLookup(TEXT("gs.Hybrid.CacheFlightLifecycleLookup"),1,
    TEXT("Reuse the current client's exact-life directory lookup for flights. Reads current owner/lifetime on every service. 0 selects tree lookup."));
constexpr uint32 Version=GuLiLinearFlightContract::StateVersion;
constexpr uint32 MaximumRecords=16384;
constexpr uint32 MaximumBytes=16*1024*1024;
constexpr uint32 EventTag=0x47554c46;
constexpr uint32 RicochetTag=0x47555243;
const FName System(TEXT("LinearFlights"));
constexpr uint8 Alive=1,Destroyed=3;
// The adapter consumes only these ledger fields after birth validation.
// Capabilities remain in the authoritative object and the bridge; retaining
// another full recipe for every ledger callback serves no flight operation.
struct FLedgerIdentity
{
    FGuLiHybridObjectKey Key,Parent;
    FGuid SpawnEvent,SimulationOwner;
    uint8 LifeState=0;
    FLedgerIdentity()=default;
    FLedgerIdentity(const FGuLiHybridObjectState& O)
        :Key(O.Key),Parent(O.Parent),SpawnEvent(O.SpawnEvent),SimulationOwner(O.SimulationOwner),LifeState(O.LifeState) {}
};
struct FHopStatus
{
    FGuLiHybridObjectKey Target;
    FVector Position=FVector::ZeroVector;
    FGuid Event;
    bool Submitted=false,Resolved=false,Published=false,Notified=false;
};
struct FRecord
{
    FGuLiHybridObjectKey Key,Source,Target,HitTarget;
    FGuLiPooledProjectileLaunch Recipe;
    FGuLiCombatEffectState Geometry,ExplosiveGeometry;
    FGuLiProjectilePoolHandle ExplosiveHandle;
    uint64 ExplosiveImpactAt=0;
    bool bAttackFrozen=false;
    bool bExplosiveFinished=false,bExplosiveLaunchPublished=false,bExplosiveTerminalPublished=false;
    EGuLiTeam Team=EGuLiTeam::Unassigned;
    EGuLiTargetKind SourceKind=EGuLiTargetKind::None;
    int32 FrozenUnitTypeId=0;
    FGuid Admission,Retirement,Damage,HistoryGroup;
    uint64 DamageAt=0,RetirementAt=0;
    FGuLiProjectilePoolHandle Handle;
    bool bRecipe=false,bAdmitted=false,bFinished=false,bHit=false,bDamageSubmitted=false,bDamageResolved=false,bRetirementSubmitted=false;
    bool bLaunchPublished=false,bTerminalPublished=false;
    bool bRetirementPrepared=false;
    FGuid PlanEvent;
    TArray<uint8> Plan;
    TArray<FHopStatus> Hops;
    bool bPlanFrozen=false,bPlanSubmitted=false,bPlanResolved=false,bFirstDamageCommitted=false,bFirstHitNotified=false;
    FGuid BlastPlanEvent;
    TArray<uint8> BlastPlan;
    TArray<FHopStatus> BlastTargets;
    bool bBlastFrozen=false,bBlastSubmitted=false,bBlastResolved=false;
    // Local lookup address only; Record() deliberately excludes this cursor.
    GuLiNet::ObjectReadCursor LifecycleLookup;
    // Exact locally encoded admission bytes, shared by transient capture copies.
    // This is only a decode cache, never checkpoint/wire state or an authority.
    TSharedPtr<const TArray<uint8>> BirthCapability;
};
struct FPrevious { FGuLiHybridObjectKey Key; FVector Position=FVector::ZeroVector; };
struct FHistory { FGuid Group; uint8 Domain=0; float Time=0; TArray<FPrevious> Targets; };
struct FPiece { uint32 Epoch=0; float At=0; TArray<FRecord> Records; TArray<FHistory> Histories; };
void SerializeEmitter(FGuLiHybridStateArchive& Archive, FGuLiWingmanHandle& Emitter)
{
    if (!Archive.IsReading() && Emitter == FGuLiWingmanHandle{})
    {
        // Army rounds share the exact default handle. Keep the bytes produced by
        // the normal reflected serializer, including schema, field names and types.
        static const TArray<uint8> DefaultEmitterBytes = []
        {
            FGuLiWingmanHandle DefaultEmitter;
            FGuLiHybridStateArchive Field;
            TArray<uint8> Bytes;
            FGuLiHybridReflectedState::Value(Field, DefaultEmitter);
            Field.Finish(Bytes);
            return Bytes;
        }();
        if (!DefaultEmitterBytes.IsEmpty())
        {
            Archive.WriteEncodedField(DefaultEmitterBytes);
            return;
        }
    }
    FGuLiHybridReflectedState::Value(Archive, Emitter);
}
void Record(FGuLiHybridStateArchive& A,FRecord& V)
{
    A.Value(V.Key); A.Value(V.Source); A.Value(V.Target); A.Value(V.HitTarget);
    A.Enum(V.Team,2); A.Enum(V.SourceKind,4); A.Value(V.Admission); A.Value(V.Retirement); A.Value(V.Damage); A.Value(V.HistoryGroup);
    A.Value(V.FrozenUnitTypeId);if(V.FrozenUnitTypeId<0||V.FrozenUnitTypeId>MAX_uint16)A.Reject();
    auto& R=V.Recipe; auto& C=R.Context; auto& B=C.WeaponBinding;
    A.Value(C.MatchEpoch); A.Value(C.SkillId); A.Value(C.EffectConfigId); A.Value(C.Damage); A.Value(C.ShotId); A.Value(C.RootEventId);
    A.Value(C.ProfileRevision); A.Value(C.LoadoutRevision); A.Value(B.MatchEpoch); A.Enum(B.Team,2);
    SerializeEmitter(A,C.Emitter);
    A.Value(B.OwnerPlayerGuid); A.Enum(B.Domain,2); A.Value(B.SubjectId); A.Value(B.SlotId);
    A.Value(R.Position); A.Value(R.Direction); A.Value(R.MuzzleOffset); A.Value(R.Speed); A.Value(R.Lifetime);
    A.Value(R.MaximumDistance); A.Value(R.SweepRadius); A.Value(R.ServerTime); A.Value(R.PlayerBulletVfxId); A.Value(R.bAcceptanceAdditional);
    A.Value(R.ScheduledActivationSeconds);
    A.Value(R.FireOrdinal);A.Value(R.FireNotifyTime);A.Value(R.GroundMuzzleIndex);A.Value(R.GroundOutward);A.Value(R.GroundOutwardLocal);
    A.Value(V.bAttackFrozen);
    auto& G=R.GroundAttack;A.Value(G.Player);A.Value(G.Revision);A.Value(G.Damage);A.Value(G.FireRate);A.Value(G.BulletVfxId);A.Value(G.ImpactVfxId);A.Value(G.MuzzleVfxId);
    A.Value(G.ExplosiveDamage);A.Value(G.ExplosiveRadius);A.Value(G.ExplosiveBulletVfxId);A.Value(G.ExplosiveImpactVfxId);
    TArray<uint8> Effects;if(!A.IsReading()){const auto EffectBytes=GuLiNet::Effects::Encode(G.Effects);Effects.Append(EffectBytes.data(),int32(EffectBytes.size()));}
    A.Bytes(Effects,GuLiNet::MaximumCapabilityBytes);if(A.IsReading()&&!GuLiNet::Effects::Decode({Effects.GetData(),SIZE_T(Effects.Num())},G.Effects))A.Reject();
    A.Value(V.PlanEvent);A.Array(V.Plan,GuLiNet::MaximumCapabilityBytes,[](auto& A,uint8& B){A.Value(B);});
    A.Array(V.Hops,GuLiNet::GroundRogue::MaximumHops,[](auto& A,FHopStatus& H){A.Value(H.Target);A.Value(H.Position);A.Value(H.Event);A.Value(H.Submitted);A.Value(H.Resolved);A.Value(H.Published);A.Value(H.Notified);});
    A.Value(V.bPlanFrozen);A.Value(V.bPlanSubmitted);A.Value(V.bPlanResolved);A.Value(V.bFirstDamageCommitted);A.Value(V.bFirstHitNotified);
    A.Value(V.BlastPlanEvent);A.Array(V.BlastPlan,GuLiNet::MaximumCapabilityBytes,[](auto& Ar,uint8& B){Ar.Value(B);});
    A.Array(V.BlastTargets,GuLiNet::GroundRogue::MaximumBlastTargets,[](auto& Ar,FHopStatus& H){Ar.Value(H.Target);Ar.Value(H.Position);Ar.Value(H.Event);Ar.Value(H.Submitted);Ar.Value(H.Resolved);});
    A.Value(V.bBlastFrozen);A.Value(V.bBlastSubmitted);A.Value(V.bBlastResolved);
    A.Value(V.bExplosiveFinished);A.Value(V.bExplosiveLaunchPublished);A.Value(V.bExplosiveTerminalPublished);A.Value(V.ExplosiveImpactAt);
    auto& X=V.ExplosiveGeometry;FVector XP=X.Location,XV=X.Velocity;
    A.Value(XP);A.Value(XV);A.Value(X.SampleTime);A.Value(X.EndTime);A.Value(X.Sequence);A.Enum(X.Phase,3);A.Enum(X.EndReason,6);
    if(A.IsReading()){X.Location=XP;X.Velocity=XV;}
    if(R.FireOrdinal && (V.SourceKind!=EGuLiTargetKind::GroundActor || R.GroundMuzzleIndex>1 || R.GroundOutward.ContainsNaN() || R.GroundOutward.IsNearlyZero()))A.Reject();
    if(V.bExplosiveFinished && (G.ExplosiveDamage<=0 || X.Phase!=EGuLiCombatEffectPhase::Finished))A.Reject();

    FVector Position=V.Geometry.Location,Velocity=V.Geometry.Velocity,LastTarget=V.Geometry.LastTargetLocation;
    A.Value(Position); A.Value(Velocity); A.Value(V.Geometry.SampleTime); A.Value(V.Geometry.EndTime); A.Value(V.Geometry.Sequence);
    A.Enum(V.Geometry.Phase,3); A.Enum(V.Geometry.EndReason,6);
    A.Value(LastTarget);
    if (A.IsReading()) { V.Geometry.Location=Position; V.Geometry.Velocity=Velocity; V.Geometry.LastTargetLocation=LastTarget; }
    A.Value(V.bRecipe); A.Value(V.bAdmitted); A.Value(V.bFinished); A.Value(V.bHit);
    A.Value(V.bDamageSubmitted); A.Value(V.bDamageResolved); A.Value(V.bRetirementSubmitted);
    A.Value(V.bLaunchPublished); A.Value(V.bTerminalPublished);
    A.Value(V.DamageAt);A.Value(V.RetirementAt);A.Value(V.bRetirementPrepared);
    if(R.FireOrdinal && (R.GroundOutwardLocal.ContainsNaN() || R.GroundOutwardLocal.IsNearlyZero()))A.Reject();
    if(X.Sequence && (XP.ContainsNaN() || XV.ContainsNaN() || !FMath::IsFinite(X.SampleTime)
        || !FMath::IsFinite(X.EndTime) || X.EndTime<=R.ServerTime || X.SampleTime<R.ServerTime))A.Reject();
    // Ground's native weapon has no Army/Wingman/ShipMounted channel. Its
    // provenance is the admitted Ground key plus the frozen native skill slot.
    // Do not fabricate an Army channel merely to satisfy this optional binding.
    const bool NativeGround=V.Source.Kind==EGuLiHybridObjectKind::GroundPlayer
        && V.SourceKind==EGuLiTargetKind::GroundActor && !B.SlotId.IsNone()
        && !B.OwnerPlayerGuid.IsValid() && B.SubjectId.IsNone() && (B.MatchEpoch==0 || B.MatchEpoch==C.MatchEpoch);
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::Flight || !V.Source.IsValid() || !V.bRecipe
        || !V.Admission.IsValid() || !V.Retirement.IsValid() || !V.Damage.IsValid() || !V.HistoryGroup.IsValid()
        || !C.MatchEpoch || C.SkillId.IsNone() || C.Damage<=0 || C.ShotId!=V.Key.StableId || !C.RootEventId.IsValid()
        || (!NativeGround && !B.IsWellFormed()) || R.Direction.IsNearlyZero() || R.Speed<=0 || R.Speed>1000000 || R.Lifetime<.01f || R.Lifetime>120
        || R.MaximumDistance<=0 || R.SweepRadius<0 || R.SweepRadius>10000 || R.ServerTime<0 || R.PlayerBulletVfxId<0
        || !V.Geometry.Sequence || V.Geometry.SampleTime<R.ServerTime || V.Geometry.EndTime<=R.ServerTime
        || (V.bHit && (!V.bFinished || !V.HitTarget.IsValid())) || (V.bDamageResolved && !V.bDamageSubmitted)
        || (V.bRetirementPrepared && (!V.bFinished || (V.bHit && !V.bDamageResolved)))
        || (V.bRetirementSubmitted && !V.bRetirementPrepared)
        || (V.bRetirementPrepared && G.ExplosiveDamage>0 && !V.bExplosiveFinished)) A.Reject();
    if(!V.BlastPlan.IsEmpty())
    {
        GuLiNet::GroundRogue::Blast Blast;
        if(!GuLiNet::GroundRogue::DecodeBlast({V.BlastPlan.GetData(),SIZE_T(V.BlastPlan.Num())},Blast)||!V.bBlastFrozen||!V.BlastPlanEvent.IsValid()||Blast.Targets.size()!=SIZE_T(V.BlastTargets.Num())||Blast.Shot!=GuLiNetBridge::ToCore(V.Key.StableId))A.Reject();
        else for(int32 I=0;I<V.BlastTargets.Num();++I)if(GuLiNetBridge::ToCore(V.BlastTargets[I].Target)!=Blast.Targets[I]||GuLiNetBridge::ToCore(V.BlastTargets[I].Event)!=GuLiNet::GroundRogue::BlastHitEvent(Blast.Shot,Blast.Targets[I]))A.Reject();
    }
    if(!V.Plan.IsEmpty())
    {
        GuLiNet::GroundRogue::Chain P;
        if(!GuLiNet::GroundRogue::DecodeChain({V.Plan.GetData(),SIZE_T(V.Plan.Num())},P)||!V.bPlanFrozen||!V.PlanEvent.IsValid()||P.Hops.size()!=SIZE_T(V.Hops.Num())||P.Shot!=GuLiNetBridge::ToCore(V.Key.StableId))A.Reject();
        else for(int32 I=0;I<V.Hops.Num();++I)if(GuLiNetBridge::ToCore(V.Hops[I].Target)!=P.Hops[I].Target||GuLiNetBridge::ToCore(V.Hops[I].Event)!=GuLiNet::GroundRogue::HopEvent(P.Shot,uint32(I+1),P.Hops[I].Target))A.Reject();
    }
}
void Piece(FGuLiHybridStateArchive& A,FPiece& V)
{
    uint32 Ver=Version; A.Value(Ver); A.Value(V.Epoch); A.Value(V.At);
    if (Ver!=Version || !V.Epoch || V.At<0) A.Reject();
    A.Array(V.Records,MaximumRecords,[](auto& Ar,auto& R){Record(Ar,R);});
    A.Array(V.Histories,128,[](auto& Ar,auto& H)
    {
        Ar.Value(H.Group); Ar.Value(H.Domain); Ar.Value(H.Time);
        if (!H.Group.IsValid() || H.Domain>1 || H.Time<0) Ar.Reject();
        Ar.Array(H.Targets,16384,[](auto& W,auto& T){W.Value(T.Key); W.Value(T.Position); if (!T.Key.IsValid()) W.Reject();});
        TSet<FGuLiHybridObjectKey> Keys;
        for (const auto& T:H.Targets) { if (Keys.Contains(T.Key)) Ar.Reject(); Keys.Add(T.Key); }
    });
    TSet<FGuLiHybridObjectKey> Keys;
    const bool CheckDuplicates=V.Records.Num()>1;
    bool bReportedInvalidRecord=false;
    for (const auto& R:V.Records)
    {
        const bool Duplicate=CheckDuplicates && Keys.Contains(R.Key);
        const bool WrongEpoch=R.Recipe.Context.MatchEpoch!=V.Epoch;
        const bool FutureSample=R.Geometry.Phase!=EGuLiCombatEffectPhase::Waiting && R.Geometry.SampleTime>V.At+.001f;
        const bool FutureActivation=R.Recipe.ScheduledActivationSeconds>V.At+FMath::Min(R.Recipe.Lifetime,10.f)+.001f;
        if (Duplicate || WrongEpoch || FutureSample || FutureActivation)
        {
            if (!A.IsReading() && !bReportedInvalidRecord)
            {
                UE_LOG(LogNet,Error,TEXT("Linear flight private cut rejected id=%s epoch=%u/%u at=%.6f sample=%.6f start=%.6f scheduled=%.6f lifetime=%.6f phase=%u admitted=%d finished=%d reason=%d/%d/%d/%d"),
                    *R.Key.StableId.ToString(),R.Recipe.Context.MatchEpoch,V.Epoch,V.At,R.Geometry.SampleTime,
                    R.Recipe.ServerTime,R.Recipe.ScheduledActivationSeconds,R.Recipe.Lifetime,uint8(R.Geometry.Phase),
                    R.bAdmitted,R.bFinished,Duplicate,WrongEpoch,FutureSample,FutureActivation);
                bReportedInvalidRecord=true;
            }
            A.Reject();
        }
        if (CheckDuplicates) Keys.Add(R.Key);
    }
}
bool Decode(TConstArrayView<uint8> Bytes,FPiece& V)
{ FGuLiHybridStateArchive A(Bytes,MaximumBytes); Piece(A,V); return A.Done(); }
FGuid Event(const FGuLiHybridObjectKey& K,const TCHAR* Suffix)
{ return FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/EffectEvents"),EGuLiHybridObjectKind::Flight,K.StableId.ToString(EGuidFormats::Digits)+Suffix).StableId; }
}

/** No UObject, local pool index, lease ID or navigation handle enters the private shard. */
struct FGuLiHybridProjectileState
{
    using FRecord=GuLiLinearState::FRecord;
    TWeakObjectPtr<UGuLiProjectilePoolSubsystem> Pool;
    TWeakObjectPtr<UGuLiHybridWorldSubsystem> Hybrid;
    TMap<FGuLiHybridObjectKey,FRecord> Records;
#if WITH_EDITOR
    TArray<FRecord> RecentGroundShots;
    TArray<FRecord> RecentMassShots;
#endif
    // Separate buffers preserve snapshot traversal when a callback reenters Service.
    struct FServiceEntry {FGuLiHybridObjectKey Key;FSetElementId Index;};
    TArray<TUniquePtr<TArray<FServiceEntry>>> ServiceKeyBuffers;
    int32 ServiceDepth=0;
    bool bRestoring=false;
    FGuLiHybridManifestData Manifest;
    FGuLiHybridProjectileState(UGuLiProjectilePoolSubsystem& P,UGuLiHybridWorldSubsystem& H):Pool(&P),Hybrid(&H) {}
    bool Supported(const FGuLiHybridObjectState& O) const
    { return O.Key.Kind==EGuLiHybridObjectKind::Flight && Manifest.Strings.FindRef(TEXT("adapter/definition/")+O.Definition.ToString())==TEXT("LinearFlight"); }
    FGuid LocalPlayer() const
    {
        const auto* W=Pool.IsValid()?Pool->GetWorld():nullptr;
        const auto* S=W && W->GetGameInstance()?W->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;
        return S?S->GetLocalPlayerId():FGuid();
    }
    uint32 CurrentEpoch() const
    { const auto* B=Pool.IsValid()?Pool->GetWorld()->GetGameState<AGuLiBattleGameState>():nullptr; return B?B->GetMatchEpoch():0; }
    void Trace(const TCHAR* Step,const FRecord& V,const TArray<FGuLiHybridObjectKey>* Objects=nullptr) const
    {
#if !UE_BUILD_SHIPPING
        // Explicit, acceptance-only diagnostics. Per-process files survive a
        // fixed-role restart without changing any private state or wire bytes.
        static const FString Path=[]
        {
            FString Directory;
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("GULI_ACCEPTANCE_TRACE_FLIGHTS"))!=TEXT("1")
                || !FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceDirectory="),Directory)) return FString();
            return FPaths::Combine(Directory,FString::Printf(TEXT("flight-lifecycle-%u.tsv"),FPlatformProcess::GetCurrentProcessId()));
        }();
        static uint32 Written=0;
        if (Path.IsEmpty() || Written>=100000) return;
        ++Written;
        FGuLiHybridObjectState Object;
        const bool Registered=Hybrid.IsValid() && Hybrid->GetObject(V.Key,Object);
        const FString Line=FString::Printf(TEXT("%.6f\t%s\t%s\t%s\t%s\t%.6f\t%d\t%d\t%d\t%d\t%s\t%u\t%d\t%d\t%d\t%d\t%u\t%.6f\t%.6f\t%s\n"),
            FPlatformTime::Seconds(),Step,*LocalPlayer().ToString(EGuidFormats::Digits),*V.Key.StableId.ToString(EGuidFormats::Digits),
            *V.Source.StableId.ToString(EGuidFormats::Digits),Hybrid.IsValid()?Hybrid->GetLocalLogicalSeconds():0.,
            V.bRecipe,V.bAdmitted,V.bFinished,Registered,*Object.SimulationOwner.ToString(EGuidFormats::Digits),uint32(Object.LifeState),
            Objects && Objects->Contains(V.Key),Objects && Objects->Contains(V.Source),bRestoring,V.Handle.Slot,V.Handle.Generation,
            V.Recipe.ServerTime,V.Recipe.ScheduledActivationSeconds,*V.Admission.ToString(EGuidFormats::Digits));
        FFileHelper::SaveStringToFile(Line,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
#endif
    }
    bool Includes(const TArray<FGuLiHybridObjectKey>& Objects,const FRecord& V) const
    {
        if (Objects.Contains(V.Key)) return true;
        if (V.bAdmitted || !Objects.Contains(V.Source)) return false;
        FGuLiHybridObjectState Flight;
        // Once numerically registered, the flight's own custody controls its
        // recipe even before the local pool has observed admission/activation.
        return !Hybrid->GetObject(V.Key,Flight);
    }
    void FreezeRicochet(FRecord& V)
    {
        if(V.bPlanFrozen)return;
        if(V.SourceKind!=EGuLiTargetKind::GroundActor||!V.bHit){V.bPlanFrozen=true;return;}
        FGuLiHybridCapabilityState Frozen;GuLiNet::GroundRogue::Attack Attack;
        // The lifecycle/view fast path deliberately omits capabilities. Read only
        // the immutable admitted attack, once at this hit's route boundary.
        if(!Hybrid->GetObjectCapability(V.Key,TEXT("ground_attack"),Frozen))return;
        if(!GuLiNet::GroundRogue::DecodeAttack({Frozen.Data.GetData(),SIZE_T(Frozen.Data.Num())},Attack))
        {Hybrid->Suspend(TEXT("Invalid Ground attack while freezing ricochet"));return;}
        V.bPlanFrozen=true;
        const auto Effect=GuLiNet::Effects::GetRicochet(Attack.Effects);if(!Effect.Maximum)return;
        GuLiNet::GroundRogue::Chain Plan;Plan.Frozen=Attack;Plan.First=GuLiNetBridge::ToCore(V.HitTarget);Plan.Shot=GuLiNetBridge::ToCore(V.Key.StableId);Plan.At=V.DamageAt;
        const FVector Origin=V.Geometry.Location;Plan.X=Origin.X;Plan.Y=Origin.Y;Plan.Z=Origin.Z;Plan.Budget=GuLiNet::GroundRogue::HopBudget(Plan.Shot,Effect);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiGroundRicochet),false);
        for(const auto& Snapshot:Pool->Snapshots)if(Snapshot.CollisionActor.IsValid())Params.AddIgnoredActor(Snapshot.CollisionActor.Get());
        for(const auto& Source:Pool->SourceOnly)if(Source.CollisionActor.IsValid())Params.AddIgnoredActor(Source.CollisionActor.Get());
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        TSet<FGuLiHybridObjectKey> Visited;Visited.Add(V.HitTarget);FVector Point=Origin;
        const auto Cell=[](const FVector& P){const double Size=UGuLiProjectilePoolSubsystem::SpatialCellCentimeters;return FIntVector(FMath::FloorToInt(P.X/Size),FMath::FloorToInt(P.Y/Size),FMath::FloorToInt(P.Z/Size));};
        // The search volume belongs to the first impact, never follows the chain.
        const auto Min=Cell(Origin-FVector(Effect.Range)),Max=Cell(Origin+FVector(Effect.Range));
        struct FCandidate {FGuLiHybridObjectKey Key;FVector Point;double Distance;};TArray<FCandidate> Eligible;TSet<int32> Seen;
        for(int32 X=Min.X;X<=Max.X;++X)for(int32 Y=Min.Y;Y<=Max.Y;++Y)for(int32 Z=Min.Z;Z<=Max.Z;++Z)
        if(const auto* Bucket=Pool->SpatialGrid.Find(FIntVector(X,Y,Z)))for(int32 Index:*Bucket)
        {
            if(Seen.Contains(Index))continue;Seen.Add(Index);const auto& S=Pool->Targets[Index].Snapshot;
            if(!S.bAlive||S.Team==V.Team||S.Team==EGuLiTeam::Unassigned)continue;
            const FVector Center=S.GetHitCenter();if(Center.ContainsNaN()||FVector::DistSquared(Origin,Center)>FMath::Square(double(Effect.Range)))continue;
            FGuLiHybridObjectKey Key;if(!Hybrid->ResolveTarget(S.Handle,Key)||Visited.Contains(Key))continue;Eligible.Add({Key,Center,0});
        }
        // One spatial query per frozen route; only order/visibility changes per hop.
        for(uint32 Hop=0;Hop<Plan.Budget;++Hop)
        {
            TArray<FCandidate> Candidates;
            for(const auto& C:Eligible)if(!Visited.Contains(C.Key))Candidates.Add({C.Key,C.Point,FVector::DistSquared(Point,C.Point)});
            Candidates.Sort([](const auto& A,const auto& B){return A.Distance!=B.Distance?A.Distance<B.Distance:GuLiNetBridge::ToCore(A.Key)<GuLiNetBridge::ToCore(B.Key);});
            const auto* Next=Candidates.FindByPredicate([&](const auto& C){return !Pool->GetWorld()->LineTraceTestByObjectType(Point,C.Point,Objects,Params);});
            if(!Next)break;GuLiNet::GroundRogue::Hop H{GuLiNetBridge::ToCore(Next->Key),Next->Point.X,Next->Point.Y,Next->Point.Z};Plan.Hops.push_back(H);
            GuLiLinearState::FHopStatus Status;Status.Target=Next->Key;Status.Position=Next->Point;Status.Event=GuLiNetBridge::ToUE(GuLiNet::GroundRogue::HopEvent(Plan.Shot,Hop+1,H.Target));V.Hops.Add(Status);Visited.Add(Next->Key);Point=Next->Point;
        }
        const auto Bytes=GuLiNet::GroundRogue::EncodeChain(Plan);
        if(Bytes.empty()){V.Hops.Reset();V.bPlanFrozen=false;Hybrid->Suspend(TEXT("Ground ricochet route could not be encoded"));return;}
        V.Plan.Append(Bytes.data(),int32(Bytes.size()));V.PlanEvent=GuLiLinearState::Event(V.Key,TEXT("/ricochet-plan"));
    }
    void QueueGroundHit(FRecord& V,int32 Segment,const FGuLiTargetHandle& Target,FGuid Event,FVector Point,float Multiplier)
    {
        UGuLiProjectilePoolSubsystem::FAcceptedGroundHit Accepted;Accepted.Snapshot=V.Recipe.GroundAttack;
        auto& H=Accepted.Hit;H.Shot=V.Key.StableId;H.Event=Event;H.Target=Target;H.Position=Point;H.Segment=Segment;H.DirectDamageMultiplier=Multiplier;H.bCanTriggerRicochet=Segment==0;
        Pool->PendingGroundHits.Add(MoveTemp(Accepted));
    }
    bool PublishRicochet(FRecord& V)
    {
        TArray<FGuLiFlightEvent> Events;TArray<int32> Indices;FVector Start=V.Geometry.Location;
        for(int32 I=0;I<V.Hops.Num();++I)
        {
            const auto& H=V.Hops[I];if(!H.Published)
            {
                FGuLiFlightEvent E;E.bInstantGroundSegment=true;E.InstantEnd=H.Position;E.ImpactVfxId=V.Recipe.GroundAttack.ImpactVfxId;E.State=V.Geometry;
                auto& S=E.State;S.EffectId=H.Event;S.Sequence=1;S.Phase=EGuLiCombatEffectPhase::Active;S.EndReason=EGuLiCombatEffectEndReason::None;S.LaunchLocation=S.Location=Start;
                S.LaunchDirection=(H.Position-Start).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);S.Motion.Speed=300000.f;S.Velocity=FVector(S.LaunchDirection)*S.Motion.Speed;
                S.StartTime=S.SampleTime=S.ActivationTime=V.Geometry.SampleTime;S.EndTime=S.StartTime+.02f;Hybrid->ResolveObjectTarget(H.Target,S.Target);S.LastTargetLocation=H.Position;
                Events.Add(E);Indices.Add(I);
            }Start=H.Position;
        }
        int32 Cursor=0;
        while(Cursor<Events.Num())
        {
            TArray<uint8> Batch;const int32 Count=GuLiFlightWire::EncodeBatch(MakeArrayView(Events).Slice(Cursor,Events.Num()-Cursor),Batch);if(!Count)return false;
            const uint64 PublishedAt=Hybrid->GetObjectLogicalTick(V.Key);
            for(int32 I=0;I<Count;++I)UGuLiCombatEffectReplicationComponent::RebasePublishedState(Pool->GetWorld(),PublishedAt,Events[Cursor+I].State);
            if(GuLiFlightWire::EncodeBatch(MakeArrayView(Events).Slice(Cursor,Count),Batch)!=Count)return false;
            GuLiNet::Writer W;W.U32(GuLiLinearState::RicochetTag);W.U32(GuLiLinearState::Version);W.Blob({Batch.GetData(),SIZE_T(Batch.Num())});const auto Bytes=W.Take();TArray<uint8> Body;Body.Append(Bytes.data(),int32(Bytes.size()));
            if(!Hybrid->PublishEffect(V.Key,FGuid::NewGuid(),Body))return false;
            for(int32 I=0;I<Count;++I)
            {auto Local=Events[Cursor+I];UGuLiCombatEffectReplicationComponent::RebaseReceivedState(Pool->GetWorld(),PublishedAt,Local.State);UGuLiCombatEffectReplicationComponent::ApplyHybridFlight(Pool->GetWorld(),Local);V.Hops[Indices[Cursor+I]].Published=true;}
            Cursor+=Count;
        }return true;
    }
    bool ServiceRicochet(FRecord& V,const FGuLiHybridObjectState& Object)
    {
        if(!V.bHit||V.SourceKind!=EGuLiTargetKind::GroundActor)return true;
        if(!V.bFirstDamageCommitted)return true;
        if(!V.bPlanFrozen){FreezeRicochet(V);if(!V.bPlanFrozen)return false;}
        if(!V.bFirstHitNotified)
        {FGuLiTargetHandle Target;if(Hybrid->ResolveObjectTarget(V.HitTarget,Target)){V.bFirstHitNotified=true;QueueGroundHit(V,0,Target,V.Damage,V.Geometry.Location,1);}}
        if(V.Plan.IsEmpty())return true;
        if(!V.bPlanResolved)
        {
            FGuLiHybridCapabilityState Existing;
            if(Hybrid->GetObjectCapability(V.Key,TEXT("ground_ricochet"),Existing)&&!Existing.Data.IsEmpty())
            {if(Existing.Data!=V.Plan){Hybrid->Suspend(TEXT("Frozen ricochet route changed"));return false;}V.bPlanResolved=true;}
            else
            {
                FGuLiHybridTransactionResult Result;
                if(Hybrid->FindTransactionResult(V.PlanEvent,Result))
                {if(Result.Status==EGuLiHybridTransactionStatus::Paused||Result.Status==EGuLiHybridTransactionStatus::Deferred)return false;
                 if(!Result.IsCommitted()){V.bPlanResolved=true;V.Plan.Reset();V.Hops.Reset();return true;}V.bPlanResolved=true;}
                else if(!V.bPlanSubmitted&&!Hybrid->IsBattlePaused() && (!V.bBlastSubmitted || V.bBlastResolved))
                {
                    FGuLiHybridObjectState Current;if(!Hybrid->GetObjectView(V.Key,Current))return false;
                    V.bPlanSubmitted=Hybrid->SubmitCapability(V.Key,V.PlanEvent,FName(*(TEXT("plan/ricochet/")+Current.Definition.ToString())),Current.RelationRevision,V.Plan,V.Team).IsValid();
                }
            }
            if(!V.bPlanResolved)return false;
        }
        if(Hybrid->IsBattlePaused())return false;PublishRicochet(V);bool All=true;
        for(int32 I=0;I<V.Hops.Num();++I)
        {
            auto& H=V.Hops[I];if(!H.Published)All=false;if(H.Resolved)continue;
            FGuLiDamageCommitResult Result;const float Scale=FMath::Pow(V.Recipe.GroundAttack.GetRicochet().Retention,float(I+1));
            if(Hybrid->FindDamageResult(H.Event,Result))
            {
                H.Resolved=true;if(Result.Status==EGuLiDamageCommitStatus::Committed&&!H.Notified)
                {FGuLiTargetHandle Target;if(Hybrid->ResolveObjectTarget(H.Target,Target)){H.Notified=true;QueueGroundHit(V,I+1,Target,H.Event,H.Position,Scale);}}
            }
            else if(!H.Submitted)
            {
                FGuLiDamageRequest R;R.MatchEpoch=CurrentEpoch();R.DamageEventId=R.RootEventId=H.Event;R.ShotId=V.Key.StableId;R.Source=V.Recipe.Context.Source;R.SkillId=V.Recipe.Context.SkillId;R.WeaponBinding=V.Recipe.Context.WeaponBinding;R.HitLocation=H.Position;R.Damage=V.Recipe.GroundAttack.Damage*Scale;
                FGuLiCombatTargetSnapshot Snapshot;
                if(!Hybrid->ResolveObjectTarget(H.Target,R.Target)||!Pool->Ledger->TryGetTargetSnapshot(R.Target,Snapshot)||!Snapshot.bAlive){H.Resolved=true;continue;}
                if(R.Damage<=0){H.Resolved=true;H.Notified=true;QueueGroundHit(V,I+1,R.Target,H.Event,H.Position,Scale);continue;}
                H.Submitted=Hybrid->SubmitDamage(R,Object.BornAt,{},&V.Key,V.DamageAt).IsValid();
            }
            if(!H.Resolved)All=false;
        }return All;
    }
    void FreezeBlast(FRecord& V)
    {
        if(V.bBlastFrozen || (V.Recipe.GroundAttack.ExplosiveDamage>0 && !V.bExplosiveFinished))return;
        const auto& Attack=V.Recipe.GroundAttack;
        if(V.SourceKind!=EGuLiTargetKind::GroundActor||Attack.ExplosiveDamage<=0
            ||(V.ExplosiveGeometry.EndReason!=EGuLiCombatEffectEndReason::Impact&&V.ExplosiveGeometry.EndReason!=EGuLiCombatEffectEndReason::Blocked))
        {V.bBlastFrozen=true;return;}
        // Freeze only this independently swept projectile's area event.
        GuLiNet::GroundRogue::Blast Plan;Plan.Shot=GuLiNetBridge::ToCore(V.Key.StableId);Plan.At=V.ExplosiveImpactAt;
        const FVector Center=V.ExplosiveGeometry.Location;Plan.X=Center.X;Plan.Y=Center.Y;Plan.Z=Center.Z;
        TArray<FGuLiHybridObjectKey> Targets;
        TArray<FGuLiCombatTargetSnapshot> CurrentTargets;Pool->Ledger->GetTargetSnapshots(CurrentTargets);
        for(const auto& Snapshot:CurrentTargets)
        {
            if(!Snapshot.bAlive||Snapshot.Team==V.Team||Snapshot.Team==EGuLiTeam::Unassigned
                ||!GuLiCombatEffects::IntersectsSphere(Center,Attack.ExplosiveRadius,Snapshot))continue;
            FGuLiHybridObjectKey Key;if(Hybrid->ResolveTarget(Snapshot.Handle,Key))Targets.AddUnique(Key);
        }
        Targets.Sort([](const auto& A,const auto& B){return GuLiNetBridge::ToCore(A)<GuLiNetBridge::ToCore(B);});
        if(Targets.Num()>int32(GuLiNet::GroundRogue::MaximumBlastTargets))
        {Hybrid->Suspend(TEXT("Ground explosion exceeds recoverable target budget"));return;}
        for(const auto& Target:Targets)
        {
            Plan.Targets.push_back(GuLiNetBridge::ToCore(Target));
            auto& H=V.BlastTargets.AddDefaulted_GetRef();H.Target=Target;H.Position=Center;
            H.Event=GuLiNetBridge::ToUE(GuLiNet::GroundRogue::BlastHitEvent(Plan.Shot,Plan.Targets.back()));
        }
        const auto Bytes=GuLiNet::GroundRogue::EncodeBlast(Plan);
        if(Bytes.empty()){V.BlastTargets.Reset();Hybrid->Suspend(TEXT("Ground explosion plan could not be encoded"));return;}
        V.BlastPlan.Append(Bytes.data(),int32(Bytes.size()));
        V.BlastPlanEvent=GuLiLinearState::Event(V.Key,TEXT("/explosion-plan"));V.bBlastFrozen=true;
    }
    bool ServiceBlast(FRecord& V,const FGuLiHybridObjectState& Object)
    {
        if(!V.bBlastFrozen){FreezeBlast(V);if(!V.bBlastFrozen)return false;}
        if(V.BlastPlan.IsEmpty())return true;
        if(!V.bBlastResolved)
        {
            FGuLiHybridCapabilityState Existing;
            if(Hybrid->GetObjectCapability(V.Key,TEXT("ground_explosion"),Existing)&&!Existing.Data.IsEmpty())
            {if(Existing.Data!=V.BlastPlan){Hybrid->Suspend(TEXT("Frozen Ground explosion changed"));return false;}V.bBlastResolved=true;}
            else
            {
                FGuLiHybridTransactionResult Result;
                if(Hybrid->FindTransactionResult(V.BlastPlanEvent,Result))
                {
                    if(Result.Status==EGuLiHybridTransactionStatus::Paused||Result.Status==EGuLiHybridTransactionStatus::Deferred)return false;
                    if(!Result.IsCommitted()){Hybrid->Suspend(TEXT("Authoritative Ground explosion plan rejected"));return false;}
                    V.bBlastResolved=true;
                }
                else if(!V.bBlastSubmitted&&!Hybrid->IsBattlePaused() && (!V.bPlanSubmitted || V.bPlanResolved))
                {
                    FGuLiHybridObjectState Current;if(!Hybrid->GetObjectView(V.Key,Current))return false;
                    V.bBlastSubmitted=Hybrid->SubmitCapability(V.Key,V.BlastPlanEvent,FName(*(TEXT("plan/explosion/")+Current.Definition.ToString())),Current.RelationRevision,V.BlastPlan,V.Team).IsValid();
                }
            }
            if(!V.bBlastResolved)return false;
        }
        if(Hybrid->IsBattlePaused())return false;
        bool All=true;
        for(auto& H:V.BlastTargets)
        {
            if(H.Resolved)continue;
            FGuLiDamageCommitResult Result;
            if(Hybrid->FindDamageResult(H.Event,Result))H.Resolved=true;
            else if(!H.Submitted)
            {
                FGuLiDamageRequest R;R.MatchEpoch=CurrentEpoch();R.DamageEventId=R.RootEventId=H.Event;R.ShotId=V.Key.StableId;
                R.Source=V.Recipe.Context.Source;R.SkillId=V.Recipe.Context.SkillId;R.WeaponBinding=V.Recipe.Context.WeaponBinding;
                R.HitLocation=H.Position;R.Damage=V.Recipe.GroundAttack.ExplosiveDamage;
                FGuLiCombatTargetSnapshot Snapshot;
                if(!Hybrid->ResolveObjectTarget(H.Target,R.Target)||!Pool->Ledger->TryGetTargetSnapshot(R.Target,Snapshot)||!Snapshot.bAlive){H.Resolved=true;continue;}
                H.Submitted=Hybrid->SubmitDamage(R,Object.BornAt,{},&V.Key,V.ExplosiveImpactAt).IsValid();
            }
            if(!H.Resolved)All=false;
        }
        return All;
    }
    void Rebuild(FRecord& V)
    {
        auto& S=V.Geometry; const auto& R=V.Recipe;
        S.EffectId=V.Key.StableId; S.MatchEpoch=CurrentEpoch(); S.Kind=EGuLiCombatEffectKind::LinearProjectile;
        S.Source=V.Recipe.Context.Source; S.Target=V.Recipe.Context.Target; S.SourceTeam=V.Team;
        S.SlotId=V.Recipe.Context.WeaponBinding.SlotId;
        S.PlayerBulletVfxId=R.PlayerBulletVfxId; S.bAcceptanceAdditional=R.bAcceptanceAdditional;
        S.PlayerImpactVfxId=R.GroundAttack.ImpactVfxId;
        S.Motion.Speed=R.Speed; S.Motion.SweepRadius=R.SweepRadius; S.Motion.MaximumLifetime=R.Lifetime;
        S.LaunchLocation=R.Position; S.LaunchDirection=R.Direction; S.MuzzleOffset=R.MuzzleOffset;
        S.StartTime=S.ActivationTime=R.ServerTime;
    }
    FGuLiPooledProjectileLaunch ExplosiveRecipe(const FRecord& V) const
    {
        auto R=V.Recipe;const auto& A=V.Recipe.GroundAttack;const auto E=GuLiNet::Effects::GetExplosive(A.Effects);
        R.Context.ShotId=GuLiNetBridge::ToUE(GuLiNet::GroundRogue::BlastEvent(GuLiNetBridge::ToCore(V.Key.StableId)));
        R.Context.RootEventId=V.Key.StableId;R.Context.Damage=A.ExplosiveDamage;R.Context.Target={};
        R.Position+=R.GroundOutward*E.OutwardOffset;R.MuzzleOffset+=R.GroundOutwardLocal*E.OutwardOffset;
        R.Speed*=E.SpeedMultiplier;R.Lifetime/=E.SpeedMultiplier;R.PlayerBulletVfxId=A.ExplosiveBulletVfxId;
        R.GroundAttack={};return R;
    }
    void RebuildExplosive(FRecord& V)
    {
        if(V.Recipe.GroundAttack.ExplosiveDamage<=0)return;
        const auto R=ExplosiveRecipe(V);auto& X=V.ExplosiveGeometry;
        if(!X.Sequence)
        {X=V.Geometry;X.Sequence=1;X.Phase=EGuLiCombatEffectPhase::Waiting;X.EndReason=EGuLiCombatEffectEndReason::None;
         X.Location=R.Position;X.SampleTime=R.ServerTime;X.EndTime=R.ServerTime+FMath::Min(R.Lifetime,R.MaximumDistance/R.Speed);}
        X.EffectId=R.Context.ShotId;X.MatchEpoch=CurrentEpoch();X.Source=R.Context.Source;X.Target={};X.SourceTeam=V.Team;
        X.Kind=EGuLiCombatEffectKind::LinearProjectile;X.SlotId=R.Context.WeaponBinding.SlotId;
        X.PlayerBulletVfxId=R.PlayerBulletVfxId;X.PlayerImpactVfxId=V.Recipe.GroundAttack.ExplosiveImpactVfxId;
        X.LaunchLocation=R.Position;X.LaunchDirection=R.Direction;X.MuzzleOffset=R.MuzzleOffset;
        X.Velocity=R.Direction*R.Speed;X.Motion.Speed=R.Speed;X.Motion.SweepRadius=R.SweepRadius;X.Motion.MaximumLifetime=R.Lifetime;
        X.StartTime=X.ActivationTime=R.ServerTime;
    }
    void ActivateExplosive(FRecord& V)
    {
        if(V.Recipe.GroundAttack.ExplosiveDamage<=0 || V.bExplosiveFinished || !V.bAdmitted || Pool->Matches(V.ExplosiveHandle))return;
        RebuildExplosive(V);const auto R=ExplosiveRecipe(V);const auto Saved=V.ExplosiveGeometry;
        V.ExplosiveHandle=Pool->ActivateLaunch(R,V.Team);
        if(!V.ExplosiveHandle.IsValid()){Hybrid->Suspend(TEXT("Admitted explosive round could not activate"));return;}
        auto& Slot=Pool->Slots[V.ExplosiveHandle.Slot];Slot.State=Saved;Slot.State.Phase=EGuLiCombatEffectPhase::Active;Slot.HistoryGroup=V.HistoryGroup;
        V.ExplosiveGeometry=Slot.State;
        if(!bRestoring && !V.bExplosiveLaunchPublished)V.bExplosiveLaunchPublished=Publish(V,false,true);
        if(Saved.Phase!=EGuLiCombatEffectPhase::Waiting)return;
        // Validate the short outward offset against the same blocking world objects.
        FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiExplosiveMuzzle),false);
        TArray<FGuLiCombatTargetSnapshot> Bodies;Pool->Ledger->GetTargetSnapshots(Bodies);
        for(const auto& Body:Bodies)if(Body.CollisionActor.IsValid())Params.AddIgnoredActor(Body.CollisionActor.Get());
        Pool->Ledger->GetSourceOnlySnapshots(Bodies);
        for(const auto& Body:Bodies)if(Body.CollisionActor.IsValid())Params.AddIgnoredActor(Body.CollisionActor.Get());
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        FHitResult Hit;
        if(Pool->GetWorld()->SweepSingleByObjectType(Hit,V.Recipe.Position,R.Position,FQuat::Identity,Objects,FCollisionShape::MakeSphere(R.SweepRadius),Params))
            Pool->Retire(V.ExplosiveHandle,EGuLiCombatEffectEndReason::Blocked,Hit.Location,R.ServerTime);
    }
    bool Rebind(FRecord& V)
    {
        if (!Hybrid.IsValid() || !Hybrid->ResolveObjectTarget(V.Source,V.Recipe.Context.Source)) return false;
        if (V.Target.IsValid() && !Hybrid->ResolveObjectTarget(V.Target,V.Recipe.Context.Target)) return false;
        if (V.SourceKind==EGuLiTargetKind::Wingman)
        {
            FGuLiHybridObjectState Source;FGuLiWingmanHandle Member;
            if (!Hybrid->GetObjectMetadata(V.Source,Source) || !GuLiHybridWingman::Resolve(Manifest,Source,Member)
                || Member!=V.Recipe.Context.Emitter) return false;
        }
        V.Recipe.Context.MatchEpoch=V.Recipe.Context.WeaponBinding.MatchEpoch=CurrentEpoch(); Rebuild(V);RebuildExplosive(V); return true;
    }
    FGuLiProjectilePoolHandle Reserve(FRecord& V)
    {
        auto* P=Pool.Get(); if (!P) return {};
        if (P->FreeSlots.IsEmpty()) P->Grow(FMath::Max(1,GetDefault<UGuLiCombatEffectSettings>()->ProjectilePoolGrowthSize));
        const int32 Index=P->FreeSlots.Pop(EAllowShrinking::No); auto& S=P->Slots[Index];
        S={}; S.Generation=++SlotSerial; if (!SlotSerial) S.Generation=++SlotSerial;
        S.State=V.Geometry; S.State.Phase=EGuLiCombatEffectPhase::Waiting; S.Context=V.Recipe.Context;
        V.Handle.Slot=Index; V.Handle.Generation=S.Generation; V.Handle.MatchEpoch=P->Epoch;
        P->ById.Add(V.Key.StableId,V.Handle); return V.Handle;
    }
    uint32 SlotSerial=0;
    void Discard(FRecord& V,const TCHAR* Reason)
    {
        auto* P=Pool.Get(); if (!P) return;
        Trace(Reason,V);
#if WITH_EDITOR
        if(V.SourceKind==EGuLiTargetKind::GroundActor)
        {if(RecentGroundShots.Num()>=512)RecentGroundShots.RemoveAt(0,1,EAllowShrinking::No);RecentGroundShots.Add(V);}
        else if(V.SourceKind==EGuLiTargetKind::CommanderSoldier && V.bHit)
        {if(RecentMassShots.Num()>=16)RecentMassShots.RemoveAt(0,1,EAllowShrinking::No);RecentMassShots.Add(V);}
#endif
        P->ById.Remove(V.Key.StableId);
        if (P->Slots.IsValidIndex(V.Handle.Slot) && P->Slots[V.Handle.Slot].Generation==V.Handle.Generation
            && P->Slots[V.Handle.Slot].ActiveIndex==INDEX_NONE)
        { P->Slots[V.Handle.Slot]={}; P->FreeSlots.AddUnique(V.Handle.Slot); }
        Records.Remove(V.Key);
    }
    void Activate(FRecord& V,const FGuLiHybridObjectState& Object)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_Activate);
        auto* P=Pool.Get(); if (!P || !V.bRecipe || Object.SimulationOwner!=LocalPlayer() || Object.LifeState!=GuLiLinearState::Alive) return;
        if(!V.Recipe.Context.Source.IsValid() && !Rebind(V))return;
        if (!V.bAdmitted) Trace(TEXT("admission-observed"),V);
        if(!V.bAttackFrozen&&V.SourceKind==EGuLiTargetKind::GroundActor)
        {
            FGuLiHybridCapabilityState Frozen;
            if(!Hybrid->GetObjectCapability(V.Key,TEXT("ground_attack"),Frozen))return;
            GuLiNet::GroundRogue::Attack A;if(!GuLiNet::GroundRogue::DecodeAttack({Frozen.Data.GetData(),SIZE_T(Frozen.Data.Num())},A)){Hybrid->Suspend(TEXT("Invalid admitted Ground attack"));return;}
            auto& G=V.Recipe.GroundAttack;G.Player=GuLiNetBridge::ToUE(A.Player);G.Revision=A.RevisionValue;G.Damage=A.Damage;G.FireRate=A.FireRate;G.BulletVfxId=int32(A.BulletVfx);G.ImpactVfxId=int32(A.ImpactVfx);G.MuzzleVfxId=int32(A.MuzzleVfx);
            G.ExplosiveDamage=A.ExplosiveDamage;G.ExplosiveRadius=A.ExplosiveRadius;G.ExplosiveBulletVfxId=int32(A.ExplosiveBulletVfx);G.ExplosiveImpactVfxId=int32(A.ExplosiveImpactVfx);
            G.Effects=A.Effects;V.Recipe.PlayerBulletVfxId=G.BulletVfxId;V.Recipe.Context.Damage=G.Damage;V.bAttackFrozen=true;
        }
        if (!V.bAdmitted && !bRestoring)
        {
            V.Recipe.ServerTime=FMath::Max(V.Recipe.ServerTime,static_cast<float>(Hybrid->GetLocalLogicalSeconds()));
            V.Geometry.SampleTime=V.Recipe.ServerTime; V.Geometry.EndTime=V.Recipe.ServerTime+FMath::Min(V.Recipe.Lifetime,V.Recipe.MaximumDistance/V.Recipe.Speed);
            Rebuild(V);
        }
        V.bAdmitted=true;
        if (V.bFinished || P->Matches(V.Handle)) {ActivateExplosive(V);return;}

        if (Hybrid->GetLocalLogicalSeconds()+1.e-5<V.Recipe.ServerTime)
        {
            // Admission reserves the future recipe; only actual activation is a
            // flight event. A peer's independent clock cannot schedule our launch.
            return;
        }
        if (!V.Handle.IsValid()) Reserve(V);
        const auto Private=V.Geometry;
        V.Handle=P->ActivateLaunch(V.Recipe,V.Team,{},V.Handle.Slot);
        Trace(TEXT("activated"),V);
        if (!V.Handle.IsValid()) { Hybrid->Suspend(TEXT("An admitted flight could not activate its pool slot")); return; }
        auto& Slot=P->Slots[V.Handle.Slot]; Slot.HistoryGroup=V.HistoryGroup;
        Slot.State=Private; Slot.State.Phase=EGuLiCombatEffectPhase::Active; Slot.Context=V.Recipe.Context;
        V.Geometry=Slot.State;
        if (!bRestoring)
        {
            if (!V.bLaunchPublished) V.bLaunchPublished=Publish(V,false);
        }
        ActivateExplosive(V);
    }
    bool Publish(FRecord& V,bool Bootstrap,bool Explosive=false)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_Publish);
        // A numeric readback can activate a flight at the final tick of a
        // catch-up batch before its earlier shared steps reach Service().
        // Keep the publication pending until that actual sample is current;
        // do not replay local effects or attempt an invalid private encoding.
        const auto& Handle=Explosive?V.ExplosiveHandle:V.Handle;
        const auto& Geometry=Pool->Matches(Handle)?Pool->Slots[Handle.Slot].State:(Explosive?V.ExplosiveGeometry:V.Geometry);
        if (!Bootstrap && Geometry.Phase!=EGuLiCombatEffectPhase::Waiting
            && Geometry.SampleTime>Hybrid->GetLocalLogicalSeconds()+.001)
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_WaitForPublicationStep);
            return false;
        }
        FGuLiFlightEvent E; E.State=Geometry; E.bBootstrap=Bootstrap;
        E.FrozenUnitTypeId=V.FrozenUnitTypeId;
        E.bExplosiveFlight=Explosive;
        E.ExplosiveRadius=Explosive?V.Recipe.GroundAttack.ExplosiveRadius:0.f;
        if (!Explosive && V.SourceKind==EGuLiTargetKind::GroundActor && E.State.Phase==EGuLiCombatEffectPhase::Active)
        {
            E.bHasMuzzle=true;auto& Cue=E.Muzzle;
            Cue.MatchEpoch=CurrentEpoch();Cue.ShotId=V.Key.StableId;Cue.Source=E.State.Source;
            Cue.Start=E.State.LaunchLocation;Cue.MuzzleDirection=E.State.LaunchDirection;
            Cue.ServerTime=E.State.StartTime;Cue.MuzzleIndex=V.Recipe.GroundMuzzleIndex;Cue.SlotId=TEXT("BasicAttack");
            Cue.bMuzzleOnly=true;Cue.FrozenMuzzleVfxId=V.Recipe.GroundAttack.MuzzleVfxId;Cue.FrozenFireRate=V.Recipe.GroundAttack.FireRate;
        }
        else if (!Explosive && V.SourceKind==EGuLiTargetKind::CommanderSoldier
            && (V.FrozenUnitTypeId==1 || V.FrozenUnitTypeId==2)
            && V.Recipe.Context.WeaponBinding.SlotId==TEXT("BasicAttack")
            && E.State.Phase==EGuLiCombatEffectPhase::Active)
        {
            // The legacy attack-batch delegate is not the Hybrid transport.
            // Attach one cosmetic cue to the admitted flight, including owner
            // handoff recipes. Bootstrap stays silent in ApplyFlightEvent.
            E.bHasMuzzle=true;auto& Cue=E.Muzzle;
            Cue.MatchEpoch=CurrentEpoch();Cue.ShotId=V.Key.StableId;
            Cue.Source=E.State.Source;Cue.Target=E.State.Target;Cue.UnitTypeId=V.FrozenUnitTypeId;
            Cue.SlotId=TEXT("BasicAttack");Cue.MuzzleIndex=V.Recipe.GroundMuzzleIndex;
            Cue.Start=E.State.LaunchLocation;Cue.End=E.State.LastTargetLocation;
            Cue.MuzzleOffset=E.State.MuzzleOffset;Cue.MuzzleDirection=E.State.LaunchDirection;
            Cue.ServerTime=E.State.StartTime;Cue.MechanicalPoseTimeSeconds=E.State.StartTime;
            Cue.bMuzzleOnly=true;Cue.bMechanicalShot=true;
        }
        if (E.State.Phase==EGuLiCombatEffectPhase::Finished && E.State.Source.Kind==EGuLiTargetKind::GroundActor
            && (E.State.EndReason==EGuLiCombatEffectEndReason::Impact || E.State.EndReason==EGuLiCombatEffectEndReason::Blocked))
        {E.ImpactVfxId=Explosive?V.Recipe.GroundAttack.ExplosiveImpactVfxId:V.Recipe.GroundAttack.ImpactVfxId;E.bFrozenGroundImpact=true;}
        if (!UGuLiCombatEffectReplicationComponent::ApplyHybridFlight(Pool->GetWorld(),E)) return false;
        if (Bootstrap) return true;
        return Hybrid->PublishNewEffect(V.Key,[this,&V,&E](GuLiNet::Writer& Wire)
        {
            const float Sample=E.State.SampleTime;
            UGuLiCombatEffectReplicationComponent::RebasePublishedState(Pool->GetWorld(),Hybrid->GetObjectLogicalTick(V.Key),E.State);
            if (E.bHasMuzzle)
            {
                E.Muzzle.ServerTime+=E.State.SampleTime-Sample;
                if (E.Muzzle.bMechanicalShot) E.Muzzle.MechanicalPoseTimeSeconds+=E.State.SampleTime-Sample;
            }
            TArray<uint8> Visual,Private;
            if (GuLiFlightWire::EncodeBatch(MakeArrayView(&E,1),Visual)!=1) return false;
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_EncodePrivate);
                if (!EncodeRecord(V,Private)) return false;
            }
            Wire.U32(GuLiLinearState::EventTag);
            Wire.U32(GuLiLinearState::Version);
            Wire.Blob(std::span<const uint8>(Visual.GetData(),Visual.Num()));
            Wire.Blob(std::span<const uint8>(Private.GetData(),Private.Num()));
            return Wire.Good();
        });
    }
    bool EncodeRecord(const FRecord& V,TArray<uint8>& Out) const
    {
        GuLiLinearState::FPiece Piece;Piece.Epoch=CurrentEpoch();Piece.At=Hybrid->GetLocalLogicalSeconds();
        auto& Copy=Piece.Records.Add_GetRef(V);
        if (Pool->Matches(V.Handle)) Copy.Geometry=Pool->Slots[V.Handle.Slot].State;
        if (Pool->Matches(V.ExplosiveHandle)) Copy.ExplosiveGeometry=Pool->Slots[V.ExplosiveHandle.Slot].State;
        Copy.bLaunchPublished=Copy.Geometry.Phase!=EGuLiCombatEffectPhase::Waiting;Copy.bTerminalPublished=Copy.bFinished;
        Copy.bExplosiveLaunchPublished=Copy.ExplosiveGeometry.Sequence && Copy.ExplosiveGeometry.Phase!=EGuLiCombatEffectPhase::Waiting;
        Copy.bExplosiveTerminalPublished=Copy.bExplosiveFinished;
        FGuLiHybridStateArchive A(GuLiLinearFlightContract::MaximumRecordBytes);GuLiLinearState::Piece(A,Piece);return A.Finish(Out);
    }
    bool CacheRecord(const GuLiLinearState::FLedgerIdentity& O,TConstArrayView<uint8> Bytes,bool Birth)
    {
        GuLiLinearState::FPiece Data;
        if (!GuLiLinearState::Decode(Bytes,Data) || Data.Epoch!=CurrentEpoch() || Data.Records.Num()!=1 || !Data.Histories.IsEmpty()) return false;
        auto V=Data.Records[0];
        if (V.Key!=O.Key || V.Source!=O.Parent || V.Admission!=O.SpawnEvent) return false;
        if (O.LifeState==GuLiLinearState::Destroyed) return true;
        if (auto* Existing=Records.Find(O.Key))
        {
            if (Existing->Source!=V.Source || Existing->Admission!=V.Admission) return false;
            if (Birth || O.SimulationOwner==LocalPlayer() || Existing->Geometry.Sequence>V.Geometry.Sequence
                || Existing->ExplosiveGeometry.Sequence>V.ExplosiveGeometry.Sequence) return true;
        }
        else if (Records.Num()>=GuLiLinearState::MaximumRecords) return false;
        const float Offset=float(Hybrid->GetLocalLogicalSeconds()-Data.At);
        V.Recipe.ServerTime+=Offset;if (V.Recipe.ScheduledActivationSeconds>0) V.Recipe.ScheduledActivationSeconds+=Offset;
        V.Geometry.StartTime+=Offset;V.Geometry.SampleTime+=Offset;V.Geometry.ActivationTime+=Offset;V.Geometry.EndTime+=Offset;
        if(V.ExplosiveGeometry.Sequence){V.ExplosiveGeometry.StartTime+=Offset;V.ExplosiveGeometry.SampleTime+=Offset;V.ExplosiveGeometry.ActivationTime+=Offset;V.ExplosiveGeometry.EndTime+=Offset;}
        V.bAdmitted=true;V.Handle={};V.ExplosiveHandle={};Rebuild(V);RebuildExplosive(V);Records.Add(V.Key,V);return true;
    }
    bool CacheBirth(const FGuLiHybridObjectState& O)
    {
        const auto* Cap=O.Capabilities.Find(FName(UTF8_TO_TCHAR(GuLiNet::LinearFlightRecipe::Name)));
        if (!Cap || Cap->Version!=GuLiNet::LinearFlightRecipe::Version) return false;
        if (const auto* Existing=Records.Find(O.Key); Existing && Existing->BirthCapability
            && Existing->Source==O.Parent && Existing->Admission==O.SpawnEvent
            && *Existing->BirthCapability==Cap->Data)
            return true; // Same recipe already encoded/validated by LaunchHybrid.
        GuLiNet::LinearFlightRecipe::Recipe Recipe;
        if (!GuLiNet::LinearFlightRecipe::Decode(std::span<const uint8>(Cap->Data.GetData(),Cap->Data.Num()),Recipe)
            || Recipe.Flight!=GuLiNetBridge::ToCore(O.Key) || Recipe.Source!=GuLiNetBridge::ToCore(O.Parent)
            || Recipe.Admission!=GuLiNetBridge::ToCore(O.SpawnEvent)) return false;
        return CacheRecord(O,MakeArrayView(Recipe.State.data(),int32(Recipe.State.size())),true);
    }
    void Service(const FGuLiHybridObjectKey* Changed=nullptr)
    {
        if (bRestoring || !Pool.IsValid() || !Hybrid.IsValid() || !Hybrid->GetFailure().IsEmpty()) return;
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_LinearFlightService);
        const bool FastPath=GuLiLinearState::FlightServiceFastPath.GetValueOnGameThread()!=0;
        const bool CachedLookup=FastPath && GuLiLinearState::CacheFlightLifecycleLookup.GetValueOnGameThread()!=0;
        const FGuid Local=FastPath?LocalPlayer():FGuid{};
        const int32 Depth=ServiceDepth;
        TGuardValue<int32> DepthGuard(ServiceDepth,Depth+1);
        while (FastPath && ServiceKeyBuffers.Num()<=Depth) ServiceKeyBuffers.Add(MakeUnique<TArray<FServiceEntry>>());
        TArray<FServiceEntry> ComparisonKeys;
        TArray<FServiceEntry>& Keys=FastPath?*ServiceKeyBuffers[Depth]:ComparisonKeys;
        Keys.Reset();
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_ServiceKeys);
            if (Changed) Keys.Add({*Changed,Records.FindId(*Changed)});
            else
            {
                // These value keys cannot expire or become equal like UObject
                // map keys. Preserve the exact map order without GetKeys' extra
                // temporary uniqueness set on every pool/logical service.
                Keys.Reserve(Records.Num());
                for (auto It=Records.CreateConstIterator();It;++It) Keys.Add({It.Key(),It.GetId()});
            }
        }
        for (const auto& Entry:Keys)
        {
            const auto& Key=Entry.Key;
            // Sparse indices survive growth, but callbacks can erase/reuse or
            // restore entries. Check the complete identity before using a hint.
            auto* Pair=Records.IsValidId(Entry.Index)?&Records.Get(Entry.Index):nullptr;
            auto* V=Pair && Pair->Key==Key?&Pair->Value:Records.Find(Key);
            if (!V || !V->bRecipe) continue;
            if (V->bAdmitted && !V->bFinished && Pool->Matches(V->Handle) && V->Recipe.GroundAttack.ExplosiveDamage<=0)
            { if (!V->bLaunchPublished && !Hybrid->IsBattlePaused()) V->bLaunchPublished=Publish(*V,false); continue; }
            FGuLiHybridObjectState O;
            bool Registered;
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_ReadLifecycle);
                Registered=FastPath
                    ?Hybrid->GetObjectLifecycle(Key,O.SimulationOwner,O.LifeState,CachedLookup?&V->LifecycleLookup:nullptr)
                    :Hybrid->GetObjectView(Key,O);
            }
            if (Registered && O.LifeState==GuLiLinearState::Destroyed) {Discard(*V,TEXT("discard-numeric-destroyed"));continue;}
            if (!V->bAdmitted && !Registered)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_AdmissionResult);
                FGuLiHybridTransactionResult R;
                if (Hybrid->FindTransactionResult(V->Admission,R) && !R.IsCommitted()
                    && R.Status!=EGuLiHybridTransactionStatus::Deferred && R.Status!=EGuLiHybridTransactionStatus::Paused)
                { Discard(*V,TEXT("discard-admission-rejected")); continue; }
            }
            if (!Registered || O.SimulationOwner!=(FastPath?Local:LocalPlayer())) continue;
            // Activation only consumes the current owner and lifetime. A
            // finished record waiting for its already-submitted retirement
            // also needs no definition/health/relationship projection.
            // Keep the read at this point for every path that uses those fields.
            const bool NeedsView=(V->bFinished && ((!V->bDamageResolved && V->bHit) || !V->bRetirementSubmitted))
                || V->bExplosiveFinished;
            if (FastPath && NeedsView)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_ReadView);
                if (!Hybrid->GetObjectView(Key,O)) continue;
            }
            Activate(*V,O);
            if (V->Recipe.GroundAttack.ExplosiveDamage>0)
            {
                if (!V->bExplosiveLaunchPublished && Pool->Matches(V->ExplosiveHandle)) V->bExplosiveLaunchPublished=Publish(*V,false,true);
                if (V->bExplosiveFinished && !V->bExplosiveTerminalPublished) V->bExplosiveTerminalPublished=Publish(*V,false,true);
                if (V->bExplosiveFinished) ServiceBlast(*V,O);
            }
            if (!V->bFinished) continue;
            if (V->Recipe.Context.Source.Kind==EGuLiTargetKind::None && !Rebind(*V)) continue;
            if (!V->bTerminalPublished && !Hybrid->IsBattlePaused()) V->bTerminalPublished=Publish(*V,false);
            if (V->bHit && !V->bDamageResolved)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_DamageResult);
                FGuLiDamageCommitResult Result;
                if (Hybrid->FindDamageResult(V->Damage,Result))
                { V->bDamageResolved=true;V->bFirstDamageCommitted=Result.Status==EGuLiDamageCommitStatus::Committed||Result.Status==EGuLiDamageCommitStatus::Duplicate; if (Result.Status==EGuLiDamageCommitStatus::Committed) ++Pool->Stats.Hits; }
                else if (!V->bDamageSubmitted && !Hybrid->IsBattlePaused())
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_SubmitDamage);
                    FGuLiDamageRequest R; R.MatchEpoch=CurrentEpoch(); R.DamageEventId=V->Damage;
                    R.ShotId=V->Key.StableId; R.RootEventId=V->Recipe.Context.RootEventId; R.Source=V->Recipe.Context.Source;
                    R.WeaponBinding=V->Recipe.Context.WeaponBinding; R.SkillId=V->Recipe.Context.SkillId;
                    R.ProfileRevision=V->Recipe.Context.ProfileRevision; R.LoadoutRevision=V->Recipe.Context.LoadoutRevision;
                    R.Damage=V->Recipe.Context.Damage; R.HitLocation=V->Geometry.Location;
                    if (!Hybrid->ResolveObjectTarget(V->HitTarget,R.Target)) { Hybrid->Suspend(TEXT("Pending flight hit lost its stable target")); return; }
                    V->bDamageSubmitted=Hybrid->SubmitDamage(R,O.BornAt,{},&Key,V->DamageAt).IsValid();
                }
                if (!V->bDamageResolved) continue;
            }
            if(!ServiceRicochet(*V,O))continue;
            if(!ServiceBlast(*V,O))continue;
            if (!V->bRetirementSubmitted && V->bTerminalPublished && !Hybrid->IsBattlePaused()
                && (V->Recipe.GroundAttack.ExplosiveDamage<=0 || V->bExplosiveTerminalPublished))
            {
                if(!Hybrid->GetObjectView(Key,O))continue;
                if (!V->bRetirementPrepared)
                {
                    V->RetirementAt=Hybrid->GetObjectLogicalTick(Key);V->bRetirementPrepared=true;
                    if (!Publish(*V,false)) {V->bRetirementPrepared=false;continue;}
                }
                FGuLiHybridObjectRequest R; R.Source=R.Target=Key; R.Event=V->Retirement;
                R.Rule=FName(*(TEXT("retire/")+O.Definition.ToString())); R.Operation=EGuLiHybridObjectOperation::Destroy;
                R.ExpectedRelationRevision=O.RelationRevision;R.OriginalAt=V->RetirementAt;
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_SubmitRetirement);
                V->bRetirementSubmitted=Hybrid->SubmitObjectOperationNative(R,{}).IsValid();
            }
        }
    }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Out) const
    {
        if (!Pool.IsValid() || !Hybrid.IsValid()) return false;
        GuLiLinearState::FPiece Piece; Piece.Epoch=CurrentEpoch(); Piece.At=Hybrid->GetLocalLogicalSeconds();
        TSet<FGuid> Groups[2];
        for (const auto& Pair:Records)
        {
            const auto& V=Pair.Value;
            // Numeric admission can commit before this pool activates the
            // prepared recipe. Once the flight belongs to this export, its
            // private state follows its own key even if the source moved.
            const bool IncludesFlight=Objects.Contains(V.Key);
            if (IncludesFlight || Objects.Contains(V.Source)) Trace(Includes(Objects,V)?TEXT("capture-selected"):TEXT("capture-excluded"),V,&Objects);
            if (!V.bRecipe || !Includes(Objects,V)) continue;
            auto Copy=V;
            if (!V.bAdmitted && !IncludesFlight)
            { FGuLiHybridTransactionResult Admission; if (Hybrid->FindTransactionResult(V.Admission,Admission) && !Admission.IsCommitted()
                && Admission.Status!=EGuLiHybridTransactionStatus::Deferred && Admission.Status!=EGuLiHybridTransactionStatus::Paused) continue; }
            if (V.bHit && !V.bDamageResolved)
            { FGuLiDamageCommitResult Damage; if (Hybrid->FindDamageResult(V.Damage,Damage)){Copy.bDamageResolved=true;Copy.bFirstDamageCommitted=Damage.Status==EGuLiDamageCommitStatus::Committed||Damage.Status==EGuLiDamageCommitStatus::Duplicate;} }
            if (Pool->Matches(V.Handle)) { Copy.Geometry=Pool->Slots[V.Handle.Slot].State; Groups[Copy.SourceKind==EGuLiTargetKind::CommanderSoldier?1:0].Add(Copy.HistoryGroup); }
            if(Pool->Matches(V.ExplosiveHandle)){Copy.ExplosiveGeometry=Pool->Slots[V.ExplosiveHandle.Slot].State;Groups[0].Add(Copy.HistoryGroup);}
            FGuLiHybridStateArchive Check(GuLiLinearFlightContract::MaximumRecordBytes); GuLiLinearState::Record(Check,Copy);
            if (!Check.Good())
            {
                UE_LOG(LogNet,Error,TEXT("Linear flight private recipe rejected id=%s sourceKind=%u phase=%u at=%.9f sample=%.9f server=%.9f end=%.9f damage=%d/%d/%d binding=%d finished=%d retirement=%d/%d"),
                    *Copy.Key.StableId.ToString(),uint8(Copy.Source.Kind),uint8(Copy.Geometry.Phase),Piece.At,Copy.Geometry.SampleTime,
                    Copy.Recipe.ServerTime,Copy.Geometry.EndTime,Copy.bHit,Copy.bDamageSubmitted,Copy.bDamageResolved,
                    Copy.Recipe.Context.WeaponBinding.IsWellFormed(),Copy.bFinished,Copy.bRetirementPrepared,Copy.bRetirementSubmitted);
                return false;
            }
            Piece.Records.Add(Copy);
        }
        for (const auto& K:Objects) if (K.Kind==EGuLiHybridObjectKind::Flight && !Piece.Records.ContainsByPredicate([&](const auto& V){return V.Key==K;}))
        {
            const auto* V=Records.Find(K);
            FRecord Missing;Missing.Key=K;Trace(TEXT("capture-missing"),V?*V:Missing,&Objects);
            UE_LOG(LogNet,Error,TEXT("Linear flight private record missing id=%s epoch=%u at=%.6f recorded=%d recipe=%d admitted=%d records=%d owned=%d"),
                *K.StableId.ToString(),Piece.Epoch,Piece.At,V!=nullptr,V && V->bRecipe,V && V->bAdmitted,Records.Num(),Objects.Num());
            return false;
        }
        Piece.Records.Sort([](const auto& A,const auto& B){return A.Key.StableId<B.Key.StableId;});
        FGuLiHybridStateArchive A(GuLiLinearState::MaximumBytes); GuLiLinearState::Piece(A,Piece);
        if (A.Finish(Out)) return true;
        UE_LOG(LogNet,Error,TEXT("Linear flight private piece rejected epoch=%u at=%.6f records=%d histories=%d owned=%d"),
            Piece.Epoch,Piece.At,Piece.Records.Num(),Piece.Histories.Num(),Objects.Num());
        return false;
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects) const
    {
        TSet<FGuLiHybridObjectKey> Found;
        for (const auto& P:Pieces)
        {
            GuLiLinearState::FPiece Data; if (!GuLiLinearState::Decode(P.Bytes,Data)) return false;
            for (const auto& V:Data.Records)
            {
                FGuLiHybridObjectState Source;
                if (Found.Contains(V.Key) || !Hybrid->GetObjectMetadata(V.Source,Source)) return false;
                const FString DamageRule=FGuLiHybridManifest::MakeDamageRuleId(Source.Definition.ToString(),V.Recipe.Context.SkillId,V.Recipe.Context.WeaponBinding.SlotId);
                const auto* Rule=Manifest.Rules.FindByPredicate([&](const auto& R){return R.Id==TEXT("admit/linear/")+DamageRule;});
                if (!Rule || Rule->SpawnKind!=EGuLiHybridObjectKind::Flight || (V.bAdmitted && !P.Objects.Contains(V.Key))) return false;
                FGuLiHybridObjectState O;
                if (Hybrid->GetObject(V.Key,O) && (O.SpawnEvent!=V.Admission || O.Parent!=V.Source)) return false;
                Found.Add(V.Key);
            }
        }
        for (const auto& K:Objects) if (K.Kind==EGuLiHybridObjectKind::Flight && !Found.Contains(K)) return false;
        return true;
    }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage)
    {
        if (!Pool.IsValid() || !Hybrid.IsValid()) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach)
        {
            // A source only receives Detach when its shard commits. Other
            // flights still owned here must keep servicing afterwards.
            for (auto It=Records.CreateIterator();It;++It)
            {
                const auto& V=It.Value();if (!Includes(Objects,V)) continue;
                Trace(TEXT("detach-remove"),V,&Objects);
                for(const auto Handle:{V.Handle,V.ExplosiveHandle})
                {const int32 Slot=Handle.Slot;
                if (Pool->Slots.IsValidIndex(Slot) && Pool->Slots[Slot].Generation==Handle.Generation)
                {
                    const int32 Active=Pool->Slots[Slot].ActiveIndex;
                    Pool->ById.Remove(Pool->Slots[Slot].State.EffectId);
                    for (auto& Domain:Pool->DomainSlots) Domain.RemoveSingle(Slot);
                    if (Pool->ActiveSlots.IsValidIndex(Active)) Pool->ActiveSlots.RemoveAt(Active,1,EAllowShrinking::No);
                    for (int32 I=0;I<Pool->ActiveSlots.Num();++I) Pool->Slots[Pool->ActiveSlots[I]].ActiveIndex=I;
                    Pool->Slots[Slot]={};Pool->FreeSlots.AddUnique(Slot);
                }
                }
                It.RemoveCurrent();
            }
            Pool->Stats.Active=Pool->ActiveSlots.Num();return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Create)
        {
            bRestoring=true;
            for (const auto& P:Pieces)
            {
                GuLiLinearState::FPiece Data; if (!GuLiLinearState::Decode(P.Bytes,Data)) return false;
                for (auto V:Data.Records)
                {
                    const bool Selected=Includes(Objects,V);
                    FGuLiHybridObjectState O; const bool Registered=Hybrid->GetObject(V.Key,O);
                    if (Objects.Contains(V.Key) || Objects.Contains(V.Source)) Trace(Selected?TEXT("restore-create-selected"):TEXT("restore-create-excluded"),V,&Objects);
                    if (Registered && O.LifeState==GuLiLinearState::Destroyed) continue;
                    if (!Selected && !V.bAdmitted && !Registered) continue;
                    if (!V.bAdmitted && Registered)
                    {
                        V.bAdmitted=true; V.Recipe.ServerTime=FMath::Max(Data.At,V.Recipe.ScheduledActivationSeconds);
                        V.Geometry.SampleTime=V.Recipe.ServerTime;
                        V.Geometry.EndTime=V.Recipe.ServerTime+FMath::Min(V.Recipe.Lifetime,V.Recipe.MaximumDistance/V.Recipe.Speed);
                    }
                    if (!Selected) continue;
                    const float Offset=float(Hybrid->GetLocalLogicalSeconds()-Data.At);
                    V.Recipe.ServerTime+=Offset;if (V.Recipe.ScheduledActivationSeconds>0) V.Recipe.ScheduledActivationSeconds+=Offset;
                    V.Geometry.StartTime+=Offset;V.Geometry.SampleTime+=Offset;V.Geometry.ActivationTime+=Offset;V.Geometry.EndTime+=Offset;
        if(V.ExplosiveGeometry.Sequence){V.ExplosiveGeometry.StartTime+=Offset;V.ExplosiveGeometry.SampleTime+=Offset;V.ExplosiveGeometry.ActivationTime+=Offset;V.ExplosiveGeometry.EndTime+=Offset;}
                    if(!V.bDamageResolved)V.bDamageSubmitted=false;
                    if(!V.bPlanResolved)V.bPlanSubmitted=false;
                    for(auto& H:V.Hops)if(!H.Resolved)H.Submitted=false;
                    if(!V.bBlastResolved)V.bBlastSubmitted=false;
                    for(auto& H:V.BlastTargets)if(!H.Resolved)H.Submitted=false;
                    V.Handle={};V.ExplosiveHandle={}; Records.Add(V.Key,V);
                    Trace(TEXT("restore-created"),V,&Objects);
                }
                for (const auto& H:Data.Histories)
                {
                    bool Used=false;
                    for (const auto& K:Objects) if (const auto* V=Records.Find(K); V && V->HistoryGroup==H.Group) { Used=true; break; }
                    if (!Used) continue;
                    auto& History=Pool->HybridHistories[H.Domain].FindOrAdd(H.Group); History.PreviousTime=H.Time;
                    for (const auto& T:H.Targets)
                    { FGuLiTargetHandle Target; if (!Hybrid->ResolveObjectTarget(T.Key,Target)) return false; History.PreviousTargets.Add(Target,T.Position); }
                }
            }
            return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            for (auto& Pair:Records)
            {
                auto& V=Pair.Value;if (!Includes(Objects,V)) continue; if (!Rebind(V)) return false;
                if (!V.bFinished) Reserve(V);
            }
            return true;
        }
        for (auto& Pair:Records)
        {
            auto& V=Pair.Value;if (!Includes(Objects,V)) continue; FGuLiHybridObjectState O;
            if (Hybrid->GetObject(V.Key,O))
            {
                if (Objects.Contains(V.Key)) Activate(V,O);
                if (V.bAdmitted && V.Geometry.Phase==EGuLiCombatEffectPhase::Active && !Publish(V,true)) return false;
                V.bLaunchPublished=V.Geometry.Phase!=EGuLiCombatEffectPhase::Waiting;
                V.bTerminalPublished=V.bFinished;
                if(V.Recipe.GroundAttack.ExplosiveDamage>0 && V.bAdmitted && !V.bExplosiveFinished && !Publish(V,true,true))return false;
                V.bExplosiveLaunchPublished=V.ExplosiveGeometry.Phase!=EGuLiCombatEffectPhase::Waiting;V.bExplosiveTerminalPublished=V.bExplosiveFinished;
            }
        }
        bRestoring=false; return true;
    }
};

class FGuLiLinearFlightAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakPtr<FGuLiHybridProjectileState> State;
    GuLiLinearState::FLedgerIdentity Object;
    bool Private(TConstArrayView<uint8> Bytes) const
    { FGuLiHybridStateArchive A(Bytes); uint32 Version=0; FGuLiHybridObjectKey Key; A.Value(Version); A.Value(Key); return A.Done() && Version==GuLiLinearState::Version && Key==Object.Key; }
public:
    FGuLiLinearFlightAdapter(TSharedPtr<FGuLiHybridProjectileState> S):State(S) {}
    UObject* LifetimeOwner() const override { const auto S=State.Pin(); return S?S->Pool.Get():nullptr; }
    uint32 Version() const override { return GuLiLinearState::Version; }
    TArray<FName> RequiredSharedStateSystems() const override { return {GuLiLinearState::System}; }
    bool Prepare(const FGuLiHybridObjectState& O) override { const auto S=State.Pin(); if (!S || !S->Supported(O) || !S->CacheBirth(O)) return false; Object=O; return true; }
    void SetAuthority(const FGuLiHybridObjectState& O,bool Local) override
    { Object=O; if (const auto S=State.Pin()) if (auto* V=S->Records.Find(O.Key); V && Local && !S->bRestoring) S->Activate(*V,O); }
    // The shared LinearFlights system owns the original simulation cadence.
    // Per-object age and ledger delivery continue even when this empty family
    // callback is skipped; actor/entity work retains the bridge's own eligibility.
    uint32 AdvancePeriod() const override
    {return GuLiLinearState::FlightServiceFastPath.GetValueOnGameThread()!=0?0:1;}
    void Advance(uint64,double) override {}
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override { return false; }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 At) override
    {
        const auto S=State.Pin(); if (!S || Object.SimulationOwner==S->LocalPlayer()) return false;
        GuLiNet::Reader Prefix(std::span<const uint8>(Bytes.GetData(),Bytes.Num()));uint32 Tag;
        if(Prefix.U32(Tag)&&Tag==GuLiLinearState::RicochetTag)
        {
            uint32 Version;GuLiNet::Bytes Visual;if(!Prefix.U32(Version)||Version!=GuLiLinearState::Version||!Prefix.Blob(Visual,GuLiFlightWire::MaximumBytes)||!Prefix.Done())return false;
            TArray<uint8> Body;Body.Append(Visual.data(),int32(Visual.size()));TArray<FGuLiFlightEvent> Events;if(!GuLiFlightWire::DecodeBatch(Body,Events))return false;
            for(auto& E:Events)
            {
                if(!E.bInstantGroundSegment||E.State.MatchEpoch!=S->CurrentEpoch())return false;
                UGuLiCombatEffectReplicationComponent::RebaseReceivedState(S->Pool->GetWorld(),At,E.State);
                if(!UGuLiCombatEffectReplicationComponent::ApplyHybridFlight(S->Pool->GetWorld(),E))return false;
            }return true;
        }
        Prefix=GuLiNet::Reader(std::span<const uint8>(Bytes.GetData(),Bytes.Num()));
        if (Prefix.U32(Tag) && Tag==GuLiLinearState::EventTag) return ApplyPrivateEvent(Bytes,At);
        TArray<uint8> Body; Body.Append(Bytes.GetData(),Bytes.Num()); TArray<FGuLiFlightEvent> Events;
        if (!GuLiFlightWire::DecodeBatch(Body,Events) || Events.Num()!=1) return false;
        auto E=Events[0];
        const auto ExpectedId=E.bExplosiveFlight?GuLiNetBridge::ToUE(GuLiNet::GroundRogue::BlastEvent(GuLiNetBridge::ToCore(Object.Key.StableId))):Object.Key.StableId;
        if (E.State.EffectId!=ExpectedId || E.State.MatchEpoch!=S->CurrentEpoch() || E.State.Kind!=EGuLiCombatEffectKind::LinearProjectile) return false;
        const float Before=E.State.SampleTime;
        UGuLiCombatEffectReplicationComponent::RebaseReceivedState(S->Pool->GetWorld(),At,E.State);
        if (E.bHasMuzzle)
        {
            E.Muzzle.ServerTime+=E.State.SampleTime-Before;
            if (E.Muzzle.bMechanicalShot) E.Muzzle.MechanicalPoseTimeSeconds+=E.State.SampleTime-Before;
        }
        if (auto* V=S->Records.Find(Object.Key))
        {
            V->Recipe.Context.Source=E.State.Source;V->Recipe.Context.Target=E.State.Target;
            if(E.bExplosiveFlight)
            {if(V->Recipe.GroundAttack.ExplosiveDamage<=0)return false;
             if(V->ExplosiveGeometry.Sequence<=E.State.Sequence && !V->bExplosiveFinished)V->ExplosiveGeometry=E.State;}
            else if (V->Geometry.Sequence<=E.State.Sequence && !V->bFinished)
            {
                V->Geometry=E.State;V->Recipe.ServerTime=E.State.StartTime;
                if (V->Recipe.ScheduledActivationSeconds>0) V->Recipe.ScheduledActivationSeconds=E.State.ActivationTime;
            }
        }
        return UGuLiCombatEffectReplicationComponent::ApplyHybridFlight(S->Pool->GetWorld(),E,Object.LifeState>=2);
    }
    bool ApplyPrivateEvent(TConstArrayView<uint8> Bytes,uint64 At)
    {
        const auto S=State.Pin();if (!S) return false;
        GuLiNet::Reader Wire(std::span<const uint8>(Bytes.GetData(),Bytes.Num()));uint32 Tag,Version;GuLiNet::Bytes Visual,Private;
        if (!Wire.U32(Tag) || Tag!=GuLiLinearState::EventTag || !Wire.U32(Version) || Version!=GuLiLinearState::Version
            || !Wire.Blob(Visual) || !Wire.Blob(Private,GuLiLinearFlightContract::MaximumRecordBytes) || !Wire.Done()
            || !S->CacheRecord(Object,MakeArrayView(Private.data(),int32(Private.size())),false)) return false;
        return ApplyMotion(MakeArrayView(Visual.data(),int32(Visual.size())),At);
    }
    void ApplyLedger(const FGuLiHybridObjectState& O,uint64,bool) override { Object=O; }


    bool Capture(TArray<uint8>& Bytes) const override
    { FGuLiHybridStateArchive A; uint32 V=Version(); auto K=Object.Key; A.Value(V); A.Value(K); return A.Finish(Bytes); }
    bool Validate(TConstArrayView<uint8> Bytes) const override { return Private(Bytes); }


    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage) override { return Private(Bytes); }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage) override { return Private(Bytes); }
    void Release() override {}
};

class FGuLiLinearFlightSystem final:public IGuLiHybridSharedStateAdapter
{
    TWeakPtr<FGuLiHybridProjectileState> State;
public:
    explicit FGuLiLinearFlightSystem(TSharedPtr<FGuLiHybridProjectileState> S):State(S) {}
    UObject* LifetimeOwner() const override { const auto S=State.Pin(); return S?S->Pool.Get():nullptr; }
    uint32 Version() const override { return GuLiLinearState::Version; }
    bool UsesObject(const FGuLiHybridObjectKey& K) const override
    {
        const auto S=State.Pin();if (!S) return false;
        if (const auto* R=S->Records.Find(K);R && R->Key==K) return true;
        // Unregistered recipes still follow their source. Registered flights
        // follow their own complete key, including life generation.
        for (const auto& P:S->Records)
        {
            const auto& R=P.Value;if (R.bAdmitted || R.Source!=K) continue;
            FGuid Owner;uint8 Life;if (!S->Hybrid->GetObjectLifecycle(R.Key,Owner,Life)) return true;
        }
        return false;
    }
    void Advance(uint64 At,double) override
    { if (const auto S=State.Pin()) { S->Service(); if (!(At%2)) S->Pool->Step(static_cast<float>(At)/60.f); } }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    { const auto S=State.Pin(); return S && S->Capture(Objects,Bytes); }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects) const override
    { const auto S=State.Pin(); return S && S->Validate(Pieces,Objects); }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    { const auto S=State.Pin(); return S && S->Restore(Pieces,Objects,Stage); }
    bool RestoreQuery(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    {return Restore(Pieces,Objects,Stage);}
};

void UGuLiProjectilePoolSubsystem::InitializeHybrid(FSubsystemCollectionBase& Collection)
{
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld())) return;
    Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!H) return;
    HybridState=MakeShared<FGuLiHybridProjectileState>(*this,*H); FString Error;
    if (!FGuLiHybridManifest::Read(UGuLiNetworkSettings::Resolve(GetWorld()).ManifestFile,HybridState->Manifest,Error)
        || !FGuLiHybridAdapterRegistry::Validate(HybridState->Manifest,Error)) { H->Suspend(Error); return; }
    if (!H->RegisterSharedStateSystem(GuLiLinearState::System,MakeShared<FGuLiLinearFlightSystem>(HybridState)))
        H->Suspend(TEXT("Linear flight factory or checkpoint registration failed"));
}
TSharedPtr<IGuLiHybridSimulationAdapter> UGuLiProjectilePoolSubsystem::CreateHybridAdapter(const FGuLiHybridObjectState& Object)
{return HybridState && HybridState->Supported(Object)?MakeShared<FGuLiLinearFlightAdapter>(HybridState):TSharedPtr<IGuLiHybridSimulationAdapter>{};}
namespace
{
const FGuLiHybridAdapterRegistration LinearFlightFamily({TEXT("LinearFlight"),EGuLiHybridObjectKind::Flight,GuLiLinearFlightContract::StateVersion,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState& Object)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {auto* Pool=Owner.GetWorld()->GetSubsystem<UGuLiProjectilePoolSubsystem>();return Pool?Pool->CreateHybridAdapter(Object):nullptr;},nullptr,nullptr,EGuLiHybridOwnershipPolicy::RetainedSource,3,false});
}
void UGuLiProjectilePoolSubsystem::ResetHybrid()
{
    if (!HybridState) return;
    for (const auto& Pair:HybridState->Records) HybridState->Trace(TEXT("reset"),Pair.Value);
    HybridState->Records.Reset();
#if WITH_EDITOR
    HybridState->RecentGroundShots.Reset();
    HybridState->RecentMassShots.Reset();
#endif
}
bool UGuLiProjectilePoolSubsystem::IsTickable() const { return !IsTemplate() && (HybridState.IsValid() || !ScheduledLaunches.IsEmpty()); }
TStatId UGuLiProjectilePoolSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiProjectileTransactions,STATGROUP_Tickables); }
void UGuLiProjectilePoolSubsystem::Tick(float)
{
    if (HybridState) { HybridState->Service();auto Hits=MoveTemp(PendingGroundHits);PendingGroundHits.Reset();for(const auto& H:Hits)OnGroundWeaponHit.Broadcast(H.Hit,H.Snapshot);return; }
    if (!GetWorld() || !Ledger) return;
    BeginEpoch(Ledger->GetMatchEpoch()); TArray<FGuid> Keys; ScheduledLaunches.GetKeys(Keys);
    for (const auto& Key:Keys) if (const auto* Pending=ScheduledLaunches.Find(Key); Pending && Pending->Recipe.ServerTime<=GetWorld()->GetTimeSeconds()+.0001f)
    {
        const auto Copy=*Pending; ScheduledLaunches.Remove(Key);
        ActivateLaunch(Copy.Recipe,Copy.Team,Copy.Lease,Copy.Handle.Slot);
    }
}
bool UGuLiProjectilePoolSubsystem::IsPendingHybrid(FGuLiProjectilePoolHandle H) const
{
    if (!H.IsValid() || H.MatchEpoch!=Epoch || !Slots.IsValidIndex(H.Slot) || Slots[H.Slot].Generation!=H.Generation) return false;
    if (const auto* P=ScheduledLaunches.Find(Slots[H.Slot].State.EffectId); P && P->Handle.Generation==H.Generation) return true;
    if (!HybridState) return false;
    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=Slots[H.Slot].State.EffectId; Key.LifeGeneration=1;
    const auto* R=HybridState->Records.Find(Key);
    return R && R->Handle.Slot==H.Slot && R->Handle.Generation==H.Generation && !R->bFinished;
}
bool UGuLiProjectilePoolSubsystem::HasTerminalFlightResult(const FGuid& Id) const
{
    const auto* H=HybridState?HybridState->Hybrid.Get():nullptr;
    if (!H || !Id.IsValid()) return false;
    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=Id; Key.LifeGeneration=1;
    FGuLiHybridObjectState Object;
    if (H->GetObject(Key,Object)) return Object.LifeState>=2;
    FGuLiHybridTransactionResult Result;
    return H->FindTransactionResult(GuLiLinearState::Event(Key,TEXT("/admission")),Result)
        && !Result.IsCommitted() && Result.Status!=EGuLiHybridTransactionStatus::Deferred
        && Result.Status!=EGuLiHybridTransactionStatus::Paused;
}
FGuLiProjectilePoolHandle UGuLiProjectilePoolSubsystem::LaunchHybrid(const FGuLiPooledProjectileLaunch& R,EGuLiTeam Team)
{
    auto S=HybridState; auto* H=S?S->Hybrid.Get():nullptr; FGuLiHybridObjectKey Source,Target;
    if (!H || H->IsBattlePaused() || !H->ResolveTarget(R.Context.Source,Source) || !H->HasSimulation(Source)
        || (R.Context.Target.IsValid() && !H->ResolveTarget(R.Context.Target,Target)) || S->Records.Num()>=GuLiLinearState::MaximumRecords) return {};
    FGuLiHybridObjectState Parent; if (!H->GetObject(Source,Parent)) return {};
    const FString DamageRule=FGuLiHybridManifest::MakeDamageRuleId(Parent.Definition.ToString(),R.Context.SkillId,R.Context.WeaponBinding.SlotId);
    const auto* Rule=S->Manifest.Rules.FindByPredicate([&](const auto& C){return C.Id==TEXT("admit/linear/")+DamageRule;});
    if (!Rule || Rule->SpawnKind!=EGuLiHybridObjectKind::Flight) return {};
    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=R.Context.ShotId; Key.LifeGeneration=1;
    // The primary pool slot may already be gone while its companion or damage
    // receipt is still alive. Replaying Launch must not replace that record.
    if (S->Records.Contains(Key)) return {};
    auto& V=S->Records.Add(Key); V.Key=Key; V.Source=Source; V.Target=Target; V.Team=Team;
    V.SourceKind=R.Context.Emitter.IsValid()?EGuLiTargetKind::Wingman:R.Context.Source.Kind;
    if(V.SourceKind==EGuLiTargetKind::CommanderSoldier&&Parent.Definition.ToString().StartsWith(TEXT("unit/")))
        LexTryParseString(V.FrozenUnitTypeId,*Parent.Definition.ToString().Mid(5));
    V.Recipe=R; V.Recipe.SourceLease={}; V.Recipe.ServerTime=H->GetLocalLogicalSeconds(); V.bRecipe=true; V.HistoryGroup=S->LocalPlayer();
    if (R.ScheduledActivationSeconds>V.Recipe.ServerTime)
    {
        if (!R.bAcceptanceAdditional || R.ScheduledActivationSeconds>V.Recipe.ServerTime+FMath::Min(R.Lifetime,10.f)+.001f) { S->Records.Remove(Key); return {}; }
        V.Recipe.ServerTime=R.ScheduledActivationSeconds;
    }
    V.Admission=GuLiLinearState::Event(Key,TEXT("/admission")); V.Retirement=GuLiLinearState::Event(Key,TEXT("/retirement"));
    V.Damage=GuLiLinearState::Event(Key,TEXT("/damage")); V.Geometry.Sequence=1;
    V.Geometry.Phase=EGuLiCombatEffectPhase::Waiting; V.Geometry.Location=R.Position; V.Geometry.Velocity=R.Direction*R.Speed;
    V.Geometry.SampleTime=V.Recipe.ServerTime; V.Geometry.EndTime=V.Recipe.ServerTime+FMath::Min(R.Lifetime,R.MaximumDistance/R.Speed); S->Rebuild(V);
    FGuLiCombatTargetSnapshot TargetSnapshot;
    if (Ledger->TryGetTargetSnapshot(R.Context.Target,TargetSnapshot)) V.Geometry.LastTargetLocation=TargetSnapshot.Location;
    FGuLiHybridObjectRequest Request; Request.Source=Source; Request.Target=Key; Request.Event=V.Admission;
    Request.Rule=FName(*Rule->Id); Request.Operation=EGuLiHybridObjectOperation::Spawn; Request.Location=R.Position;
    TArray<uint8> Birth;
    if (!S->EncodeRecord(V,Birth)) {S->Records.Remove(Key);return {};}
    GuLiNet::LinearFlightRecipe::Recipe Frozen{GuLiNetBridge::ToCore(Key),GuLiNetBridge::ToCore(Source),GuLiNetBridge::ToCore(V.Admission),
        GuLiNet::Bytes(Birth.GetData(),Birth.GetData()+Birth.Num())};
    const auto Payload=GuLiNet::LinearFlightRecipe::Encode(Frozen);
    if (Payload.empty()) {S->Records.Remove(Key);return {};}
    Request.CapabilityPayload.Append(Payload.data(),int32(Payload.size()));
    V.BirthCapability=MakeShared<const TArray<uint8>>(Request.CapabilityPayload);
    S->Trace(TEXT("recipe-ready"),V);
    if (!H->SubmitObjectOperationNative(Request,{}).IsValid()) { S->Records.Remove(Key); return {}; }
    // Submit can enter adapter callbacks. Re-find solely for diagnostics rather
    // than dereferencing a possibly relocated TMap value in the trace itself.
    if (const auto* Current=S->Records.Find(Key)) S->Trace(TEXT("submit-return"),*Current);
    if (auto* Current=S->Records.Find(Key)) return S->Reserve(*Current);
    return {};
}
void UGuLiProjectilePoolSubsystem::RetireHybrid(const FGuLiCombatEffectState& State,const FGuLiCombatEffectContext& Context,
    EGuLiCombatEffectEndReason Reason,const FVector& Position,float Time,const FGuLiTargetHandle* Target)
{
    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=State.EffectId; Key.LifeGeneration=1;
    auto S=HybridState;auto* V=S?S->Records.Find(Key):nullptr;
    if(!V && S && Context.RootEventId.IsValid() && State.EffectId==GuLiNetBridge::ToUE(GuLiNet::GroundRogue::BlastEvent(GuLiNetBridge::ToCore(Context.RootEventId))))
    {
        Key.StableId=Context.RootEventId;V=S->Records.Find(Key);
        if(!V || !S->Hybrid.IsValid() || V->bExplosiveFinished)return;
        V->ExplosiveGeometry=State;auto& X=V->ExplosiveGeometry;
        X.Phase=EGuLiCombatEffectPhase::Finished;X.EndReason=Reason;X.Location=Position;X.SampleTime=FMath::Max(Time,X.StartTime);++X.Sequence;
        V->bExplosiveFinished=true;V->ExplosiveHandle={};V->ExplosiveImpactAt=S->Hybrid->GetObjectLogicalTick(Key);
        S->FreezeBlast(*V);V->bExplosiveTerminalPublished=S->Publish(*V,false,true);OnState.Broadcast(X,true);
        S->Service(bStepping?&Key:nullptr);return;
    }
    if(!V || !S->Hybrid.IsValid())return;
    V->Geometry=State; V->Geometry.Phase=EGuLiCombatEffectPhase::Finished; V->Geometry.EndReason=Reason;
    V->Geometry.Location=Position; V->Geometry.SampleTime=FMath::Max(Time,State.StartTime); ++V->Geometry.Sequence;
    V->bFinished=true; V->Handle={};
    V->DamageAt=S->Hybrid->GetObjectLogicalTick(Key);
    if (Target)
    { if (!S->Hybrid->ResolveTarget(*Target,V->HitTarget)) { S->Hybrid->Suspend(TEXT("Flight hit lacks a stable target")); return; } V->bHit=true;V->DamageAt=S->Hybrid->GetObjectLogicalTick(Key); }
    S->FreezeRicochet(*V);
    if (Reason==EGuLiCombatEffectEndReason::Blocked) ++Stats.Blocked;
    V->bTerminalPublished=S->Publish(*V,false); OnState.Broadcast(V->Geometry,true);
    // The shared logical step services every record before stepping the pool.
    // Retiring one slot changes only this record; transaction results are applied
    // by the ordered main-thread pump, never in this synchronous pool traversal.
    // Preserve immediate hit/retirement submission without rescanning every other
    // admitted or waiting flight for each projectile that finishes in the step.
    S->Service(bStepping?&Key:nullptr);
}


FString UGuLiProjectilePoolSubsystem::GroundShotDiagnostics() const
{
#if WITH_EDITOR
    const auto S=HybridState;if(!S || !S->Hybrid.IsValid())return TEXT("{}");
    TArray<TSharedPtr<FJsonValue>> Rows;
    auto Add=[&](const GuLiLinearState::FRecord& V)
    {
        if(Rows.Num()>=1024)return;
        auto J=MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("source_kind"),uint8(V.SourceKind));
        J->SetStringField(TEXT("shot"),V.Key.StableId.ToString(EGuidFormats::Digits));
        J->SetStringField(TEXT("target"),V.HitTarget.StableId.ToString(EGuidFormats::Digits));
        J->SetBoolField(TEXT("admitted"),V.bAdmitted);J->SetBoolField(TEXT("fire_notify"),V.Recipe.FireOrdinal>0);J->SetNumberField(TEXT("fire_ordinal"),double(V.Recipe.FireOrdinal));
        FGuLiHybridTransactionResult Admission;
        J->SetStringField(TEXT("admission_event"),V.Admission.ToString(EGuidFormats::Digits));
        J->SetBoolField(TEXT("admission_receipt_found"),S->Hybrid->FindTransactionResult(V.Admission,Admission));
        J->SetNumberField(TEXT("admission_status"),uint8(Admission.Status));
        J->SetBoolField(TEXT("attack_frozen"),V.bAttackFrozen);J->SetNumberField(TEXT("fire_time"),V.Recipe.FireNotifyTime);J->SetNumberField(TEXT("activation_time"),V.Recipe.ServerTime);
        J->SetNumberField(TEXT("muzzle"),V.Recipe.GroundMuzzleIndex);J->SetNumberField(TEXT("frozen_damage"),V.Recipe.Context.Damage);
        J->SetStringField(TEXT("muzzle_position"),V.Recipe.Position.ToString());J->SetStringField(TEXT("direction"),V.Recipe.Direction.ToString());
        J->SetNumberField(TEXT("build_revision"),double(V.Recipe.GroundAttack.Revision));
        J->SetBoolField(TEXT("collision_hit"),V.bHit);J->SetNumberField(TEXT("end_reason"),uint8(V.Geometry.EndReason));
        J->SetStringField(TEXT("hit_position"),FVector(V.Geometry.Location).ToString());
        J->SetNumberField(TEXT("primary_terminal_sample_time"),V.bFinished?V.Geometry.SampleTime:0);
        J->SetBoolField(TEXT("damage_submitted"),V.bDamageSubmitted);J->SetBoolField(TEXT("damage_resolved"),V.bDamageResolved);
        FGuLiDamageCommitResult Result;const bool Found=S->Hybrid->FindDamageResult(V.Damage,Result);
        J->SetStringField(TEXT("damage_event"),V.Damage.ToString(EGuidFormats::Digits));J->SetBoolField(TEXT("receipt_found"),Found);
        if(Found){J->SetNumberField(TEXT("status"),uint8(Result.Status));J->SetNumberField(TEXT("applied"),Result.AppliedDamage);
            J->SetNumberField(TEXT("absorbed"),Result.AbsorbedDamage);J->SetNumberField(TEXT("remaining_health"),Result.RemainingHealth);}
        FGuLiHybridObjectState Target;
        if(V.HitTarget.IsValid() && S->Hybrid->GetObjectView(V.HitTarget,Target))
        {J->SetNumberField(TEXT("target_health_now"),Target.Health.Health);J->SetNumberField(TEXT("target_shield_now"),Target.Health.Shield);}
        J->SetBoolField(TEXT("explosive_finished"),V.bExplosiveFinished);J->SetNumberField(TEXT("explosive_damage"),V.Recipe.GroundAttack.ExplosiveDamage);
        J->SetStringField(TEXT("explosive_position"),FVector(V.ExplosiveGeometry.Location).ToString());
        J->SetStringField(TEXT("explosive_launch_position"),FVector(V.ExplosiveGeometry.LaunchLocation).ToString());
        J->SetStringField(TEXT("explosive_launch_direction"),FVector(V.ExplosiveGeometry.LaunchDirection).ToString());
        J->SetNumberField(TEXT("explosive_start_time"),V.ExplosiveGeometry.StartTime);
        J->SetNumberField(TEXT("explosive_terminal_sample_time"),V.bExplosiveFinished?V.ExplosiveGeometry.SampleTime:0);
        J->SetNumberField(TEXT("explosive_end_time"),V.ExplosiveGeometry.EndTime);
        J->SetNumberField(TEXT("explosive_speed"),V.ExplosiveGeometry.Motion.Speed);J->SetNumberField(TEXT("explosive_end_reason"),uint8(V.ExplosiveGeometry.EndReason));
        Rows.Add(MakeShared<FJsonValueObject>(J));
    };
    for(int32 I=S->RecentGroundShots.Num()-1;I>=0;--I)Add(S->RecentGroundShots[I]);
    for(const auto& Pair:S->Records)if(Pair.Value.SourceKind==EGuLiTargetKind::GroundActor)Add(Pair.Value);
    auto Root=MakeShared<FJsonObject>();Root->SetArrayField(TEXT("shots"),Rows);Rows.Reset();
    for(const auto& Shot:S->RecentMassShots)Add(Shot);Root->SetArrayField(TEXT("mass_shots"),Rows);FString Json;
    FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
#else
    return TEXT("{}");
#endif
}
