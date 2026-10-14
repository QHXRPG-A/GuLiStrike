#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Network/Private/GuLiHybridReflectedState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"

namespace
{
struct FPrivate
{
    FString AbilitySet;uint64 Checksum=0;FGuLiShipAbilityLoadoutState Loadout;
    FGuLiShipAbilityProjectionContext Context;FGuLiGroupAbilityConfigSnapshot Observed;
    TArray<FName> Pressed;TArray<FGuLiShipCapabilityTimerView> Cooldowns;
    uint32 AbilityRevision=0,WeaponRevision=0,ProjectionRevision=0,ObservedAbilityRevision=0,ObservedRevision=0;
    bool InputEnabled=false,Initialized=false,OwnsCooldown=false;
    struct FPending {uint8 Slot=0;uint32 Generation=1;FGuid Event;uint64 StartedAt=0;FVector Position=FVector::ZeroVector;float Facing=0;};
    struct FReplenish {uint8 Slot=0;uint32 Generation=0;uint64 DueAt=0;};
    TArray<FPending> Spawns;TArray<FReplenish> Replenishment;uint64 NextSpawnTick=0;
};
void SerializePrivate(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Schema=2;A.Value(Schema);if (Schema!=2) A.Reject();
    A.String(V.AbilitySet);A.Value(V.Checksum);FGuLiHybridReflectedState::Value(A,V.Loadout);
    FGuLiHybridReflectedState::Value(A,V.Context);FGuLiHybridReflectedState::Value(A,V.Observed);
    A.Array(V.Pressed,32,[](auto& Ar,auto& N){Ar.Value(N);if (N.IsNone()) Ar.Reject();});
    A.Array(V.Cooldowns,128,[](auto& Ar,auto& C)
    {FGuLiHybridReflectedState::Value(Ar,C);if (C.TimerId.IsNone() || C.EndsAtServerSeconds<0) Ar.Reject();});
    A.Value(V.AbilityRevision);A.Value(V.WeaponRevision);A.Value(V.ProjectionRevision);
    A.Value(V.ObservedAbilityRevision);A.Value(V.ObservedRevision);A.Value(V.InputEnabled);A.Value(V.Initialized);A.Value(V.OwnsCooldown);
    A.Value(V.NextSpawnTick);
    A.Array(V.Spawns,GULI_WINGMAN_GROUP_SIZE,[](auto& Ar,auto& P)
    {Ar.Value(P.Slot);Ar.Value(P.Generation);Ar.Value(P.Event);Ar.Value(P.StartedAt);Ar.Value(P.Position);Ar.Value(P.Facing);
     if (P.Slot>=GULI_WINGMAN_GROUP_SIZE || !P.Generation || !P.Event.IsValid() || !P.StartedAt) Ar.Reject();});
    A.Array(V.Replenishment,GULI_WINGMAN_GROUP_SIZE,[](auto& Ar,auto& P)
    {Ar.Value(P.Slot);Ar.Value(P.Generation);Ar.Value(P.DueAt);if (P.Slot>=GULI_WINGMAN_GROUP_SIZE || !P.Generation || !P.DueAt) Ar.Reject();});
    if (V.AbilitySet.IsEmpty() || !V.Checksum || !V.Loadout.IsWellFormed() || !V.AbilityRevision || !V.WeaponRevision || !V.ProjectionRevision
        || (!(V.Context==FGuLiShipAbilityProjectionContext{}) && !V.Context.IsWellFormed())
        || (V.ObservedRevision && (!V.Observed.IsWellFormed() || V.Observed.SnapshotRevision!=V.ObservedRevision
            || V.Observed.AbilitySetRevision!=V.ObservedAbilityRevision))) A.Reject();
    TSet<FName> Pressed,Timers;
    for (const auto N:V.Pressed) {if (Pressed.Contains(N)) A.Reject();Pressed.Add(N);}
    for (const auto& C:V.Cooldowns) {if (Timers.Contains(C.TimerId)) A.Reject();Timers.Add(C.TimerId);}
    TSet<uint8> SpawnSlots,DeadSlots;TSet<FGuid> Events;
    for (const auto& P:V.Spawns) {if (SpawnSlots.Contains(P.Slot) || Events.Contains(P.Event)) A.Reject();SpawnSlots.Add(P.Slot);Events.Add(P.Event);}
    for (const auto& P:V.Replenishment) {if (DeadSlots.Contains(P.Slot)) A.Reject();DeadSlots.Add(P.Slot);}
    if ((!V.Spawns.IsEmpty() || !V.Replenishment.IsEmpty()) && !V.Context.IsWellFormed()) A.Reject();
}
bool ReadPrivate(TConstArrayView<uint8> Bytes,FPrivate& V)
{FGuLiHybridStateArchive A(Bytes);SerializePrivate(A,V);return A.Done();}
bool Compatible(const FPrivate& V,const UDataAsset* Configuration)
{
    const auto* D=Cast<UGuLiShipHangarDefinition>(Configuration);if (!D || !D->AbilitySet || D->AbilitySet->GetPathName()!=V.AbilitySet) return false;
    auto Loadout=D->Loadout;Loadout.Normalize();
    if (V.Loadout!=Loadout || V.Checksum!=D->AbilitySet->ComputeLoadoutChecksum(Loadout)) return false;
    for (const auto N:V.Pressed)
    {
        const auto Id=FGameplayTag::RequestGameplayTag(N,false);const auto* Grant=D->AbilitySet->FindGrant(Id);
        if (!Id.IsValid() || !Loadout.Contains(Id) || !Grant || !Grant->InputTag.IsValid()) return false;
    }
    if (V.Context.IsWellFormed() && V.Context.WingmanTypeId!=D->AbilitySet->WingmanTypeId) return false;
    return true;
}
}
uint64 UGuLiShipHangarCapabilityComponent::GetConfigurationChecksum(const UDataAsset* Configuration) const
{
    const auto* D=Cast<UGuLiShipHangarDefinition>(Configuration);if (!D || !D->AbilitySet) return 0;
    auto Loadout=D->Loadout;Loadout.Normalize();return D->AbilitySet->ComputeLoadoutChecksum(Loadout);
}
bool UGuLiShipHangarCapabilityComponent::CapturePrivateState(TArray<uint8>& Bytes) const
{
    // Member combat and replenishment are part of the owner capability state.
    if (!AppliedAbilitySet) return false;
    FPrivate V;V.AbilitySet=AppliedAbilitySet->GetPathName();V.Checksum=AppliedLoadoutChecksum;V.Loadout=AppliedLoadout;
    V.Context=ProjectionContext;V.Observed=ObservedGroupAbilityConfig;V.Cooldowns=CooldownViews;
    V.AbilityRevision=AbilitySetRevision;V.WeaponRevision=WeaponLoadoutRevision;V.ProjectionRevision=ProjectionSnapshotRevision;
    V.ObservedAbilityRevision=ObservedAbilitySetRevision;V.ObservedRevision=ObservedSnapshotRevision;
    V.InputEnabled=bActiveAbilityInputEnabled;V.Initialized=bInitializedOnAuthority;V.OwnsCooldown=bOwnsCooldownDomain;
    V.NextSpawnTick=NextHybridSpawnTick;
    TArray<uint8> Slots;HybridSpawns.GetKeys(Slots);Slots.Sort();for (const auto Slot:Slots)
    {const auto& P=HybridSpawns[Slot];V.Spawns.Add({P.Slot,P.Generation,P.Event,P.StartedAt,P.Position,P.Facing});}
    HybridReplenishment.GetKeys(Slots);Slots.Sort();for (const auto Slot:Slots)
    {const auto& P=HybridReplenishment[Slot];V.Replenishment.Add({Slot,P.Generation,P.DueAt});}
    for (const auto Id:PressedAbilities) V.Pressed.Add(Id.GetTagName());V.Pressed.Sort(FNameLexicalLess());
    if (!Compatible(V,CapabilityConfiguration)) return false;
    FGuLiHybridStateArchive A;SerializePrivate(A,V);return A.Finish(Bytes);
}
bool UGuLiShipHangarCapabilityComponent::ValidatePrivateState(TConstArrayView<uint8> Bytes,const UDataAsset* Configuration) const
{FPrivate V;return ReadPrivate(Bytes,V) && Compatible(V,Configuration);}
bool UGuLiShipHangarCapabilityComponent::RestorePrivateState(TConstArrayView<uint8> Bytes)
{
    FPrivate V;if (!ReadPrivate(Bytes,V) || !Compatible(V,CapabilityConfiguration)) return false;
    AppliedLoadout=V.Loadout;AppliedLoadoutChecksum=V.Checksum;ProjectionContext=V.Context;ObservedGroupAbilityConfig=V.Observed;
    AbilitySetRevision=V.AbilityRevision;WeaponLoadoutRevision=V.WeaponRevision;ProjectionSnapshotRevision=V.ProjectionRevision;
    ObservedAbilitySetRevision=V.ObservedAbilityRevision;ObservedSnapshotRevision=V.ObservedRevision;
    bActiveAbilityInputEnabled=V.InputEnabled;bInitializedOnAuthority=V.Initialized;bOwnsCooldownDomain=V.OwnsCooldown;
    NextHybridSpawnTick=V.NextSpawnTick;HybridSpawns.Reset();HybridReplenishment.Reset();
    for (const auto& P:V.Spawns) HybridSpawns.Add(P.Slot,{P.Slot,P.Generation,P.Event,P.StartedAt,P.Position,P.Facing});
    for (const auto& P:V.Replenishment) HybridReplenishment.Add(P.Slot,{P.Generation,P.DueAt});
    PressedAbilities.Reset();for (const auto N:V.Pressed) PressedAbilities.Add(FGameplayTag::RequestGameplayTag(N,false));CooldownViews=V.Cooldowns;
    if (const auto* Battle=GetWorld()?GetWorld()->GetGameState<AGuLiBattleGameState>():nullptr;Battle && ProjectionContext.IsWellFormed())
        ProjectionContext.MatchEpoch=Battle->GetMatchEpoch();
    RebuildWeaponBindingIndex();return true;
}
