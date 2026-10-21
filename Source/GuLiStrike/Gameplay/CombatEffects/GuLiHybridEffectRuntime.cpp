#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiEffectRuntimeContract.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/Private/GuLiHybridReflectedState.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "GuLiNetRuntimeEffectRecipe.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/GCObject.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace GuLiEffectState
{
using namespace GuLiEffectRuntimeContract;
const FName System(TEXT("CombatEffects"));
struct FHit
{
    FGuLiHybridObjectKey Target;FGuid Event,Root;FVector Location=FVector::ZeroVector;
    uint64 StartedAt=0;bool Submitted=false,Resolved=false;
};
struct FRecord
{
    FGuLiHybridObjectKey Key,Source,Target;
    FGuid Admission,Retirement;
    FGuLiRuntimeCombatEffect Instance;
    TArray<FHit> Hits;
    TArray<FGuLiCombatEffectState> PendingPublic;
    bool Admitted=false,Finished=false,Published=false,TerminalPublished=false,RetirementSubmitted=false;
    uint64 RetirementAt=0;bool RetirementPrepared=false;
};
struct FCooldown { FGuLiHybridObjectKey Member,Carrier;FName Slot;FGuLiWingmanBurstCooldown Value; };
struct FPiece { uint32 Epoch=0;double At=0;TArray<FRecord> Records;TArray<FCooldown> Cooldowns; };
FGuid Event(const FGuLiHybridObjectKey& Key,const FString& Suffix)
{return FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/EffectEvents"),EGuLiHybridObjectKind::Field,
    Key.StableId.ToString(EGuidFormats::Digits)+TEXT("/")+LexToString(Key.LifeGeneration)+Suffix).StableId;}
template<class T> void Asset(FGuLiHybridStateArchive& A,TSoftObjectPtr<T>& Value)
{
    FString Path=Value.ToSoftObjectPath().ToString();A.String(Path,1024);
    if (!Path.IsEmpty() && !Path.StartsWith(TEXT("/Game/"))) A.Reject();
    if (A.IsReading() && A.Good()) Value=TSoftObjectPtr<T>(FSoftObjectPath(Path));
}
void State(FGuLiHybridStateArchive& A,FGuLiCombatEffectState& S)
{
    A.Value(S.MatchEpoch);A.Value(S.EffectId);A.Enum(S.Kind,2);A.Enum(S.Phase,3);A.Enum(S.EndReason,6);
    FGuLiHybridReflectedState::Value(A,S.Source);FGuLiHybridReflectedState::Value(A,S.Target);A.Enum(S.SourceTeam,2);
    Asset(A,S.ProjectileDefinition);Asset(A,S.GroundWarningStyle);Asset(A,S.FieldDefinition);
    A.Value(S.GuidanceBatchId);FVector Center=S.GuidanceCenter;A.Value(Center);if (A.IsReading()) S.GuidanceCenter=Center;
    A.Value(S.GuidanceRadius);
    FVector Position=S.Location,Velocity=S.Velocity,Launch=S.LaunchLocation,Target=S.LastTargetLocation,Direction=S.LaunchDirection;
    A.Value(Position);A.Value(Velocity);A.Value(Launch);A.Value(Target);A.Value(Direction);
    if (A.IsReading()) {S.Location=Position;S.Velocity=Velocity;S.LaunchLocation=Launch;S.LastTargetLocation=Target;S.LaunchDirection=Direction;}
    FGuLiHybridReflectedState::Value(A,S.Motion);A.Value(S.SlotId);A.Value(S.MuzzleOffset);A.Value(S.FireRateHz);
    A.Value(S.bFixedPoint);A.Value(S.RandomSeed);A.Value(S.VariantIndex);
    A.Value(S.StartTime);A.Value(S.SampleTime);A.Value(S.ActivationTime);A.Value(S.EndTime);A.Value(S.Radius);A.Value(S.Sequence);
    if (!S.IsWellFormed()) A.Reject();
}
void Record(FGuLiHybridStateArchive& A,FRecord& R)
{
    A.Value(R.Key);A.Value(R.Source);A.Value(R.Target);A.Value(R.Admission);A.Value(R.Retirement);
    auto& I=R.Instance;State(A,I.State);FGuLiHybridReflectedState::Value(A,I.Context);
    A.Enum(I.Timing,2);A.Value(I.Interval);A.Value(I.Duration);A.Value(I.FadeSeconds);A.Value(I.FrozenRadius);A.Value(I.FrozenDelay);
    A.Value(I.StopDistance);A.Value(I.OrbitRadius);A.Value(I.CooldownSeconds);A.Value(I.GunSpeed);A.Value(I.GunLifetime);
    A.Value(I.GunSweepRadius);A.Value(I.GunRange);A.Value(I.VariantCount);A.Value(I.DeliveredPulses);A.Value(I.LastCorrection);
    A.Value(I.bHoming);A.Value(I.HomingTurnRate);
    A.Array(R.PendingPublic,4,[](auto& W,auto& S){State(W,S);});
    A.Value(R.Admitted);A.Value(R.Finished);A.Value(R.Published);A.Value(R.TerminalPublished);A.Value(R.RetirementSubmitted);
    A.Value(R.RetirementAt);A.Value(R.RetirementPrepared);
    A.Array(R.Hits,MaximumPendingHits,[](auto& W,auto& H)
    {W.Value(H.Target);W.Value(H.Event);W.Value(H.Root);W.Value(H.Location);W.Value(H.StartedAt);W.Value(H.Submitted);W.Value(H.Resolved);
     if (!H.Target.IsValid() || !H.Event.IsValid() || !H.Root.IsValid() || (H.Resolved && !H.Submitted)) W.Reject();});
    TSet<FGuid> Events;for (const auto& H:R.Hits) {if (Events.Contains(H.Event)) A.Reject();Events.Add(H.Event);}
    if (!R.Key.IsValid() || R.Key.Kind!=EGuLiHybridObjectKind::Field || !R.Source.IsValid()
        || R.Admission!=Event(R.Key,TEXT("/admission")) || R.Retirement!=Event(R.Key,TEXT("/retirement"))
        || (I.State.EffectId!=R.Key.StableId && I.State.EffectId!=Event(R.Key,TEXT("/field")))
        || I.Context.SkillId.IsNone() || I.Context.Damage<=0 || I.Context.MatchEpoch!=I.State.MatchEpoch
        || I.Interval<=0 || I.Duration<0 || I.FrozenRadius<0 || I.DeliveredPulses<0 || I.HomingTurnRate<0
        || (R.TerminalPublished && !R.Finished) || (R.RetirementPrepared && !R.Finished)
        || (R.RetirementSubmitted && !R.RetirementPrepared)) A.Reject();
}
void Piece(FGuLiHybridStateArchive& A,FPiece& P)
{
    uint32 Schema=Version;A.Value(Schema);A.Value(P.Epoch);A.Value(P.At);
    if (Schema!=Version || !P.Epoch || P.At<0) A.Reject();
    A.Array(P.Records,MaximumRecords,[](auto& W,auto& R){Record(W,R);});
    A.Array(P.Cooldowns,1024,[](auto& W,auto& C)
    {W.Value(C.Member);W.Value(C.Carrier);W.Value(C.Slot);W.Value(C.Value.OrbitRadius);W.Value(C.Value.DurationSeconds);W.Value(C.Value.OrbitEntryTime);
     if (!C.Member.IsValid() || !C.Carrier.IsValid() || C.Slot.IsNone() || C.Value.OrbitRadius<=0 || C.Value.DurationSeconds<=0) W.Reject();});
    TSet<FGuLiHybridObjectKey> Keys;uint32 Hits=0;
    for (const auto& R:P.Records)
    {
        Hits+=R.Hits.Num();
        if (Keys.Contains(R.Key) || Hits>MaximumPendingHits || R.Instance.State.MatchEpoch!=P.Epoch
            || R.Instance.State.SampleTime>P.At+.001) A.Reject();Keys.Add(R.Key);
    }
}
bool Decode(TConstArrayView<uint8> Bytes,FPiece& P)
{FGuLiHybridStateArchive A(Bytes,MaximumBytes);Piece(A,P);return A.Done();}
}

