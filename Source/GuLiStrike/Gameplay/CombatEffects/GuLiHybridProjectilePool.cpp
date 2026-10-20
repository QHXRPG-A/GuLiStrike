#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
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
#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Gameplay/CombatEffects/GuLiLinearFlightContract.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/SubsystemCollection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace GuLiLinearState
{
constexpr uint32 Version=GuLiLinearFlightContract::StateVersion;
constexpr uint32 MaximumRecords=16384;
constexpr uint32 MaximumBytes=16*1024*1024;
constexpr uint32 EventTag=0x47554c46;
const FName System(TEXT("LinearFlights"));
constexpr uint8 Alive=1,Destroyed=3;
struct FRecord
{
    FGuLiHybridObjectKey Key,Source,Target,HitTarget;
    FGuLiPooledProjectileLaunch Recipe;
    FGuLiCombatEffectState Geometry;
    EGuLiTeam Team=EGuLiTeam::Unassigned;
    EGuLiTargetKind SourceKind=EGuLiTargetKind::None;
    FGuid Admission,Retirement,Damage,HistoryGroup;
    uint64 DamageAt=0,RetirementAt=0;
    FGuLiProjectilePoolHandle Handle;
    bool bRecipe=false,bAdmitted=false,bFinished=false,bHit=false,bDamageSubmitted=false,bDamageResolved=false,bRetirementSubmitted=false;
    bool bLaunchPublished=false,bTerminalPublished=false;
    bool bRetirementPrepared=false;
};
struct FPrevious { FGuLiHybridObjectKey Key; FVector Position=FVector::ZeroVector; };
struct FHistory { FGuid Group; uint8 Domain=0; float Time=0; TArray<FPrevious> Targets; };
struct FPiece { uint32 Epoch=0; float At=0; TArray<FRecord> Records; TArray<FHistory> Histories; };
void Record(FGuLiHybridStateArchive& A,FRecord& V)
{
    A.Value(V.Key); A.Value(V.Source); A.Value(V.Target); A.Value(V.HitTarget);
    A.Enum(V.Team,2); A.Enum(V.SourceKind,4); A.Value(V.Admission); A.Value(V.Retirement); A.Value(V.Damage); A.Value(V.HistoryGroup);
    auto& R=V.Recipe; auto& C=R.Context; auto& B=C.WeaponBinding;
    A.Value(C.MatchEpoch); A.Value(C.SkillId); A.Value(C.EffectConfigId); A.Value(C.Damage); A.Value(C.ShotId); A.Value(C.RootEventId);
    A.Value(C.ProfileRevision); A.Value(C.LoadoutRevision); A.Value(B.MatchEpoch); A.Enum(B.Team,2);
    FGuLiHybridReflectedState::Value(A,C.Emitter);
    A.Value(B.OwnerPlayerGuid); A.Enum(B.Domain,2); A.Value(B.SubjectId); A.Value(B.SlotId);
    A.Value(R.Position); A.Value(R.Direction); A.Value(R.MuzzleOffset); A.Value(R.Speed); A.Value(R.Lifetime);
    A.Value(R.MaximumDistance); A.Value(R.SweepRadius); A.Value(R.ServerTime); A.Value(R.PlayerBulletVfxId); A.Value(R.bAcceptanceAdditional);
    A.Value(R.ScheduledActivationSeconds);
    FVector Position=V.Geometry.Location,Velocity=V.Geometry.Velocity,LastTarget=V.Geometry.LastTargetLocation;
    A.Value(Position); A.Value(Velocity); A.Value(V.Geometry.SampleTime); A.Value(V.Geometry.EndTime); A.Value(V.Geometry.Sequence);
    A.Enum(V.Geometry.Phase,3); A.Enum(V.Geometry.EndReason,6);
    A.Value(LastTarget);
    if (A.IsReading()) { V.Geometry.Location=Position; V.Geometry.Velocity=Velocity; V.Geometry.LastTargetLocation=LastTarget; }
    A.Value(V.bRecipe); A.Value(V.bAdmitted); A.Value(V.bFinished); A.Value(V.bHit);
    A.Value(V.bDamageSubmitted); A.Value(V.bDamageResolved); A.Value(V.bRetirementSubmitted);
    A.Value(V.bLaunchPublished); A.Value(V.bTerminalPublished);
    A.Value(V.DamageAt);A.Value(V.RetirementAt);A.Value(V.bRetirementPrepared);
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
        || (V.bRetirementSubmitted && !V.bRetirementPrepared)) A.Reject();
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
    bool bReportedInvalidRecord=false;
    for (const auto& R:V.Records)
    {
        const bool Duplicate=Keys.Contains(R.Key);
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
        Keys.Add(R.Key);
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
    void Rebuild(FRecord& V)
    {
        auto& S=V.Geometry; const auto& R=V.Recipe;
        S.EffectId=V.Key.StableId; S.MatchEpoch=CurrentEpoch(); S.Kind=EGuLiCombatEffectKind::LinearProjectile;
        S.Source=V.Recipe.Context.Source; S.Target=V.Recipe.Context.Target; S.SourceTeam=V.Team;
        S.SlotId=V.Recipe.Context.WeaponBinding.SlotId;
        S.PlayerBulletVfxId=R.PlayerBulletVfxId; S.bAcceptanceAdditional=R.bAcceptanceAdditional;
        S.Motion.Speed=R.Speed; S.Motion.SweepRadius=R.SweepRadius; S.Motion.MaximumLifetime=R.Lifetime;
        S.LaunchLocation=R.Position; S.LaunchDirection=R.Direction; S.MuzzleOffset=R.MuzzleOffset;
        S.StartTime=S.ActivationTime=R.ServerTime;
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
        V.Recipe.Context.MatchEpoch=V.Recipe.Context.WeaponBinding.MatchEpoch=CurrentEpoch(); Rebuild(V); return true;
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
        P->ById.Remove(V.Key.StableId);
        if (P->Slots.IsValidIndex(V.Handle.Slot) && P->Slots[V.Handle.Slot].Generation==V.Handle.Generation
            && P->Slots[V.Handle.Slot].ActiveIndex==INDEX_NONE)
        { P->Slots[V.Handle.Slot]={}; P->FreeSlots.AddUnique(V.Handle.Slot); }
        Records.Remove(V.Key);
    }
    void Activate(FRecord& V,const FGuLiHybridObjectState& Object)
    {
        auto* P=Pool.Get(); if (!P || V.bFinished || !V.bRecipe || Object.SimulationOwner!=LocalPlayer() || Object.LifeState!=GuLiLinearState::Alive) return;
        if (!V.bAdmitted) Trace(TEXT("admission-observed"),V);
        if (!V.bAdmitted && !bRestoring)
        {
            V.Recipe.ServerTime=FMath::Max(V.Recipe.ServerTime,static_cast<float>(Hybrid->GetLocalLogicalSeconds()));
            V.Geometry.SampleTime=V.Recipe.ServerTime; V.Geometry.EndTime=V.Recipe.ServerTime+FMath::Min(V.Recipe.Lifetime,V.Recipe.MaximumDistance/V.Recipe.Speed);
            Rebuild(V);
        }
        V.bAdmitted=true;
        if (P->Matches(V.Handle)) return;
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
    }
    bool Publish(FRecord& V,bool Bootstrap)
    {
        // A numeric readback can activate a flight at the final tick of a
        // catch-up batch before its earlier shared steps reach Service().
        // Keep the publication pending until that actual sample is current;
        // do not replay local effects or attempt an invalid private encoding.
        const auto& Geometry=Pool->Matches(V.Handle)?Pool->Slots[V.Handle.Slot].State:V.Geometry;
        if (!Bootstrap && Geometry.Phase!=EGuLiCombatEffectPhase::Waiting
            && Geometry.SampleTime>Hybrid->GetLocalLogicalSeconds()+.001)
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(GuLiLinearFlight_WaitForPublicationStep);
            return false;
        }
        FGuLiFlightEvent E; E.State=V.Geometry; E.bBootstrap=Bootstrap;
        if (!UGuLiCombatEffectReplicationComponent::ApplyHybridFlight(Pool->GetWorld(),E)) return false;
        if (Bootstrap) return true;
        const float Sample=E.State.SampleTime;
        UGuLiCombatEffectReplicationComponent::RebasePublishedState(Pool->GetWorld(),Hybrid->GetObjectLogicalTick(V.Key),E.State);
        if (E.bHasMuzzle) E.Muzzle.ServerTime+=E.State.SampleTime-Sample;
        TArray<uint8> Bytes; if (GuLiFlightWire::EncodeBatch(MakeArrayView(&E,1),Bytes)!=1) return false;
        TArray<uint8> Private;
        if (!EncodeRecord(V,Private)) return false;
        GuLiNet::Writer Wire;Wire.U32(GuLiLinearState::EventTag);Wire.U32(GuLiLinearState::Version);
        Wire.Blob(std::span<const uint8>(Bytes.GetData(),Bytes.Num()));Wire.Blob(std::span<const uint8>(Private.GetData(),Private.Num()));
        if (!Wire.Good()) return false;const auto Body=Wire.Take();Bytes.Reset();Bytes.Append(Body.data(),int32(Body.size()));
        return Hybrid->PublishEffect(V.Key,FGuid::NewGuid(),Bytes);
    }
    bool EncodeRecord(const FRecord& V,TArray<uint8>& Out) const
    {
        GuLiLinearState::FPiece Piece;Piece.Epoch=CurrentEpoch();Piece.At=Hybrid->GetLocalLogicalSeconds();
        auto Copy=V;if (Pool->Matches(V.Handle)) Copy.Geometry=Pool->Slots[V.Handle.Slot].State;
        Copy.bLaunchPublished=Copy.Geometry.Phase!=EGuLiCombatEffectPhase::Waiting;Copy.bTerminalPublished=Copy.bFinished;
        Piece.Records.Add(Copy);FGuLiHybridStateArchive A(65536);GuLiLinearState::Piece(A,Piece);return A.Finish(Out);
    }
    bool CacheRecord(const FGuLiHybridObjectState& O,TConstArrayView<uint8> Bytes,bool Birth)
    {
        GuLiLinearState::FPiece Data;
        if (!GuLiLinearState::Decode(Bytes,Data) || Data.Epoch!=CurrentEpoch() || Data.Records.Num()!=1 || !Data.Histories.IsEmpty()) return false;
        auto V=Data.Records[0];
        if (V.Key!=O.Key || V.Source!=O.Parent || V.Admission!=O.SpawnEvent) return false;
        if (O.LifeState==GuLiLinearState::Destroyed) return true;
        if (auto* Existing=Records.Find(O.Key))
        {
            if (Existing->Source!=V.Source || Existing->Admission!=V.Admission) return false;
            if (Birth || O.SimulationOwner==LocalPlayer() || Existing->Geometry.Sequence>V.Geometry.Sequence) return true;
        }
        else if (Records.Num()>=GuLiLinearState::MaximumRecords) return false;
        const float Offset=float(Hybrid->GetLocalLogicalSeconds()-Data.At);
        V.Recipe.ServerTime+=Offset;if (V.Recipe.ScheduledActivationSeconds>0) V.Recipe.ScheduledActivationSeconds+=Offset;
        V.Geometry.StartTime+=Offset;V.Geometry.SampleTime+=Offset;V.Geometry.ActivationTime+=Offset;V.Geometry.EndTime+=Offset;
        V.bAdmitted=true;V.Handle={};Rebuild(V);Records.Add(V.Key,V);return true;
    }
    bool CacheBirth(const FGuLiHybridObjectState& O)
    {
        const auto* Cap=O.Capabilities.Find(FName(UTF8_TO_TCHAR(GuLiNet::LinearFlightRecipe::Name)));
        if (!Cap || Cap->Version!=GuLiNet::LinearFlightRecipe::Version) return false;
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
        TArray<FGuLiHybridObjectKey> Keys;
        if (Changed) Keys.Add(*Changed);else Records.GetKeys(Keys);
        for (const auto& Key:Keys)
        {
            auto* V=Records.Find(Key); if (!V || !V->bRecipe) continue;
            if (V->bAdmitted && !V->bFinished && Pool->Matches(V->Handle))
            { if (!V->bLaunchPublished && !Hybrid->IsBattlePaused()) V->bLaunchPublished=Publish(*V,false); continue; }
            FGuLiHybridObjectState O;
            const bool Registered=Hybrid->GetObjectView(Key,O);
            if (Registered && O.LifeState==GuLiLinearState::Destroyed) {Discard(*V,TEXT("discard-numeric-destroyed"));continue;}
            if (!V->bAdmitted && !Registered)
            {
                FGuLiHybridTransactionResult R;
                if (Hybrid->FindTransactionResult(V->Admission,R) && !R.IsCommitted()
                    && R.Status!=EGuLiHybridTransactionStatus::Deferred && R.Status!=EGuLiHybridTransactionStatus::Paused)
                { Discard(*V,TEXT("discard-admission-rejected")); continue; }
            }
            if (!Registered || O.SimulationOwner!=LocalPlayer()) continue;
            if (!V->bFinished) { Activate(*V,O); continue; }
            if (V->Recipe.Context.Source.Kind==EGuLiTargetKind::None && !Rebind(*V)) continue;
            if (!V->bTerminalPublished && !Hybrid->IsBattlePaused()) V->bTerminalPublished=Publish(*V,false);
            if (V->bHit && !V->bDamageResolved)
            {
                FGuLiDamageCommitResult Result;
                if (Hybrid->FindDamageResult(V->Damage,Result))
                { V->bDamageResolved=true; if (Result.Status==EGuLiDamageCommitStatus::Committed) ++Pool->Stats.Hits; }
                else if (!V->bDamageSubmitted && !Hybrid->IsBattlePaused())
                {
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
            if (!V->bRetirementSubmitted && V->bTerminalPublished && !Hybrid->IsBattlePaused())
            {
                if (!V->bRetirementPrepared)
                {
                    V->RetirementAt=Hybrid->GetObjectLogicalTick(Key);V->bRetirementPrepared=true;
                    if (!Publish(*V,false)) {V->bRetirementPrepared=false;continue;}
                }
                FGuLiHybridObjectRequest R; R.Source=R.Target=Key; R.Event=V->Retirement;
                R.Rule=FName(*(TEXT("retire/")+O.Definition.ToString())); R.Operation=EGuLiHybridObjectOperation::Destroy;
                R.ExpectedRelationRevision=O.RelationRevision;R.OriginalAt=V->RetirementAt;
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
            { FGuLiDamageCommitResult Damage; if (Hybrid->FindDamageResult(V.Damage,Damage)) Copy.bDamageResolved=true; }
            if (Pool->Matches(V.Handle)) { Copy.Geometry=Pool->Slots[V.Handle.Slot].State; Groups[Copy.SourceKind==EGuLiTargetKind::CommanderSoldier?1:0].Add(Copy.HistoryGroup); }
            FGuLiHybridStateArchive Check(65536); GuLiLinearState::Record(Check,Copy);
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
                const int32 Slot=V.Handle.Slot;
                if (Pool->Slots.IsValidIndex(Slot) && Pool->Slots[Slot].Generation==V.Handle.Generation)
                {
                    const int32 Active=Pool->Slots[Slot].ActiveIndex;
                    Pool->ById.Remove(V.Key.StableId);
                    for (auto& Domain:Pool->DomainSlots) Domain.RemoveSingle(Slot);
                    if (Pool->ActiveSlots.IsValidIndex(Active)) Pool->ActiveSlots.RemoveAt(Active,1,EAllowShrinking::No);
                    for (int32 I=0;I<Pool->ActiveSlots.Num();++I) Pool->Slots[Pool->ActiveSlots[I]].ActiveIndex=I;
                    Pool->Slots[Slot]={};Pool->FreeSlots.AddUnique(Slot);
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
                    V.Handle={}; Records.Add(V.Key,V);
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
            }
        }
        bRestoring=false; return true;
    }
};

class FGuLiLinearFlightAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakPtr<FGuLiHybridProjectileState> State;
    FGuLiHybridObjectState Object;
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
    void Advance(uint64,double) override {}
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override { return false; }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 At) override
    {
        const auto S=State.Pin(); if (!S || Object.SimulationOwner==S->LocalPlayer()) return false;
        GuLiNet::Reader Prefix(std::span<const uint8>(Bytes.GetData(),Bytes.Num()));uint32 Tag;
        if (Prefix.U32(Tag) && Tag==GuLiLinearState::EventTag) return ApplyPrivateEvent(Bytes,At);
        TArray<uint8> Body; Body.Append(Bytes.GetData(),Bytes.Num()); TArray<FGuLiFlightEvent> Events;
        if (!GuLiFlightWire::DecodeBatch(Body,Events) || Events.Num()!=1) return false;
        auto E=Events[0];
        if (E.State.EffectId!=Object.Key.StableId || E.State.MatchEpoch!=S->CurrentEpoch() || E.State.Kind!=EGuLiCombatEffectKind::LinearProjectile) return false;
        const float Before=E.State.SampleTime;
        UGuLiCombatEffectReplicationComponent::RebaseReceivedState(S->Pool->GetWorld(),At,E.State);
        if (E.bHasMuzzle) E.Muzzle.ServerTime+=E.State.SampleTime-Before;
        if (auto* V=S->Records.Find(Object.Key))
        {
            V->Recipe.Context.Source=E.State.Source;V->Recipe.Context.Target=E.State.Target;
            if (V->Geometry.Sequence<=E.State.Sequence && !V->bFinished)
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
            || !Wire.Blob(Visual) || !Wire.Blob(Private,65536) || !Wire.Done()
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
    {if (const auto S=State.Pin()) for (const auto& P:S->Records) if (S->Includes({K},P.Value)) return true;return false;}
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
}
bool UGuLiProjectilePoolSubsystem::IsTickable() const { return !IsTemplate() && (HybridState.IsValid() || !ScheduledLaunches.IsEmpty()); }
TStatId UGuLiProjectilePoolSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(GuLiProjectileTransactions,STATGROUP_Tickables); }
void UGuLiProjectilePoolSubsystem::Tick(float)
{
    if (HybridState) { HybridState->Service(); return; }
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
    auto& V=S->Records.Add(Key); V.Key=Key; V.Source=Source; V.Target=Target; V.Team=Team;
    V.SourceKind=R.Context.Emitter.IsValid()?EGuLiTargetKind::Wingman:R.Context.Source.Kind;
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
    S->Trace(TEXT("recipe-ready"),V);
    if (!H->SubmitObjectOperationNative(Request,{}).IsValid()) { S->Records.Remove(Key); return {}; }
    // Submit can enter adapter callbacks. Re-find solely for diagnostics rather
    // than dereferencing a possibly relocated TMap value in the trace itself.
    if (const auto* Current=S->Records.Find(Key)) S->Trace(TEXT("submit-return"),*Current);
    if (auto* Current=S->Records.Find(Key)) return S->Reserve(*Current);
    return {};
}
void UGuLiProjectilePoolSubsystem::RetireHybrid(const FGuLiCombatEffectState& State,const FGuLiCombatEffectContext&,
    EGuLiCombatEffectEndReason Reason,const FVector& Position,float Time,const FGuLiTargetHandle* Target)
{
    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=State.EffectId; Key.LifeGeneration=1;
    auto S=HybridState; auto* V=S?S->Records.Find(Key):nullptr; if (!V || !S->Hybrid.IsValid()) return;
    V->Geometry=State; V->Geometry.Phase=EGuLiCombatEffectPhase::Finished; V->Geometry.EndReason=Reason;
    V->Geometry.Location=Position; V->Geometry.SampleTime=FMath::Max(Time,State.StartTime); ++V->Geometry.Sequence;
    V->bFinished=true; V->Handle={};
    if (Target)
    { if (!S->Hybrid->ResolveTarget(*Target,V->HitTarget)) { S->Hybrid->Suspend(TEXT("Flight hit lacks a stable target")); return; } V->bHit=true;V->DamageAt=S->Hybrid->GetObjectLogicalTick(Key); }
    if (Reason==EGuLiCombatEffectEndReason::Blocked) ++Stats.Blocked;
    V->bTerminalPublished=S->Publish(*V,false); OnState.Broadcast(V->Geometry,true);
    // The shared logical step services every record before stepping the pool.
    // Retiring one slot changes only this record; transaction results are applied
    // by the ordered main-thread pump, never in this synchronous pool traversal.
    // Preserve immediate hit/retirement submission without rescanning every other
    // admitted or waiting flight for each projectile that finishes in the step.
    S->Service(bStepping?&Key:nullptr);
}
