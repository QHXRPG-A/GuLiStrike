from pathlib import Path
root=Path('Source/GuLiStrike/Commander')
p=root/'Network/GuLiCommanderStateStream.h'; s=p.read_text(encoding='utf-8-sig')
s=s.replace('uint8 Priority = 2;', 'uint8 Priority = 2;\n\tuint64 QueueTicket = 0;')
s=s.replace('bool bStarted = false;\n\n\tvoid Reset', '''bool bStarted = false;
\tstruct FQueueEntry { FGuLiSoldierId Id; uint64 Ticket; };
\tTArray<FQueueEntry> Queues[3];
\tint32 QueueHeads[3] = {0,0,0};
\tuint64 NextQueueTicket = 1;
\tTArray<FGuLiSoldierId> BaselineIds;
\tint32 BaselineCursor = 0;
\tvoid Queue(const FGuLiSoldierStateItem& State, const FGuLiMoveEndpointItem* Endpoint, double Now);
\tvoid Enqueue(FGuLiSoldierId Id, FPending& Value);
\tvoid Remove(FGuLiSoldierId Id, double Now);

\tvoid Reset''',1)
s=s.replace('FPrepared& Out) const;', 'FPrepared& Out);')
p.write_text(s,encoding='utf-8')
p=root/'Network/GuLiCommanderStateStream.cpp'; s=p.read_text(encoding='utf-8-sig')
a=s.index('\tfor (const auto& State : States) RequiredBaseline.Add',s.index('void FSender::Begin')); b=s.index('\nvoid FSender::Refresh',a)
s=s[:a]+'''\tBaselineIds.Reserve(States.Num());
\tfor (const auto& State : States) { RequiredBaseline.Add(State.SoldierId); BaselineIds.Add(State.SoldierId); }
\tBaselineCursor=0;
}

void FSender::Enqueue(FGuLiSoldierId Id, FPending& P)
{
\tP.QueueTicket=NextQueueTicket++;
\tQueues[P.Priority].Add({Id,P.QueueTicket});
}

void FSender::Queue(const FGuLiSoldierStateItem& State, const FGuLiMoveEndpointItem* Endpoint, double Now)
{
\tconst auto Id=State.SoldierId;
\tif (Retiring.Contains(Id)) return;
\tFPending* Existing=Pending.Find(Id);
\tif (Existing && Existing->bRemove) return;
\tconst FValue* Baseline=Published.Find(Id);
\tFValue Desired; Desired.State=State;
\tif (State.IsAlive() && State.ActiveOrderId && !State.bPhased && !State.bExternalActionsLocked)
\t{
\t\tif (Endpoint && Endpoint->ActiveOrderId==State.ActiveOrderId) Desired.Endpoint=*Endpoint;
\t\telse if (Endpoint)
\t\t{
\t\t\tconst FValue* Previous=Existing ? &Existing->Value : Baseline;
\t\t\tif (Previous && Previous->Endpoint.IsSet() && Previous->Endpoint->ActiveOrderId==State.ActiveOrderId) Desired.Endpoint=Previous->Endpoint;
\t\t}
\t}
\tif (!Delta(Desired,Baseline)) { Pending.Remove(Id); return; }
\tconst uint8 NewPriority=Priority(Desired,Baseline);
\tif (Existing)
\t{
\t\tif (Delta(Desired,&Existing->Value)) ++MergeCount;
\t\tExisting->Value=MoveTemp(Desired);
\t\tif (NewPriority < Existing->Priority) { Existing->Priority=NewPriority; Enqueue(Id,*Existing); }
\t}
\telse
\t{
\t\tFPending P; P.Value=MoveTemp(Desired); P.FirstWaitingTime=Now; P.Priority=NewPriority;
\t\tauto& Added=Pending.Add(Id,MoveTemp(P)); Enqueue(Id,Added);
\t}
}

void FSender::Remove(FGuLiSoldierId Id, double Now)
{
\tFGuLiSoldierStateItem State; State.SoldierId=Id;
\tif (const auto* Existing=Pending.Find(Id)) State=Existing->Value.State;
\telse if (const auto* Previous=Published.Find(Id)) State=Previous->State;
\tRetire(State,Now);
}
'''+s[b:]
# Compatibility refresh is no longer a production tick path, but must use the same queues.
a=s.index('\tTMap<FGuLiSoldierId, const FGuLiMoveEndpointItem*>',s.index('void FSender::Refresh')); b=s.index('\nvoid FSender::Retire',a)
s=s[:a]+'''\tTSet<FGuLiSoldierId> Current;
\tfor (const auto& State : States) { Current.Add(State.SoldierId); Queue(State,Endpoints.Find(State.SoldierId),Now); }
\tTArray<FGuLiSoldierId> Removed;
\tfor (const auto& P : Published) if (!Current.Contains(P.Key)) Removed.Add(P.Key);
\tfor (auto Id : Removed) Remove(Id,Now);
\tBaselineCursor=BaselineIds.Num();
}
'''+s[b:]
s=s.replace('P->bRemove = true; P->Priority = 0;', 'P->bRemove = true; P->Priority = 0; Enqueue(Id,*P);')
s=s.replace('bool FSender::Prepare(int32 MaxBytes, bool bUrgentOnly, double Now, FPrepared& Out) const', 'bool FSender::Prepare(int32 MaxBytes, bool bUrgentOnly, double Now, FPrepared& Out)')
s=s.replace('else if (!CompleteSequence && RequiredBaseline.IsEmpty())', 'else if (!CompleteSequence && BaselineCursor >= BaselineIds.Num() && RequiredBaseline.IsEmpty())')
a=s.index('\tTArray<FGuLiSoldierId> Ids; Pending.GetKeys',s.index('bool FSender::Prepare')); b=s.index('\tfor (auto Id : Ids)',a)
s=s[:a]+'''\tTArray<FGuLiSoldierId, TInlineAllocator<MaxRecords>> Ids;
\tfor (int32 PriorityIndex=0; PriorityIndex<3; ++PriorityIndex)
\t{
\t\tauto& Q=Queues[PriorityIndex]; auto& Head=QueueHeads[PriorityIndex];
\t\twhile (Head<Q.Num())
\t\t{
\t\t\tconst auto* P=Pending.Find(Q[Head].Id);
\t\t\tif (P && P->QueueTicket==Q[Head].Ticket) break;
\t\t\t++Head;
\t\t}
\t\tif (Head==Q.Num()) { Q.Reset(); Head=0; continue; }
\t\tfor (int32 I=Head; I<Q.Num() && Ids.Num()<MaxRecords; ++I)
\t\t{
\t\t\tconst auto* P=Pending.Find(Q[I].Id);
\t\t\tif (!P || P->QueueTicket!=Q[I].Ticket) continue;
\t\t\tif (bUrgentOnly && PriorityIndex==2 && Now-P->FirstWaitingTime<1.0) break;
\t\t\tIds.Add(Q[I].Id);
\t\t}
\t}
'''+s[b:]
s=s.replace('\t\tif (bUrgentOnly && EffectivePriority(Id) > 1) continue;\n','')
# Transactional decoder stages only records touched by this <=1000 byte batch.
s=s.replace('TMap<FGuLiSoldierId, FValue> Staged = Header.bStart ? TMap<FGuLiSoldierId, FValue>{} : Values;', 'TMap<FGuLiSoldierId, FValue> Staged;\n\tTSet<FGuLiSoldierId> Removed;')
s=s.replace('const auto* Previous = Staged.Find(FGuLiSoldierId(IdValue));', '''const auto Id=FGuLiSoldierId(IdValue);
\t\tconst auto* Previous = Staged.Find(Id);
\t\tif (!Previous && !Header.bStart && !Removed.Contains(Id)) Previous=Values.Find(Id);''')
s=s.replace('if (R.bRemove) Staged.Remove(R.Value.State.SoldierId);', 'if (R.bRemove) { Staged.Remove(R.Value.State.SoldierId); Removed.Add(R.Value.State.SoldierId); }')
s=s.replace('else { R.Value.PublishedSequence = Header.Sequence; Staged.Add(R.Value.State.SoldierId, R.Value); }', 'else { Removed.Remove(R.Value.State.SoldierId); R.Value.PublishedSequence = Header.Sequence; Staged.Add(R.Value.State.SoldierId, R.Value); }')
s=s.replace('Header.RosterCount != uint32(Staged.Num()) || Header.IdHash != HashIds(Staged)', 'Header.RosterCount != uint32(Values.Num()) || Header.IdHash != HashIds(Values)')
s=s.replace('Values = MoveTemp(Staged); AppliedSequence', 'if (Header.bStart) Values.Reset();\n\tfor (auto Id : Removed) Values.Remove(Id);\n\tfor (auto& P : Staged) Values.Add(P.Key,MoveTemp(P.Value));\n\tAppliedSequence')
p.write_text(s,encoding='utf-8')