/** All effect mechanisms share the same independent authority and final-result lifetime. */
struct FGuLiHybridEffectRuntime final:FGCObject
{
    using FRecord=GuLiEffectState::FRecord;
    TWeakObjectPtr<UGuLiCombatEffectRuntimeSubsystem> Runtime;
    TWeakObjectPtr<UGuLiHybridWorldSubsystem> Hybrid;
    FGuLiHybridManifestData Manifest;
    TMap<FGuid,FRecord> Records;
    TMap<FGuid,FGuid> ByPresentation;
    bool Restoring=false;
    bool Includes(const TArray<FGuLiHybridObjectKey>& Objects,const FRecord& R) const
    {
        if (Objects.Contains(R.Key)) return true;
        if (R.Admitted || !Objects.Contains(R.Source)) return false;
        FGuLiHybridObjectState Existing;return !Hybrid->GetObject(R.Key,Existing);
    }
    bool UsesObject(const FGuLiHybridObjectKey& K) const
    {
        const auto Comparison=[&]
        {for (const auto& P:Records) if (Includes({K},P.Value)) return true;return false;};
        static const auto* Option=IConsoleManager::Get().FindConsoleVariable(TEXT("gs.Hybrid.DirectCaptureMembership"));
        const int32 Mode=Option?Option->GetInt():0;
        bool HasRecord=false;
        if (!Mode) HasRecord=Comparison();
        else
        {
            HasRecord=[&]
            {
                // This map is indexed by StableId; always check the entire key
                // so a different kind or life cannot inherit the captured state.
                if (const auto* R=Records.Find(K.StableId);R && R->Key==K) return true;
                for (const auto& P:Records)
                {
                    const auto& R=P.Value;if (R.Admitted || R.Source!=K) continue;
                    FGuLiHybridObjectState Effect;if (!Hybrid->GetObject(R.Key,Effect)) return true;
                }
                return false;
            }();
            if (Mode==2)
            {
                const bool Original=Comparison();
                if (HasRecord==Original) {TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCapture_EffectMembershipEqual);}
                else {TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCapture_EffectMembershipMismatch);ensureAlwaysMsgf(false,TEXT("Combat effect capture membership differs"));}
                HasRecord=Original;
            }
        }
        if (HasRecord) return true;
        if (!Runtime.IsValid() || !Hybrid.IsValid()) return false;
        for (const auto& P:Runtime->WingmanBurstCooldowns)
        {FGuLiHybridObjectKey Member;if (Hybrid->ResolveTarget(GuLiCombatTargets::MakeWingmanTargetHandle(P.Key),Member) && Member==K) return true;}
        return false;
    }
    FGuLiHybridEffectRuntime(UGuLiCombatEffectRuntimeSubsystem& R,UGuLiHybridWorldSubsystem& H):Runtime(&R),Hybrid(&H) {}
    FString GetReferencerName() const override {return TEXT("GuLiHybridEffectRuntime");}
    void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        for (auto& P:Records)
        {Collector.AddReferencedObject(P.Value.Instance.Projectile);Collector.AddReferencedObject(P.Value.Instance.Field);
         Collector.AddReferencedObject(P.Value.Instance.GroundWarningStyle);}
    }
    uint32 Epoch() const
    {const auto* B=Runtime.IsValid()?Runtime->GetWorld()->GetGameState<AGuLiBattleGameState>():nullptr;return B?B->GetMatchEpoch():0;}
    FGuid Player() const
    {const auto* S=Runtime.IsValid()?Runtime->GetWorld()->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;return S?S->GetLocalPlayerId():FGuid();}
    bool Supported(const FGuLiHybridObjectState& O) const
    {return O.Key.Kind==EGuLiHybridObjectKind::Field && Manifest.Strings.FindRef(TEXT("adapter/definition/")+O.Definition.ToString())==TEXT("CombatEffect");}
    FRecord* Find(FGuid Id) {const auto* Key=ByPresentation.Find(Id);return Key?Records.Find(*Key):nullptr;}
    bool Resolve(const FGuLiHybridObjectKey& Key,FGuLiTargetHandle& Target) const
    {
        if (Hybrid->ResolveObjectTarget(Key,Target)) return true;
        FGuLiHybridObjectState Object;FGuLiWingmanHandle Member;
        if (!Hybrid->GetObject(Key,Object) || !GuLiHybridWingman::Resolve(Manifest,Object,Member)) return false;
        Target=GuLiCombatTargets::MakeWingmanTargetHandle(Member);return Target.IsValid();
    }
    bool Rebind(FRecord& R)
    {
        auto& I=R.Instance;auto& S=I.State;
        if (!ResolveStored(R.Source,I.Context.Source)
            || (R.Target.IsValid() && !ResolveStored(R.Target,I.Context.Target))) return false;
        S.Source=I.Context.Source;S.Target=I.Context.Target;
        S.MatchEpoch=I.Context.MatchEpoch=I.Context.WeaponBinding.MatchEpoch=Epoch();I.SourceLease={};
        I.Projectile=S.ProjectileDefinition.LoadSynchronous();I.Field=S.FieldDefinition.LoadSynchronous();
        I.GroundWarningStyle=S.GroundWarningStyle.LoadSynchronous();
        if ((!S.ProjectileDefinition.IsNull() && !I.Projectile) || (!S.FieldDefinition.IsNull() && !I.Field)
            || (!S.GroundWarningStyle.IsNull() && !I.GroundWarningStyle)) return false;
        I.CurveCoefficients.Initialize(S);return true;
    }
    bool ResolveStored(const FGuLiHybridObjectKey& Key,FGuLiTargetHandle& Target) const
    {
        FGuLiTargetHandle Current;
        if (Resolve(Key,Current)) {Target=Current;return true;}
        return Target.IsValid();
    }
    bool ApplyPublic(const FGuLiCombatEffectState& S,bool Bootstrap)
    {
        auto* W=Runtime->GetWorld();
        if (GuLiFlightWire::IsFlight(S.Kind))
        {FGuLiFlightEvent E;E.State=S;E.bBootstrap=Bootstrap;return UGuLiCombatEffectReplicationComponent::ApplyHybridFlight(W,E);}
        if (auto* Visuals=W->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visuals->ApplyState(S);
        return true;
    }
    bool PublishState(FRecord& R,FGuLiCombatEffectState State,bool Reliable,bool Bootstrap=false)
    {
        State.MatchEpoch=Epoch();
        if (!ResolveStored(R.Source,State.Source) || (R.Target.IsValid() && !ResolveStored(R.Target,State.Target))) return false;
        if (!R.Admitted || !ApplyPublic(State,Bootstrap)) return false;
        if (Bootstrap) return true;
        UGuLiCombatEffectReplicationComponent::RebasePublishedState(Runtime->GetWorld(),Hybrid->GetObjectLogicalTick(R.Key),State);
        FGuLiHybridStateArchive A(GuLiNet::MaxMessageBytes-256);uint32 Version=GuLiEffectRuntimeContract::Version;A.Value(Version);
        auto Source=R.Source,Target=R.Target;A.Value(Source);A.Value(Target);GuLiEffectState::State(A,State);
        TArray<uint8> Private;if (!EncodeRecord(R,Private)) return false;A.Bytes(Private,GuLiNet::MaxMessageBytes-512);
        TArray<uint8> Bytes;if (!A.Finish(Bytes)) return false;
        const auto Event=GuLiEffectState::Event(R.Key,TEXT("/public/")+State.EffectId.ToString(EGuidFormats::Digits)
            +TEXT("/")+LexToString(State.Sequence)+TEXT("/")+LexToString(uint8(State.Phase)));
        return Reliable?Hybrid->PublishEffect(R.Key,Event,Bytes):Hybrid->PublishMotion(R.Key,Bytes);
    }
    bool Publish(FRecord& R,bool Reliable,bool Bootstrap=false)
    {
        if (!Bootstrap) while (!R.PendingPublic.IsEmpty())
        {
            if (!PublishState(R,R.PendingPublic[0],true)) return false;
            R.PendingPublic.RemoveAt(0);
        }
        return PublishState(R,R.Instance.State,Reliable,Bootstrap);
    }
    bool Receive(const FGuLiHybridObjectState& O,TConstArrayView<uint8> Bytes,uint64 CapturedEpoch=0,TOptional<uint64> OwnerTick={})
    {
        FGuLiHybridStateArchive A(Bytes,GuLiNet::MaxMessageBytes-256);uint32 Version=0;FGuLiHybridObjectKey Source,Target;FGuLiCombatEffectState S;
        A.Value(Version);A.Value(Source);A.Value(Target);GuLiEffectState::State(A,S);
        TArray<uint8> Private;A.Bytes(Private,GuLiNet::MaxMessageBytes-512);
        if (!A.Done() || Version!=GuLiEffectRuntimeContract::Version || Source!=O.Parent
            || S.MatchEpoch!=(CapturedEpoch?CapturedEpoch:Epoch())
            || (S.EffectId!=O.Key.StableId && S.EffectId!=GuLiEffectState::Event(O.Key,TEXT("/field")))
            || !CacheRecord(O,Private,false)) return false;
        if (O.LifeState>=2 && S.Phase!=EGuLiCombatEffectPhase::Finished) return true;
        if (!ResolveStored(Source,S.Source) || (Target.IsValid() && !ResolveStored(Target,S.Target))) return false;
        if (OwnerTick.IsSet()) UGuLiCombatEffectReplicationComponent::RebaseReceivedState(Runtime->GetWorld(),OwnerTick.GetValue(),S);
        S.MatchEpoch=Epoch();return ApplyPublic(S,CapturedEpoch!=0);
    }
    bool EncodeRecord(const FRecord& R,TArray<uint8>& Bytes) const
    {
        GuLiEffectState::FPiece P;P.Epoch=Epoch();P.At=Hybrid->GetLocalLogicalSeconds();auto Copy=R;
        if (const auto* Live=Runtime->Effects.Find(R.Instance.State.EffectId)) Copy.Instance=*Live;
        P.Records.Add(Copy);FGuLiHybridStateArchive A(GuLiNet::MaxMessageBytes-1024);GuLiEffectState::Piece(A,P);return A.Finish(Bytes);
    }
    bool CacheRecord(const FGuLiHybridObjectState& O,TConstArrayView<uint8> Bytes,bool Birth)
    {
        GuLiEffectState::FPiece P;
        if (!GuLiEffectState::Decode(Bytes,P) || P.Epoch!=Epoch() || P.Records.Num()!=1 || !P.Cooldowns.IsEmpty()) return false;
        auto R=P.Records[0];if (R.Key!=O.Key || R.Source!=O.Parent || R.Admission!=O.SpawnEvent) return false;
        if (O.LifeState==3) return true;
        if (auto* Old=Records.Find(O.Key.StableId))
        {
            if (Old->Key!=R.Key || Old->Source!=R.Source || Old->Admission!=R.Admission) return false;
            if (Birth || O.SimulationOwner==Player() || (Old->Instance.State.EffectId==R.Instance.State.EffectId && Old->Instance.State.Sequence>R.Instance.State.Sequence)) return true;
            ByPresentation.Remove(Old->Instance.State.EffectId);
        }
        else if (Records.Num()>=int32(GuLiEffectRuntimeContract::MaximumRecords)) return false;
        const float Offset=float(Hybrid->GetLocalLogicalSeconds()-P.At);auto Rebase=[&](FGuLiCombatEffectState& S)
        {S.StartTime+=Offset;S.SampleTime+=Offset;S.ActivationTime+=Offset;S.EndTime+=Offset;};
        Rebase(R.Instance.State);R.Instance.LastCorrection+=Offset;for (auto& S:R.PendingPublic) Rebase(S);
        // Observers do not inherit the former owner's pending send queue.
        // Same-event work keeps its original pulse age when it is resubmitted.
        for (auto& H:R.Hits) if (!H.Resolved) H.Submitted=false;
        R.Admitted=true;R.RetirementSubmitted=false;R.TerminalPublished=false;
        ByPresentation.Add(R.Instance.State.EffectId,R.Key.StableId);Records.Add(R.Key.StableId,MoveTemp(R));return true;
    }
    bool CacheBirth(const FGuLiHybridObjectState& O)
    {
        const auto* Cap=O.Capabilities.Find(FName(UTF8_TO_TCHAR(GuLiNet::RuntimeEffectRecipe::Name)));
        GuLiNet::RuntimeEffectRecipe::Recipe Recipe;
        if (!Cap || Cap->Version!=GuLiNet::RuntimeEffectRecipe::Version
            || !GuLiNet::RuntimeEffectRecipe::Decode(std::span<const uint8>(Cap->Data.GetData(),Cap->Data.Num()),Recipe)
            || Recipe.Effect!=GuLiNetBridge::ToCore(O.Key) || Recipe.Source!=GuLiNetBridge::ToCore(O.Parent)
            || Recipe.Admission!=GuLiNetBridge::ToCore(O.SpawnEvent)) return false;
        return CacheRecord(O,MakeArrayView(Recipe.State.data(),int32(Recipe.State.size())),true);
    }
    void Activate(FRecord& R,const FGuLiHybridObjectState& O)
    {
        if (R.Finished || O.SimulationOwner!=Player() || O.LifeState!=1) return;
        if (R.Admitted && Runtime->Effects.Contains(R.Instance.State.EffectId)) return;
        if (!R.Admitted)
        {
            const float Shift=FMath::Max(0.f,float(Hybrid->GetLocalLogicalSeconds())-R.Instance.State.StartTime);
            auto& S=R.Instance.State;S.StartTime+=Shift;S.SampleTime+=Shift;S.ActivationTime+=Shift;S.EndTime+=Shift;
            R.Instance.LastCorrection+=Shift;R.Admitted=true;
        }
        if (!Rebind(R)) {Hybrid->Suspend(TEXT("Admitted combat effect cannot rebuild its source or assets"));return;}
        Runtime->Effects.FindOrAdd(R.Instance.State.EffectId)=R.Instance;
        ByPresentation.Add(R.Instance.State.EffectId,R.Key.StableId);
        if (!Restoring && !R.Published && !Hybrid->IsBattlePaused()) R.Published=Publish(R,true);
    }
    void Service()
    {
        if (Restoring || !Runtime.IsValid() || !Hybrid.IsValid() || !Hybrid->GetFailure().IsEmpty()) return;
        TArray<FGuid> Keys;Records.GetKeys(Keys);
        for (const auto Id:Keys)
        {
            auto* R=Records.Find(Id);if (!R) continue;
            FGuLiHybridObjectState O;
            if (!Hybrid->GetObject(R->Key,O))
            {
                FGuLiHybridTransactionResult Result;
                if (Hybrid->FindTransactionResult(R->Admission,Result) && !Result.IsCommitted())
                {ByPresentation.Remove(R->Instance.State.EffectId);Runtime->Effects.Remove(R->Instance.State.EffectId);Records.Remove(Id);}
                continue;
            }
            if (O.LifeState==3)
            {ByPresentation.Remove(R->Instance.State.EffectId);Runtime->Effects.Remove(R->Instance.State.EffectId);Records.Remove(Id);continue;}
            if (O.SimulationOwner!=Player()) continue;
            if (!R->Admitted || (!R->Finished && !Runtime->Effects.Contains(R->Instance.State.EffectId))) Activate(*R,O);
            if (!R->Finished && (!R->Published || !R->PendingPublic.IsEmpty()) && !Hybrid->IsBattlePaused())
            {
                if (const auto* Live=Runtime->Effects.Find(R->Instance.State.EffectId)) R->Instance=*Live;
                R->Published=Publish(*R,true);
            }
            bool Settled=true;
            for (auto& Hit:R->Hits)
            {
                if (Hit.Resolved) continue;FGuLiDamageCommitResult Result;
                if (Hybrid->FindDamageResult(Hit.Event,Result))
                {Hit.Resolved=true;if (Result.Status==EGuLiDamageCommitStatus::Committed) ++Runtime->Counters.DamageCommits;continue;}
                Settled=false;if (Hit.Submitted || Hybrid->IsBattlePaused()) continue;
                FGuLiDamageRequest Request;Request.MatchEpoch=Epoch();Request.Source=R->Instance.Context.Source;
                if (!Resolve(Hit.Target,Request.Target)) {Hybrid->Suspend(TEXT("Pending effect hit lost its target identity"));return;}
                Request.DamageEventId=Hit.Event;Request.ShotId=R->Key.StableId;Request.RootEventId=Hit.Root;
                Request.SkillId=R->Instance.Context.SkillId;Request.WeaponBinding=R->Instance.Context.WeaponBinding;
                Request.Damage=R->Instance.Context.Damage;Request.HitLocation=Hit.Location;
                Hit.Submitted=Hybrid->SubmitDamage(Request,Hit.StartedAt,{},&R->Key,Hit.StartedAt).IsValid();
            }
            if (!R->Finished) continue;
            if (!R->TerminalPublished && !Hybrid->IsBattlePaused()) R->TerminalPublished=Publish(*R,true);
            if (Settled && R->TerminalPublished && !R->RetirementSubmitted && !Hybrid->IsBattlePaused())
            {
                if (!R->RetirementPrepared)
                {
                    R->RetirementAt=Hybrid->GetObjectLogicalTick(R->Key);R->RetirementPrepared=true;
                    if (!Publish(*R,true)) {R->RetirementPrepared=false;continue;}
                }
                FGuLiHybridObjectRequest Request;Request.Source=Request.Target=R->Key;Request.Event=R->Retirement;
                Request.Operation=EGuLiHybridObjectOperation::Destroy;Request.Rule=FName(*(TEXT("retire/")+O.Definition.ToString()));
                Request.ExpectedRelationRevision=O.RelationRevision;Request.OriginalAt=R->RetirementAt;
                R->RetirementSubmitted=Hybrid->SubmitObjectOperationNative(Request,{}).IsValid();
            }
        }
    }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const
    {
        GuLiEffectState::FPiece P;P.Epoch=Epoch();P.At=Hybrid->GetLocalLogicalSeconds();
        for (const auto& Entry:Records)
        {
            const auto& R=Entry.Value;
            if (!Includes(Objects,R)) continue;
            FGuLiHybridTransactionResult Admission;
            if (!R.Admitted && Hybrid->FindTransactionResult(R.Admission,Admission) && !Admission.IsCommitted()) continue;
            auto Copy=R;if (const auto* Live=Runtime->Effects.Find(R.Instance.State.EffectId)) Copy.Instance=*Live;
            for (auto& Hit:Copy.Hits) {FGuLiDamageCommitResult Result;if (Hybrid->FindDamageResult(Hit.Event,Result)) Hit.Resolved=true;}
            P.Records.Add(MoveTemp(Copy));
        }
        for (const auto& Key:Objects) if (Key.Kind==EGuLiHybridObjectKind::Field && !P.Records.ContainsByPredicate([&](const auto& R){return R.Key==Key;}))
        {
            static uint32 Reported=0;if (Reported++<64) UE_LOG(LogNet,Error,TEXT("Combat effect private record missing id=%s epoch=%u records=%d owned=%d"),*Key.StableId.ToString(),P.Epoch,Records.Num(),Objects.Num());return false;
        }
        for (const auto& Key:Objects) if (Key.Kind==EGuLiHybridObjectKind::Wingman)
        {
            FGuLiHybridObjectState Object;FGuLiWingmanHandle Emitter;
            if (!Hybrid->GetObject(Key,Object) || !GuLiHybridWingman::Resolve(Manifest,Object,Emitter)) return false;
            const auto* Slots=Runtime->WingmanBurstCooldowns.Find(Emitter);if (!Slots) continue;
            for (const auto& Entry:*Slots)
            {
                GuLiEffectState::FCooldown C;C.Member=Key;C.Slot=Entry.Key;C.Value=Entry.Value;
                if (!Hybrid->ResolveTarget(C.Value.Carrier,C.Carrier)) return false;P.Cooldowns.Add(C);
            }
        }
        P.Records.Sort([](const auto& A,const auto& B){return A.Key.StableId<B.Key.StableId;});
        FGuLiHybridStateArchive A(GuLiEffectRuntimeContract::MaximumBytes);GuLiEffectState::Piece(A,P);
        const bool Valid=A.Finish(Bytes);
        if (!Valid)
        {
            static TSet<FGuLiHybridObjectKey> Reported;
            for (const auto& Original:P.Records)
            {
                if (Reported.Num()>=64 || Reported.Contains(Original.Key)) continue;
                auto Copy=Original;TArray<uint8> One;FGuLiHybridStateArchive Check(GuLiEffectRuntimeContract::MaximumBytes);
                GuLiEffectState::Record(Check,Copy);const bool RecordValid=Check.Finish(One);
                if (RecordValid && Copy.Instance.State.SampleTime<=P.At+.001) continue;
                Reported.Add(Copy.Key);const auto& S=Copy.Instance.State;
                UE_LOG(LogNet,Error,TEXT("Combat effect private capture invalid id=%s record_ok=%d state_ok=%d kind=%d phase=%d start=%.9f sample=%.9f end=%.9f capture=%.9f finished=%d terminal=%d retirement=%d/%d pending_public=%d hits=%d"),
                    *Copy.Key.StableId.ToString(EGuidFormats::Digits),RecordValid,S.IsWellFormed(),int32(S.Kind),int32(S.Phase),
                    double(S.StartTime),double(S.SampleTime),double(S.EndTime),P.At,Copy.Finished,Copy.TerminalPublished,
                    Copy.RetirementPrepared,Copy.RetirementSubmitted,Copy.PendingPublic.Num(),Copy.Hits.Num());
            }
        }
        return Valid;
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects) const
    {
        TSet<FGuLiHybridObjectKey> Found;
        for (const auto& Piece:Pieces)
        {
            GuLiEffectState::FPiece P;if (!GuLiEffectState::Decode(Piece.Bytes,P)) return false;
            for (const auto& R:P.Records)
            {
                FGuLiHybridObjectState Source,O;if (Found.Contains(R.Key) || !Hybrid->GetObject(R.Source,Source)) return false;
                const auto Rule=TEXT("admit/runtime/")+FGuLiHybridManifest::MakeDamageRuleId(Source.Definition.ToString(),R.Instance.Context.SkillId,R.Instance.Context.WeaponBinding.SlotId);
                if (!Manifest.Rules.ContainsByPredicate([&](const auto& V){return V.Id==Rule && V.SpawnKind==EGuLiHybridObjectKind::Field;})
                    || (R.Admitted && !Piece.Objects.Contains(R.Key))) return false;
                if (Hybrid->GetObject(R.Key,O) && (O.SpawnEvent!=R.Admission || O.Parent!=R.Source)) return false;
                Found.Add(R.Key);
            }
        }
        for (const auto& Key:Objects) if (Key.Kind==EGuLiHybridObjectKind::Field && !Found.Contains(Key)) return false;
        return true;
    }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage)
    {
        if (Stage==EGuLiHybridRestoreStage::Detach)
        {
            for (auto It=Records.CreateIterator();It;++It)
            {
                const auto& R=It.Value();if (!Includes(Objects,R)) continue;
                Runtime->Effects.Remove(R.Instance.State.EffectId);ByPresentation.Remove(R.Instance.State.EffectId);It.RemoveCurrent();
            }
            for (auto It=Runtime->WingmanBurstCooldowns.CreateIterator();It;++It)
            {FGuLiHybridObjectKey K;if (Hybrid->ResolveTarget(GuLiCombatTargets::MakeWingmanTargetHandle(It.Key()),K) && Objects.Contains(K)) It.RemoveCurrent();}
            Runtime->bIndexReady=false;return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Create)
        {
            Restoring=true;
            for (const auto& Piece:Pieces)
            {
                GuLiEffectState::FPiece P;if (!GuLiEffectState::Decode(Piece.Bytes,P)) return false;
                for (auto R:P.Records)
                {
                    FGuLiHybridObjectState O;const bool Registered=Hybrid->GetObject(R.Key,O);
                    if (Registered && O.LifeState==3) continue;
                    if (!Includes(Objects,R)) continue;
                    if (Registered && !R.Admitted)
                    {
                        const float Shift=FMath::Max(0.f,float(P.At)-R.Instance.State.StartTime);auto& S=R.Instance.State;
                        S.StartTime+=Shift;S.SampleTime+=Shift;S.ActivationTime+=Shift;S.EndTime+=Shift;R.Admitted=true;
                    }
                    const float Offset=float(Hybrid->GetLocalLogicalSeconds()-P.At);auto Rebase=[&](FGuLiCombatEffectState& S)
                    {S.StartTime+=Offset;S.SampleTime+=Offset;S.ActivationTime+=Offset;S.EndTime+=Offset;};
                    Rebase(R.Instance.State);R.Instance.LastCorrection+=Offset;for (auto& S:R.PendingPublic) Rebase(S);
                    ByPresentation.Add(R.Instance.State.EffectId,R.Key.StableId);Records.Add(R.Key.StableId,MoveTemp(R));
                }
            }
            return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            for (auto& Entry:Records) if (Includes(Objects,Entry.Value) && !Rebind(Entry.Value)) return false;
            for (const auto& Piece:Pieces)
            {
                GuLiEffectState::FPiece P;if (!GuLiEffectState::Decode(Piece.Bytes,P)) return false;
                for (auto C:P.Cooldowns)
                {
                    if (!Objects.Contains(C.Member)) continue;
                    C.Value.OrbitEntryTime+=float(Hybrid->GetLocalLogicalSeconds()-P.At);
                    FGuLiHybridObjectState Member;FGuLiWingmanHandle Emitter;
                    if (!Hybrid->GetObject(C.Member,Member) || !GuLiHybridWingman::Resolve(Manifest,Member,Emitter)
                        || !Hybrid->ResolveObjectTarget(C.Carrier,C.Value.Carrier)) return false;
                    Runtime->WingmanBurstCooldowns.FindOrAdd(Emitter).Add(C.Slot,C.Value);
                }
            }
            return true;
        }
        if (Stage!=EGuLiHybridRestoreStage::Activate) return false;
        for (auto& Entry:Records)
        {
            auto& R=Entry.Value;if (!Includes(Objects,R)) continue;FGuLiHybridObjectState O;
            if (Hybrid->GetObject(R.Key,O))
            {if (Objects.Contains(R.Key) && !R.Finished) Activate(R,O);if (R.Admitted) Publish(R,true,true);}
        }
        Restoring=false;return true;
    }
};

class FGuLiHybridEffectAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakPtr<FGuLiHybridEffectRuntime> State;FGuLiHybridObjectState Object;
    bool Private(TConstArrayView<uint8> Bytes) const
    {FGuLiHybridStateArchive A(Bytes);uint32 V=0;FGuLiHybridObjectKey K;A.Value(V);A.Value(K);return A.Done() && V==Version() && K==Object.Key;}
public:
    explicit FGuLiHybridEffectAdapter(TSharedPtr<FGuLiHybridEffectRuntime> S):State(S) {}
    UObject* LifetimeOwner() const override {const auto S=State.Pin();return S?S->Runtime.Get():nullptr;}
    uint32 Version() const override {return GuLiEffectRuntimeContract::Version;}
    TArray<FName> RequiredSharedStateSystems() const override {return {GuLiEffectState::System};}
    bool Prepare(const FGuLiHybridObjectState& O) override {const auto S=State.Pin();if (!S || !S->Supported(O) || !S->CacheBirth(O)) return false;Object=O;return true;}
    void SetAuthority(const FGuLiHybridObjectState& O,bool Local) override
    {Object=O;const auto S=State.Pin();if (S && Local && !S->Restoring) if (auto* R=S->Records.Find(O.Key.StableId)) S->Activate(*R,O);}
    void Advance(uint64,double) override {}
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override {return false;}
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 OwnerTick) override
    {const auto S=State.Pin();return S && Object.SimulationOwner!=S->Player() && S->Receive(Object,Bytes,0,OwnerTick);}
    void ApplyLedger(const FGuLiHybridObjectState& O,uint64,bool) override {Object=O;}


    bool Capture(TArray<uint8>& Bytes) const override
    {FGuLiHybridStateArchive A;uint32 V=Version();auto K=Object.Key;A.Value(V);A.Value(K);return A.Finish(Bytes);}
    bool Validate(TConstArrayView<uint8> Bytes) const override {return Private(Bytes);}


    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage) override {return Private(Bytes);}
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage) override {return Private(Bytes);}
    void Release() override {}
};
class FGuLiHybridEffectSystem final:public IGuLiHybridSharedStateAdapter
{
    TWeakPtr<FGuLiHybridEffectRuntime> State;
public:
    explicit FGuLiHybridEffectSystem(TSharedPtr<FGuLiHybridEffectRuntime> S):State(S) {}
    UObject* LifetimeOwner() const override {const auto S=State.Pin();return S?S->Runtime.Get():nullptr;}
    uint32 Version() const override {return GuLiEffectRuntimeContract::Version;}
    bool UsesObject(const FGuLiHybridObjectKey& K) const override
    {
        const auto S=State.Pin();return S && S->UsesObject(K);
    }
    void Advance(uint64 At,double) override {if (const auto S=State.Pin()) {S->Service();if (!(At%2)) S->Runtime->AdvanceHybrid(At);}}
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    {const auto S=State.Pin();return S && S->Capture(Objects,Bytes);}
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects) const override
    {const auto S=State.Pin();return S && S->Validate(Pieces,Objects);}
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    {const auto S=State.Pin();return S && S->Restore(Pieces,Objects,Stage);}
    bool RestoreQuery(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    {return Restore(Pieces,Objects,Stage);}
};