p=root/'Framework/GuLiCommanderNetSyncComponent.h'; s=p.read_text(encoding='utf-8-sig')
s=s.replace('class AGuLiSoldierStateReplicator;', 'class AGuLiSoldierStateReplicator;\nclass UGuLiBattleAuthoritySubsystem;\nstruct FGuLiSoldierRosterDelta;\nstruct FGuLiMoveEndpointDelta;\nDECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiMoveEndpointIdsChanged, TConstArrayView<FGuLiSoldierId>, bool);')
s=s.replace('return MoveEndpoints.Find(SoldierId);', 'const int32* I=MoveEndpointIndex.Find(SoldierId);\n\t\treturn I && MoveEndpoints.Items.IsValidIndex(*I) ? &MoveEndpoints.Items[*I] : nullptr;')
s=s.replace('FGuLiMoveEndpointsChangedSignature OnMoveEndpointsChanged;', 'FGuLiMoveEndpointsChangedSignature OnMoveEndpointsChanged;\n\tFGuLiMoveEndpointIdsChanged OnMoveEndpointIdsChanged;')
s=s.replace('void TickStateStream();', '''void TickStateStream();
\tvoid TickEndpointSource();
\tvoid HandleAuthorityEndpoints(const FGuLiMoveEndpointDelta& Delta);
\tvoid HandleStreamRoster(const FGuLiSoldierRosterDelta& Delta);
\tvoid RefreshStreamMember(FGuLiSoldierId Id, double Now);
\tvoid SetLocalMoveEndpoint(FGuLiSoldierId Id, const FGuLiMoveEndpointItem* Endpoint);
\tvoid ClearLocalMoveEndpoints();
\tTMap<FGuLiSoldierId,int32> MoveEndpointIndex;
\tTSet<FGuLiSoldierId> DirtyStreamMembers;
\tTWeakObjectPtr<UGuLiBattleAuthoritySubsystem> EndpointAuthority;
\tEGuLiTeam EndpointTeam = EGuLiTeam::Unassigned;''')
p.write_text(s,encoding='utf-8')