void UGuLiCombatEffectRuntimeSubsystem::InitializeHybrid(FSubsystemCollectionBase& Collection)
{
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld())) return;
    Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!H) return;HybridEffects=MakeShared<FGuLiHybridEffectRuntime>(*this,*H);FString Error;
    if (!FGuLiHybridManifest::Read(UGuLiNetworkSettings::Resolve(GetWorld()).ManifestFile,HybridEffects->Manifest,Error)
        || !FGuLiHybridAdapterRegistry::Validate(HybridEffects->Manifest,Error)) {H->Suspend(Error);return;}
    if (!H->RegisterSharedStateSystem(GuLiEffectState::System,MakeShared<FGuLiHybridEffectSystem>(HybridEffects)))
        H->Suspend(TEXT("Combat effect checkpoint registration failed"));
}
TSharedPtr<IGuLiHybridSimulationAdapter> UGuLiCombatEffectRuntimeSubsystem::CreateHybridAdapter(const FGuLiHybridObjectState& Object)
{return HybridEffects && HybridEffects->Supported(Object)?MakeShared<FGuLiHybridEffectAdapter>(HybridEffects):TSharedPtr<IGuLiHybridSimulationAdapter>{};}
namespace
{
const FGuLiHybridAdapterRegistration EffectFamily({TEXT("CombatEffect"),EGuLiHybridObjectKind::Field,GuLiEffectRuntimeContract::Version,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState& Object)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {auto* Runtime=Owner.GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();return Runtime?Runtime->CreateHybridAdapter(Object):nullptr;},nullptr,nullptr,EGuLiHybridOwnershipPolicy::RetainedSource,3,false});
}
void UGuLiCombatEffectRuntimeSubsystem::ServiceHybrid() {if (HybridEffects) HybridEffects->Service();}
void UGuLiCombatEffectRuntimeSubsystem::AdvanceHybrid(uint64 At)
{
    if (!HybridEffects || HybridEffects->Restoring || At==LastHybridTick || HybridEffects->Hybrid->IsBattlePaused()) return;
    LastHybridTick=At;Epoch=HybridEffects->Epoch();bInsideStep=true;bIndexReady=false;
    TArray<FGuid> Ids;Effects.GetKeys(Ids);const float Now=float(At)/60.f;
    for (const auto Id:Ids)
    {
        auto* R=HybridEffects->Find(Id);if (!R || !R->Admitted || R->Finished || !HybridEffects->Hybrid->HasSimulation(R->Key)) continue;
        const auto* I=Effects.Find(Id);if (!I) continue;
        if (I->bHoming) StepHybridHoming(Id,1.f/30.f,Now);
        else if (I->State.Kind==EGuLiCombatEffectKind::Projectile) StepProjectile(Id,1.f/30.f,Now);
        else if (I->State.Kind==EGuLiCombatEffectKind::SpellField) StepField(Id,Now);
        else StepSustainedHitscan(Id,Now);
    }
    StepWingmanBurstCooldowns(Now);bInsideStep=false;
}
FGuid UGuLiCombatEffectRuntimeSubsystem::AdmitHybridEffect(FGuLiRuntimeCombatEffect Instance)
{
    auto S=HybridEffects;auto* H=S?S->Hybrid.Get():nullptr;FGuLiHybridObjectKey Source,Target;FGuLiHybridObjectState Parent;
    if (!H || H->IsBattlePaused() || !H->ResolveTarget(Instance.Context.Source,Source) || !H->HasSimulation(Source)
        || !H->GetObject(Source,Parent) || (Instance.Context.Target.IsValid() && !H->ResolveTarget(Instance.Context.Target,Target))
        || S->Records.Num()>=int32(GuLiEffectRuntimeContract::MaximumRecords)) return {};
    const auto Rule=TEXT("admit/runtime/")+FGuLiHybridManifest::MakeDamageRuleId(Parent.Definition.ToString(),Instance.Context.SkillId,Instance.Context.WeaponBinding.SlotId);
    const auto* AdmissionRule=S->Manifest.Rules.FindByPredicate([&](const auto& R){return R.Id==Rule && R.SpawnKind==EGuLiHybridObjectKind::Field;});
    if (!AdmissionRule) return {};
    const FGuid Id=Instance.State.EffectId;if (!Id.IsValid() || S->Records.Contains(Id)) return {};
    FGuLiHybridObjectKey Key;Key.Kind=EGuLiHybridObjectKind::Field;Key.StableId=Id;
    GuLiEffectState::FRecord R;R.Key=Key;R.Source=Source;R.Target=Target;R.Instance=MoveTemp(Instance);
    R.Instance.SourceLease={};R.Instance.State.SourceTeam=Parent.Team;
    R.Admission=GuLiEffectState::Event(Key,TEXT("/admission"));R.Retirement=GuLiEffectState::Event(Key,TEXT("/retirement"));
    // FName/FString matching ignores case; the portable rule map does not.
    // Army profiles normalize names, so submit the exact hashed manifest ID.
    FGuLiHybridObjectRequest Request;Request.Source=Source;Request.Target=Key;Request.Event=R.Admission;Request.Rule=FName(*AdmissionRule->Id);
    Request.Operation=EGuLiHybridObjectOperation::Spawn;Request.Location=R.Instance.State.Location;
    TArray<uint8> Birth;if (!S->EncodeRecord(R,Birth)) return {};
    GuLiNet::RuntimeEffectRecipe::Recipe Frozen{GuLiNetBridge::ToCore(Key),GuLiNetBridge::ToCore(Source),GuLiNetBridge::ToCore(R.Admission),
        GuLiNet::Bytes(Birth.GetData(),Birth.GetData()+Birth.Num())};
    const auto Payload=GuLiNet::RuntimeEffectRecipe::Encode(Frozen);if (Payload.empty()) return {};
    Request.CapabilityPayload.Append(Payload.data(),int32(Payload.size()));
    S->ByPresentation.Add(Id,Id);S->Records.Add(Id,R);
    if (!H->SubmitObjectOperationNative(Request,{}).IsValid()) {S->Records.Remove(Id);S->ByPresentation.Remove(Id);return {};}
    return Id;
}
bool UGuLiCombatEffectRuntimeSubsystem::CanAdmitHybridEffects(TConstArrayView<FGuLiRuntimeCombatEffect> Instances) const
{
    const auto S=HybridEffects;const auto* H=S?S->Hybrid.Get():nullptr;
    if (!H || H->IsBattlePaused() || Instances.IsEmpty()
        || Instances.Num()+S->Records.Num()>int32(GuLiEffectRuntimeContract::MaximumRecords)) return false;
    TSet<FGuid> Ids;
    for (const auto& I:Instances)
    {
        FGuLiHybridObjectKey Source,Target;FGuLiHybridObjectState Parent;
        if (!I.State.IsWellFormed() || I.Context.MatchEpoch!=S->Epoch() || I.Context.SkillId.IsNone()
            || Ids.Contains(I.State.EffectId) || S->Records.Contains(I.State.EffectId)
            || !H->ResolveTarget(I.Context.Source,Source) || !H->HasSimulation(Source) || !H->GetObject(Source,Parent)
            || Parent.Health.bDead || (I.Context.Target.IsValid() && !H->ResolveTarget(I.Context.Target,Target))) return false;
        const auto Rule=TEXT("admit/runtime/")+FGuLiHybridManifest::MakeDamageRuleId(Parent.Definition.ToString(),I.Context.SkillId,I.Context.WeaponBinding.SlotId);
        if (!S->Manifest.Rules.ContainsByPredicate([&](const auto& R){return R.Id==Rule && R.SpawnKind==EGuLiHybridObjectKind::Field;})) return false;
        Ids.Add(I.State.EffectId);
    }
    return true;
}
namespace
{
FGuLiRuntimeCombatEffect HomingRecipe(const FGuLiLogicalMissileLaunchRequest& R,float Now)
{
    FGuLiRuntimeCombatEffect I;I.bHoming=true;auto& C=I.Context;
    C.MatchEpoch=R.MatchEpoch;C.Source=GuLiCombatTargets::MakeWingmanTargetHandle(R.Emitter);C.Emitter=R.Emitter;C.Target=R.Target;
    C.ShotId=R.MissileId;C.RootEventId=R.RootEventId;C.WeaponBinding=R.WeaponBinding;C.SkillId=R.SkillId;
    C.ProfileRevision=R.ProfileRevision;C.LoadoutRevision=R.LoadoutRevision;C.Damage=R.Damage;
    auto& S=I.State;S.MatchEpoch=R.MatchEpoch;S.EffectId=R.MissileId;S.Source=C.Source;S.Target=R.Target;S.Sequence=1;
    S.Location=S.LaunchLocation=R.LaunchPosition;S.LaunchDirection=R.LaunchDirection.GetSafeNormal();
    S.Velocity=FVector(S.LaunchDirection)*R.SpeedCentimetersPerSecond;
    S.StartTime=S.SampleTime=S.ActivationTime=Now;S.EndTime=Now+R.MaximumLifetimeSeconds;
    S.Motion.Speed=R.SpeedCentimetersPerSecond;S.Motion.TurnRate=FMath::Max(UE_SMALL_NUMBER,R.TurnRateDegreesPerSecond);
    I.HomingTurnRate=R.TurnRateDegreesPerSecond;
    S.Motion.SweepRadius=R.SweepRadiusCentimeters;S.Motion.MaximumLifetime=R.MaximumLifetimeSeconds;
    S.Motion.LiftSeconds=0;S.Motion.LateralOffset=S.Motion.VerticalCurve=S.Motion.LongitudinalCurve=0;
    I.LastCorrection=Now;return I;
}
}
bool UGuLiCombatEffectRuntimeSubsystem::CanLaunchHomingSalvo(TConstArrayView<FGuLiLogicalMissileLaunchRequest> Requests) const
{
    if (!HybridEffects || Requests.IsEmpty() || Requests.Num()>GULI_WINGMAN_MEMBERS_PER_FLIGHT) return false;
    TArray<FGuLiRuntimeCombatEffect> Recipes;TSet<FGuLiWingmanHandle> Emitters;
    for (const auto& R:Requests)
    {
        if (!R.IsWellFormed() || R.Emitter.Flight!=Requests[0].Emitter.Flight || R.Target!=Requests[0].Target
            || R.RootEventId!=Requests[0].RootEventId || !(R.WeaponBinding==Requests[0].WeaponBinding)
            || Emitters.Contains(R.Emitter)) return false;
        Emitters.Add(R.Emitter);Recipes.Add(HomingRecipe(R,BattleSeconds()));
    }
    return CanAdmitHybridEffects(Recipes);
}
bool UGuLiCombatEffectRuntimeSubsystem::LaunchHomingSalvo(TConstArrayView<FGuLiLogicalMissileLaunchRequest> Requests,int32& Count)
{
    Count=0;if (!CanLaunchHomingSalvo(Requests)) return false;
    for (const auto& R:Requests)
    {
        auto I=HomingRecipe(R,BattleSeconds());FGuLiCombatTargetSnapshot Target;
        if (Ledger->TryGetTargetSnapshot(R.Target,Target)) I.State.LastTargetLocation=Target.Location;
        if (!AdmitHybridEffect(MoveTemp(I)).IsValid())
        {HybridEffects->Hybrid->Suspend(TEXT("Preflighted homing salvo could not enqueue all admissions"));return false;}
        ++Count;++Counters.ProjectilesLaunched;
    }
    return true;
}
void UGuLiCombatEffectRuntimeSubsystem::StepHybridHoming(const FGuid& Id,float Delta,float Now)
{
    auto* Live=Effects.Find(Id);if (!Live) return;auto I=*Live;auto& S=I.State;auto& C=I.Context;
    FGuLiLogicalMissileState M;M.MatchEpoch=Epoch;M.MissileId=Id;M.ShotId=C.ShotId;M.RootEventId=C.RootEventId;
    M.WeaponBinding=C.WeaponBinding;M.SkillId=C.SkillId;M.Source=C.Source;M.Emitter=C.Emitter;M.Target=C.Target;
    M.ProfileRevision=C.ProfileRevision;M.LoadoutRevision=C.LoadoutRevision;M.Damage=C.Damage;
    M.Position=S.Location;M.Velocity=S.Velocity;M.SimulationSequence=S.Sequence;M.AgeSeconds=FMath::Max(0.f,S.SampleTime-S.StartTime);
    M.SpeedCentimetersPerSecond=S.Motion.Speed;M.TurnRateDegreesPerSecond=I.HomingTurnRate;
    M.SweepRadiusCentimeters=S.Motion.SweepRadius;M.MaximumLifetimeSeconds=S.Motion.MaximumLifetime;
    FGuLiLogicalMissileTerminalEvent Terminal;
    const bool Active=GuLiLogicalMissileMotion::Advance(*GetWorld(),*Ledger,M,Delta,Terminal);
    S.Location=M.Position;S.Velocity=M.Velocity;S.LastTargetLocation=M.LastTargetLocation;S.Sequence=M.SimulationSequence;S.SampleTime=Now;
    Effects[Id]=I;
    if (!Active)
    {
        if (Terminal.Reason==EGuLiLogicalMissileTerminalReason::Impact)
        {
            FGuLiCombatTargetSnapshot Target;
            if (!Ledger->TryGetTargetSnapshot(C.Target,Target) || !SubmitHybridPulse(Id,0,Target))
            {HybridEffects->Hybrid->Suspend(TEXT("Homing missile could not retain its impact transaction"));return;}
        }
        const auto Reason=Terminal.Reason==EGuLiLogicalMissileTerminalReason::Impact?EGuLiCombatEffectEndReason::Impact:
            Terminal.Reason==EGuLiLogicalMissileTerminalReason::Blocked?EGuLiCombatEffectEndReason::Blocked:
            Terminal.Reason==EGuLiLogicalMissileTerminalReason::Expired?EGuLiCombatEffectEndReason::Expired:EGuLiCombatEffectEndReason::Cancelled;
        Finish(Id,Reason,Now);return;
    }
    if (Now-I.LastCorrection>=.1f) {Effects[Id].LastCorrection=Now;Publish(Id,false);}
}
bool UGuLiCombatEffectRuntimeSubsystem::PublishHybridEffect(const FGuid& Id,bool Reliable)
{
    auto* R=HybridEffects?HybridEffects->Find(Id):nullptr;const auto* Live=Effects.Find(Id);if (!R || !Live) return false;
    R->Instance=*Live;const bool Sent=HybridEffects->Publish(*R,Reliable);if (Sent) R->Published=true;return Sent;
}
bool UGuLiCombatEffectRuntimeSubsystem::FinishHybridEffect(const FGuLiRuntimeCombatEffect& Instance)
{
    auto* R=HybridEffects?HybridEffects->Find(Instance.State.EffectId):nullptr;if (!R) return false;
    R->Instance=Instance;R->Finished=true;R->TerminalPublished=HybridEffects->Publish(*R,true);return true;
}
FGuid UGuLiCombatEffectRuntimeSubsystem::TransitionHybridEffect(FGuid Previous,FGuLiRuntimeCombatEffect Instance)
{
    auto* R=HybridEffects?HybridEffects->Find(Previous):nullptr;if (!R || !R->Admitted || R->Finished) return {};
    if (const auto* Old=Effects.Find(Previous)) R->Instance=*Old;
    R->Instance.State.Phase=EGuLiCombatEffectPhase::Finished;R->Instance.State.EndReason=EGuLiCombatEffectEndReason::Impact;
    ++R->Instance.State.Sequence;
    if (R->PendingPublic.Num()>=4) {HybridEffects->Hybrid->Suspend(TEXT("Effect transition publication capacity exhausted"));return {};}
    R->PendingPublic.Add(R->Instance.State);
    Instance.State.EffectId=GuLiEffectState::Event(R->Key,TEXT("/field"));Instance.State.SourceTeam=R->Instance.State.SourceTeam;
    R->Instance=MoveTemp(Instance);Effects.Remove(Previous);HybridEffects->ByPresentation.Remove(Previous);
    const FGuid Id=R->Instance.State.EffectId;Effects.Add(Id,R->Instance);HybridEffects->ByPresentation.Add(Id,R->Key.StableId);
    R->Published=HybridEffects->Publish(*R,true);return Id;
}
bool UGuLiCombatEffectRuntimeSubsystem::SubmitHybridPulse(const FGuid& Effect,int32 Pulse,const FGuLiCombatTargetSnapshot& Target)
{
    auto* R=HybridEffects?HybridEffects->Find(Effect):nullptr;if (!R || !R->Admitted) return false;
    FGuLiHybridObjectKey Key;if (!HybridEffects->Hybrid->ResolveTarget(Target.Handle,Key)) return false;
    const FGuid Root=GuLiEffectState::Event(R->Key,TEXT("/pulse/")+LexToString(Pulse));
    const FGuid Event=GuLiEffectState::Event(R->Key,TEXT("/hit/")+LexToString(Pulse)+TEXT("/")
        +Key.StableId.ToString(EGuidFormats::Digits)+TEXT("/")+LexToString(Key.LifeGeneration));
    if (R->Hits.ContainsByPredicate([&](const auto& Hit){return Hit.Event==Event;})) return true;
    uint32 Count=0;for (const auto& Entry:HybridEffects->Records) Count+=Entry.Value.Hits.Num();
    if (Count>=GuLiEffectRuntimeContract::MaximumPendingHits)
    {HybridEffects->Hybrid->Suspend(TEXT("Pending effect hit capacity exhausted"));return false;}
    GuLiEffectState::FHit Hit;Hit.Target=Key;Hit.Event=Event;Hit.Root=Root;Hit.Location=Target.Location;
    Hit.StartedAt=HybridEffects->Hybrid->GetObjectLogicalTick(R->Key);R->Hits.Add(Hit);return true;
}
bool UGuLiCombatEffectRuntimeSubsystem::ContainsHomingMissile(FGuid Id) const
{
    const auto* R=HybridEffects?HybridEffects->Records.Find(Id):nullptr;
    return R && R->Instance.bHoming && !R->Finished;
}
int32 UGuLiCombatEffectRuntimeSubsystem::GetActiveHomingMissileCount() const
{
    int32 Count=0;if (HybridEffects) for (const auto& Entry:HybridEffects->Records)
        if (Entry.Value.Instance.bHoming && Entry.Value.Admitted && !Entry.Value.Finished) ++Count;
    return Count;
}
bool UGuLiCombatEffectRuntimeSubsystem::IsHybridWingmanBurstBusy(const FGuLiWingmanHandle& Emitter,FName Slot) const
{
    const auto* Cooldowns=WingmanBurstCooldowns.Find(Emitter);if (Cooldowns && Cooldowns->Contains(Slot)) return true;
    if (HybridEffects) for (const auto& Entry:HybridEffects->Records)
    {
        const auto& R=Entry.Value;
        if (!R.Finished && R.Instance.State.Kind==EGuLiCombatEffectKind::SustainedHitscan
            && R.Instance.Context.Emitter==Emitter && R.Instance.State.SlotId==Slot) return true;
    }
    return false;
}