p=root/'Framework/GuLiCommanderNetSyncComponent.cpp'; s=p.read_text(encoding='utf-8-sig')
a=s.index('\t\tconst bool bCanPublishMoveEndpoints'); b=s.index('\n\t}\n\tTickStateStream();',a)
s=s[:a]+'\t\tTickEndpointSource();'+s[b:]
s=s.replace('if (PoseRoster.IsValid()) PoseRoster->OnSoldiersRemoved().RemoveAll(this);', 'if (PoseRoster.IsValid()) { PoseRoster->OnSoldiersRemoved().RemoveAll(this); PoseRoster->OnRosterDelta.RemoveAll(this); }')
s=s.replace('CancelPendingServerMovePlanning();\n\tif (PoseRoster.IsValid())', 'CancelPendingServerMovePlanning();\n\tif (EndpointAuthority.IsValid()) EndpointAuthority->OnMoveEndpointsChanged.RemoveAll(this);\n\tif (PoseRoster.IsValid())',1)
s=s.replace('MoveEndpoints.Items.Reset(); OnMoveEndpointsChanged.Broadcast(MoveEndpoints);', 'ClearLocalMoveEndpoints();')
s=s.replace('Roster.OnSoldiersRemoved().AddUObject(this, &UGuLiCommanderNetSyncComponent::ForgetPoseSoldiers);', 'Roster.OnSoldiersRemoved().AddUObject(this, &UGuLiCommanderNetSyncComponent::ForgetPoseSoldiers);\n\tRoster.OnRosterDelta.AddUObject(this, &UGuLiCommanderNetSyncComponent::HandleStreamRoster);')
s=s.replace('StateSender.Begin(SoldierReplicator->GetItems(), World->GetRealTimeSeconds());', 'StateSender.Begin(SoldierReplicator->GetItems(), World->GetRealTimeSeconds());\n\tDirtyStreamMembers.Reset(); ClearLocalMoveEndpoints();')
a=s.index('bool UGuLiCommanderNetSyncComponent::ServerUpsertMoveEndpoint('); b=s.index('bool UGuLiCommanderNetSyncComponent::BeginServerMovePlanning(',a)
s=s[:a]+'''bool UGuLiCommanderNetSyncComponent::ServerUpsertMoveEndpoint(FGuLiSoldierId Id, uint32 Order, const FVector& Start, const FVector& End)
{
\tif (!GetOwner() || !GetOwner()->HasAuthority() || !Id.IsValid() || !Order) return false;
\tFGuLiMoveEndpointItem E; E.SoldierId=Id; E.ActiveOrderId=Order; E.CommandStart=Start; E.FinalDestination=End;
\tE.Revision=FindMoveEndpoint(Id) ? FindMoveEndpoint(Id)->Revision+1 : 1;
\tSetLocalMoveEndpoint(Id,&E); DirtyStreamMembers.Add(Id); return true;
}
bool UGuLiCommanderNetSyncComponent::ServerRemoveMoveEndpoint(FGuLiSoldierId Id)
{
\tif (!GetOwner() || !GetOwner()->HasAuthority() || !FindMoveEndpoint(Id)) return false;
\tSetLocalMoveEndpoint(Id,nullptr); DirtyStreamMembers.Add(Id); return true;
}
int32 UGuLiCommanderNetSyncComponent::ServerBootstrapMoveEndpoints(TConstArrayView<FGuLiMoveEndpointItem> Endpoints)
{
\tif (!GetOwner() || !GetOwner()->HasAuthority()) return 0;
\tClearLocalMoveEndpoints(); for (const auto& E : Endpoints) { SetLocalMoveEndpoint(E.SoldierId,&E); DirtyStreamMembers.Add(E.SoldierId); }
\treturn Endpoints.Num();
}
bool UGuLiCommanderNetSyncComponent::ServerClearMoveEndpoints()
{
\tif (!GetOwner() || !GetOwner()->HasAuthority() || MoveEndpoints.Items.IsEmpty()) return false;
\tfor (const auto& E : MoveEndpoints.Items) DirtyStreamMembers.Add(E.SoldierId);
\tClearLocalMoveEndpoints(); return true;
}

'''+s[b:]
s=s.replace('\tMoveEndpoints.Sanitize();\n\tOnMoveEndpointsChanged.Broadcast(MoveEndpoints);', '\tMoveEndpointIndex.Reset();\n\tfor (int32 I=0; I<MoveEndpoints.Items.Num(); ++I) MoveEndpointIndex.Add(MoveEndpoints.Items[I].SoldierId,I);\n\tOnMoveEndpointIdsChanged.Broadcast({},true);\n\tOnMoveEndpointsChanged.Broadcast(MoveEndpoints);')
p.write_text(s,encoding='utf-8')

p=root/'Framework/GuLiCommanderStateDispatch.cpp'; s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include "Commander/Network/GuLiSoldierStateReplicator.h"', '#include "Commander/Network/GuLiSoldierStateReplicator.h"\n#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"\n#include "HAL/PlatformTime.h"')
s=s.replace('StateSender.Reset(); StateReceiver.Reset(); ReceivedStateBatches.Reset();', 'StateSender.Reset(); StateReceiver.Reset(); ReceivedStateBatches.Reset();\n\tDirtyStreamMembers.Reset(); ClearLocalMoveEndpoints();')
s=s.replace('if (Header.bStart) { Roster->ResetRemoteRoster(Epoch); MoveEndpoints.Items.Reset(); }', 'if (Header.bStart) { Roster->ResetRemoteRoster(Epoch); ClearLocalMoveEndpoints(); }')
a=s.index('\t\t\t\tconst int32 Index = MoveEndpoints.Items.IndexOfByPredicate'); b=s.index('\t\t\t\tif (Record.bRemove) Removed.Add',a)
s=s[:a]+'''\t\t\t\tSetLocalMoveEndpoint(Id, !Record.bRemove && Record.Value.Endpoint.IsSet() ? &Record.Value.Endpoint.GetValue() : nullptr);
'''+s[b:]
s=s.replace('\t\t\tif (Header.bStart || !Records.IsEmpty()) OnRep_MoveEndpoints();','')
s=s.replace('StateSender.Refresh(Roster->GetItems(), MoveEndpoints, Now);', '''// Build the baseline and consume coalesced deltas with a cursor. Never scan the roster per 30 Hz connection tick.
\tconst double WorkStarted=FPlatformTime::Seconds(); int32 Work=0;
\twhile (StateSender.BaselineCursor<StateSender.BaselineIds.Num() && Work<128 && FPlatformTime::Seconds()-WorkStarted<.0005)
\t{
\t\tconst auto Id=StateSender.BaselineIds[StateSender.BaselineCursor++];
\t\tRefreshStreamMember(Id,Now); DirtyStreamMembers.Remove(Id); ++Work;
\t}
\twhile (!DirtyStreamMembers.IsEmpty() && Work<128 && FPlatformTime::Seconds()-WorkStarted<.0005)
\t{
\t\tauto It=DirtyStreamMembers.CreateIterator(); const auto Id=*It; It.RemoveCurrent();
\t\tRefreshStreamMember(Id,Now); ++Work;
\t}''')
s+='''
void UGuLiCommanderNetSyncComponent::SetLocalMoveEndpoint(FGuLiSoldierId Id, const FGuLiMoveEndpointItem* Endpoint)
{
\tconst int32* Found=MoveEndpointIndex.Find(Id);
\tif (!Endpoint)
\t{
\t\tif (!Found) return;
\t\tconst int32 Index=*Found; MoveEndpointIndex.Remove(Id);
\t\tMoveEndpoints.Items.RemoveAtSwap(Index,1,EAllowShrinking::No);
\t\tif (MoveEndpoints.Items.IsValidIndex(Index)) MoveEndpointIndex.Add(MoveEndpoints.Items[Index].SoldierId,Index);
\t}
\telse
\t{
\t\tif (Found)
\t\t{
\t\t\tconst auto& Previous=MoveEndpoints.Items[*Found];
\t\t\tif (Previous.ActiveOrderId==Endpoint->ActiveOrderId && Previous.Revision==Endpoint->Revision
\t\t\t\t&& Previous.CommandStart==Endpoint->CommandStart && Previous.FinalDestination==Endpoint->FinalDestination) return;
\t\t\tMoveEndpoints.Items[*Found]=*Endpoint;
\t\t}
\t\telse MoveEndpointIndex.Add(Id,MoveEndpoints.Items.Add(*Endpoint));
\t}
\tOnMoveEndpointIdsChanged.Broadcast(MakeArrayView(&Id,1),false);
}
void UGuLiCommanderNetSyncComponent::ClearLocalMoveEndpoints()
{
\tMoveEndpointIndex.Reset(); MoveEndpoints.Items.Reset();
\tOnMoveEndpointIdsChanged.Broadcast({},true);
\tOnMoveEndpointsChanged.Broadcast(MoveEndpoints);
}
void UGuLiCommanderNetSyncComponent::TickEndpointSource()
{
\tauto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
\tif (EndpointAuthority.Get()!=Authority)
\t{
\t\tif (EndpointAuthority.IsValid()) EndpointAuthority->OnMoveEndpointsChanged.RemoveAll(this);
\t\tEndpointAuthority=Authority;
\t\tif (Authority) Authority->OnMoveEndpointsChanged.AddUObject(this,&UGuLiCommanderNetSyncComponent::HandleAuthorityEndpoints);
\t}
\tconst auto* Player=GetBattlePlayerState();
\tconst auto Team=Player && Player->IsCommander() && IsConnectionReady() ? Player->GetTeam() : EGuLiTeam::Unassigned;
\tif (EndpointTeam!=Team)
\t{
\t\tServerClearMoveEndpoints(); EndpointTeam=Team;
\t\t// Role changes are rare. Revisit the existing baseline IDs incrementally to add/remove private endpoints.
\t\tStateSender.BaselineCursor=0;
\t}
}
void UGuLiCommanderNetSyncComponent::HandleAuthorityEndpoints(const FGuLiMoveEndpointDelta& Delta)
{
\tif (Delta.bReset) { ServerClearMoveEndpoints(); StateSender.BaselineCursor=0; }
\tfor (const auto& E : Delta.Upserts) if (E.Team==EndpointTeam || FindMoveEndpoint(E.SoldierId)) DirtyStreamMembers.Add(E.SoldierId);
\tfor (auto Id : Delta.Removed) DirtyStreamMembers.Add(Id);
}
void UGuLiCommanderNetSyncComponent::HandleStreamRoster(const FGuLiSoldierRosterDelta& Delta)
{
\tif (!GetOwner() || !GetOwner()->HasAuthority()) return;
\tif (Delta.bReset) StateSender.BaselineCursor=0;
\tfor (auto Id : Delta.Added) DirtyStreamMembers.Add(Id);
\tfor (auto Id : Delta.Removed) { DirtyStreamMembers.Remove(Id); StateSender.Remove(Id,GetWorld()->GetRealTimeSeconds()); SetLocalMoveEndpoint(Id,nullptr); }
\tfor (const auto& P : Delta.Changed) DirtyStreamMembers.Add(P.Key);
}
void UGuLiCommanderNetSyncComponent::RefreshStreamMember(FGuLiSoldierId Id, double Now)
{
\tconst auto* State=PoseRoster.IsValid() ? PoseRoster->FindSoldierState(Id) : nullptr;
\tif (!State) { StateSender.Remove(Id,Now); SetLocalMoveEndpoint(Id,nullptr); return; }
\tconst auto* Source=EndpointAuthority.IsValid() ? EndpointAuthority->FindActiveMoveEndpoint(Id) : nullptr;
\tFGuLiMoveEndpointItem Endpoint; const FGuLiMoveEndpointItem* Desired=nullptr;
\tif (Source && Source->Team==EndpointTeam && Source->ActiveOrderId==State->ActiveOrderId && State->IsAlive()
\t\t&& !State->bPhased && !State->bExternalActionsLocked)
\t{
\t\tEndpoint.SoldierId=Id; Endpoint.ActiveOrderId=Source->ActiveOrderId; Endpoint.CommandStart=Source->CommandStart;
\t\tEndpoint.FinalDestination=Source->FinalDestination; Endpoint.Revision=FMath::Max(1u,Source->Revision); Desired=&Endpoint;
\t}
\telse if (Source && Source->Team==EndpointTeam && Source->ActiveOrderId!=State->ActiveOrderId)
\t{
\t\t// Authority committed ahead of roster capture. Leave the old paired state intact until the roster delta arrives.
\t\treturn;
\t}
\tSetLocalMoveEndpoint(Id,Desired);
\tStateSender.Queue(*State,Desired,Now);
}
'''
p.write_text(s,encoding='utf-8')
