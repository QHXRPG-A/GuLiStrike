#include "Commander/Network/GuLiCommanderStateStream.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace GuLiCommanderStateStream
{
namespace
{
enum : uint16
{
	Team = 1 << 0, Type = 1 << 1, Life = 1 << 2, Control = 1 << 3,
	Displacement = 1 << 4, Health = 1 << 5, Revision = 1 << 6, Order = 1 << 7,
	Endpoint = 1 << 8, All = (1 << 9) - 1, Remove = 1 << 9
};

uint32 Next(uint32 Sequence) { return Sequence == MAX_uint32 ? 1u : Sequence + 1u; }
bool Newer(uint32 A, uint32 B) { return int32(A - B) > 0; }

bool SameEndpoint(const TOptional<FGuLiMoveEndpointItem>& A, const TOptional<FGuLiMoveEndpointItem>& B)
{
	return A.IsSet() == B.IsSet() && (!A.IsSet() || (A->ActiveOrderId == B->ActiveOrderId
		&& A->CommandStart == B->CommandStart && A->FinalDestination == B->FinalDestination
		&& A->Revision == B->Revision));
}

uint16 Delta(const FValue& A, const FValue* B)
{
	if (!B) return All;
	const auto& X = A.State; const auto& Y = B->State;
	uint16 Mask = 0;
	if (X.Team != Y.Team) Mask |= Team;
	if (X.UnitTypeId != Y.UnitTypeId) Mask |= Type;
	if (X.LifeState != Y.LifeState) Mask |= Life;
	if (X.bPhased != Y.bPhased || X.bExternalActionsLocked != Y.bExternalActionsLocked) Mask |= Control;
	if (X.DisplacementFrameFloor != Y.DisplacementFrameFloor || X.DisplacementLocation != Y.DisplacementLocation
		|| X.DisplacementYaw != Y.DisplacementYaw || X.DisplacementSimulationTime != Y.DisplacementSimulationTime) Mask |= Displacement;
	if (X.Health != Y.Health || X.MaxHealth != Y.MaxHealth) Mask |= Health;
	if (X.StateRevision != Y.StateRevision) Mask |= Revision;
	if (X.ActiveOrderId != Y.ActiveOrderId) Mask |= Order;
	if (!SameEndpoint(A.Endpoint, B->Endpoint)) Mask |= Endpoint;
	return Mask;
}

uint8 Priority(const FValue& Value, const FValue* Previous)
{
	if (!Value.State.IsAlive()) return 0;
	if (Previous && ((Previous->State.ActiveOrderId && !Value.State.ActiveOrderId)
		|| Previous->State.DisplacementFrameFloor != Value.State.DisplacementFrameFloor
		|| Previous->State.bPhased != Value.State.bPhased
		|| Previous->State.bExternalActionsLocked != Value.State.bExternalActionsLocked)) return 1;
	return 2;
}

void SerializeHeader(FArchive& Ar, FHeader& Header, uint16& Count)
{
	Ar << Header.Protocol << Header.ConnectionGeneration << Header.Generation << Header.Epoch << Header.Sequence;
	uint8 Flags = (Header.bStart ? 1 : 0) | (Header.bComplete ? 2 : 0);
	Ar << Flags << Count << Header.RosterCount << Header.IdHash;
	if (Ar.IsLoading())
	{
		Header.bStart = (Flags & 1) != 0; Header.bComplete = (Flags & 2) != 0;
		if (Flags > 3 || (Header.bStart && Header.bComplete)) Ar.SetError();
	}
}

// Preserve FVector_NetQuantize's whole-centimetre precision, without object references.
void SerializeEndpointVector(FArchive& Ar, FVector_NetQuantize& Value)
{
	int32 X = FMath::RoundToInt(Value.X), Y = FMath::RoundToInt(Value.Y), Z = FMath::RoundToInt(Value.Z);
	Ar << X << Y << Z;
	if (Ar.IsLoading()) Value = FVector(X, Y, Z);
}

void SerializeRecord(FArchive& Ar, FRecord& Record)
{
	auto& S = Record.Value.State;
	Ar.SerializeIntPacked(S.SoldierId.Value);
	uint16 Mask = Record.bRemove ? Remove : Record.Mask;
	Ar << Mask;
	Record.Mask = Mask; Record.bRemove = Mask == Remove;
	if (!S.SoldierId.IsValid() || Mask == 0 || (Mask & ~(All | Remove)) || ((Mask & Remove) && Mask != Remove))
	{
		Ar.SetError(); return;
	}
	if (Record.bRemove) return;
	if (Mask & Team) { uint8 V = uint8(S.Team); Ar << V; S.Team = EGuLiTeam(V); }
	if (Mask & Type) Ar << S.UnitTypeId;
	if (Mask & Life) { uint8 V = uint8(S.LifeState); Ar << V; S.LifeState = EGuLiSoldierLifeState(V); }
	if (Mask & Control)
	{
		uint8 V = (S.bPhased ? 1 : 0) | (S.bExternalActionsLocked ? 2 : 0);
		Ar << V; if (V > 3) Ar.SetError();
		S.bPhased = (V & 1) != 0; S.bExternalActionsLocked = (V & 2) != 0;
	}
	if (Mask & Displacement)
	{
		Ar.SerializeIntPacked(S.DisplacementFrameFloor);
		Ar << S.DisplacementLocation << S.DisplacementYaw << S.DisplacementSimulationTime;
	}
	if (Mask & Health) Ar << S.Health << S.MaxHealth;
	if (Mask & Revision) Ar.SerializeIntPacked(S.StateRevision);
	if (Mask & Order) Ar.SerializeIntPacked(S.ActiveOrderId);
	if (Mask & Endpoint)
	{
		uint8 Present = Record.Value.Endpoint.IsSet() ? 1 : 0; Ar << Present;
		if (Present > 1) { Ar.SetError(); return; }
		if (Present)
		{
			if (!Record.Value.Endpoint.IsSet()) Record.Value.Endpoint.Emplace();
			auto& E = Record.Value.Endpoint.GetValue(); E.SoldierId = S.SoldierId;
			Ar.SerializeIntPacked(E.ActiveOrderId); Ar.SerializeIntPacked(E.Revision);
			SerializeEndpointVector(Ar, E.CommandStart); SerializeEndpointVector(Ar, E.FinalDestination);
		}
		else Record.Value.Endpoint.Reset();
	}
}

bool Valid(const FValue& V)
{
	const auto& S = V.State;
	return S.SoldierId.IsValid() && S.UnitTypeId != 0
		&& (S.Team == EGuLiTeam::Red || S.Team == EGuLiTeam::Blue || S.Team == EGuLiTeam::Unassigned)
		&& (S.LifeState == EGuLiSoldierLifeState::Alive || S.LifeState == EGuLiSoldierLifeState::Destroyed)
		&& FMath::IsFinite(S.Health) && FMath::IsFinite(S.MaxHealth) && S.Health >= 0 && S.MaxHealth > 0
		&& !S.DisplacementLocation.ContainsNaN() && FMath::IsFinite(S.DisplacementYaw)
		&& FMath::IsFinite(S.DisplacementSimulationTime) && S.DisplacementSimulationTime >= 0
		&& (!V.Endpoint.IsSet() || (V.Endpoint->IsValid() && S.IsAlive() && V.Endpoint->ActiveOrderId == S.ActiveOrderId));
}

void Encode(FPrepared& Batch)
{
	Batch.Block.Data.Reset();
	FMemoryWriter Writer(Batch.Block.Data);
	uint16 Count = uint16(Batch.Records.Num());
	SerializeHeader(Writer, Batch.Header, Count);
	for (FRecord& Record : Batch.Records) SerializeRecord(Writer, Record);
}
}

uint64 HashIds(const TMap<FGuLiSoldierId, FValue>& Values)
{
	TArray<FGuLiSoldierId> Ids; Values.GetKeys(Ids); Ids.Sort();
	uint64 Hash = 14695981039346656037ull;
	for (const auto Id : Ids) for (uint32 Shift = 0; Shift < 32; Shift += 8)
	{
		Hash ^= uint8(Id.Value >> Shift); Hash *= 1099511628211ull;
	}
	return Hash;
}

bool ReadHeader(const FGuLiEncodedStateBatch& Block, FHeader& OutHeader)
{
	if (Block.Data.IsEmpty() || Block.Data.Num() > MaxWireBytes - 2) return false;
	FMemoryReader Reader(Block.Data); uint16 Count = 0; SerializeHeader(Reader, OutHeader, Count);
	return !Reader.IsError() && OutHeader.Protocol == GULI_COMMANDER_PROTOCOL_VERSION
		&& OutHeader.ConnectionGeneration && OutHeader.Generation && OutHeader.Epoch && OutHeader.Sequence;
}

void FSender::Reset(uint32 Connection, uint32 Generation, uint32 Epoch)
{
	*this = FSender{};
	Session.ConnectionGeneration = Connection; Session.Generation = Generation; Session.Epoch = Epoch;
}

void FSender::Begin(TConstArrayView<FGuLiSoldierStateItem> States, double Now)
{
	BaselineIds.Reserve(States.Num());
	for (const auto& State : States) { RequiredBaseline.Add(State.SoldierId); BaselineIds.Add(State.SoldierId); }
	BaselineCursor=0;
}

void FSender::Enqueue(FGuLiSoldierId Id, FPending& P)
{
	P.QueueTicket=NextQueueTicket++;
	Queues[P.Priority].Add({Id,P.QueueTicket});
}

void FSender::Queue(const FGuLiSoldierStateItem& State, const FGuLiMoveEndpointItem* Endpoint, double Now)
{
	const auto Id=State.SoldierId;
	if (Retiring.Contains(Id)) return;
	FPending* Existing=Pending.Find(Id);
	if (Existing && Existing->bRemove) return;
	const FValue* Baseline=Published.Find(Id);
	FValue Desired; Desired.State=State;
	if (State.IsAlive() && State.ActiveOrderId && !State.bPhased && !State.bExternalActionsLocked)
	{
		if (Endpoint && Endpoint->ActiveOrderId==State.ActiveOrderId) Desired.Endpoint=*Endpoint;
		else if (Endpoint)
		{
			const FValue* Previous=Existing ? &Existing->Value : Baseline;
			if (Previous && Previous->Endpoint.IsSet() && Previous->Endpoint->ActiveOrderId==State.ActiveOrderId) Desired.Endpoint=Previous->Endpoint;
		}
	}
	if (!Delta(Desired,Baseline)) { Pending.Remove(Id); return; }
	const uint8 NewPriority=Priority(Desired,Baseline);
	if (Existing)
	{
		if (Delta(Desired,&Existing->Value)) ++MergeCount;
		Existing->Value=MoveTemp(Desired);
		if (NewPriority < Existing->Priority) { Existing->Priority=NewPriority; Enqueue(Id,*Existing); }
	}
	else
	{
		FPending P; P.Value=MoveTemp(Desired); P.FirstWaitingTime=Now; P.Priority=NewPriority;
		auto& Added=Pending.Add(Id,MoveTemp(P)); Enqueue(Id,Added);
	}
}

void FSender::Remove(FGuLiSoldierId Id, double Now)
{
	FGuLiSoldierStateItem State; State.SoldierId=Id;
	if (const auto* Existing=Pending.Find(Id)) State=Existing->Value.State;
	else if (const auto* Previous=Published.Find(Id)) State=Previous->State;
	Retire(State,Now);
}

void FSender::Refresh(TConstArrayView<FGuLiSoldierStateItem> States,
	const FGuLiMoveEndpointFastArray& Endpoints, double Now)
{
	TSet<FGuLiSoldierId> Current;
	for (const auto& State : States) { Current.Add(State.SoldierId); Queue(State,Endpoints.Find(State.SoldierId),Now); }
	TArray<FGuLiSoldierId> Removed;
	for (const auto& P : Published) if (!Current.Contains(P.Key)) Removed.Add(P.Key);
	for (auto Id : Removed) Remove(Id,Now);
	BaselineCursor=BaselineIds.Num();
}

void FSender::Retire(const FGuLiSoldierStateItem& FinalState, double Now)
{
	const auto Id = FinalState.SoldierId;
	Retiring.Add(Id);
	if (!Published.Contains(Id) && !Pending.Contains(Id) && !RequiredBaseline.Contains(Id)) return;
	FPending* P = Pending.Find(Id);
	if (!P) { FPending New; New.FirstWaitingTime = Now; P = &Pending.Add(Id, MoveTemp(New)); }
	P->Value.State = FinalState; P->Value.Endpoint.Reset(); P->bRemove = true; P->Priority = 0; Enqueue(Id,*P);
}

bool FSender::Prepare(int32 MaxBytes, bool bUrgentOnly, double Now, FPrepared& Out)
{
	Out = FPrepared{};
	if (!Session.Generation || InFlight.Num() >= MaxInFlight) return false;
	MaxBytes = FMath::Min(MaxBytes, MaxWireBytes);
	Out.Header = Session; Out.Header.Sequence = Next(LastSequence);
	if (!bStarted) Out.Header.bStart = true;
	else if (!CompleteSequence && BaselineCursor >= BaselineIds.Num() && RequiredBaseline.IsEmpty())
	{
		Out.Header.bComplete = true; Out.Header.RosterCount = Published.Num(); Out.Header.IdHash = HashIds(Published);
	}
	Encode(Out);
	if (Out.Block.Data.Num() + 2 > MaxBytes)
	{
		Out.bBudgetLimited = Out.Header.bStart || Out.Header.bComplete || !Pending.IsEmpty(); return false;
	}
	if (Out.Header.bStart || Out.Header.bComplete) return true;
	TArray<FGuLiSoldierId, TInlineAllocator<MaxRecords>> Ids;
	for (int32 PriorityIndex=0; PriorityIndex<3; ++PriorityIndex)
	{
		auto& Q=Queues[PriorityIndex]; auto& Head=QueueHeads[PriorityIndex];
		while (Head<Q.Num())
		{
			const auto* P=Pending.Find(Q[Head].Id);
			if (P && P->QueueTicket==Q[Head].Ticket) break;
			++Head;
		}
		if (Head==Q.Num()) { Q.Reset(); Head=0; continue; }
		for (int32 I=Head; I<Q.Num() && Ids.Num()<MaxRecords; ++I)
		{
			const auto* P=Pending.Find(Q[I].Id);
			if (!P || P->QueueTicket!=Q[I].Ticket) continue;
			if (bUrgentOnly && PriorityIndex==2 && Now-P->FirstWaitingTime<1.0) break;
			Ids.Add(Q[I].Id);
		}
	}
	for (auto Id : Ids)
	{
		if (Out.Records.Num() >= MaxRecords) break;
		const FPending& P = Pending.FindChecked(Id);
		const FValue* Baseline = Published.Find(Id);
		const uint16 Mask = Delta(P.Value, Baseline);
		// A terminal death is encoded before final removal, even if no alive state was ever delivered.
		if (Mask && (!P.bRemove || !P.Value.State.IsAlive()))
		{
			FRecord R; R.Value = P.Value; R.Mask = Mask; Out.Records.Add(MoveTemp(R)); Encode(Out);
			if (Out.Block.Data.Num() + 2 > MaxBytes) { Out.Records.Pop(); Encode(Out); Out.bBudgetLimited = true; continue; }
		}
		if (P.bRemove)
		{
			if (Out.Records.Num() >= MaxRecords) continue; // Keep the removal pending after a terminal death filled the batch.
			FRecord R; R.Value.State.SoldierId = Id; R.bRemove = true; Out.Records.Add(MoveTemp(R)); Encode(Out);
			if (Out.Block.Data.Num() + 2 > MaxBytes) { Out.Records.Pop(); Encode(Out); Out.bBudgetLimited = true; continue; }
		}
		Out.CompletedIds.Add(Id);
	}
	return !Out.Records.IsEmpty();
}

void FSender::Commit(const FPrepared& Batch)
{
	check(Batch.Header.Sequence == Next(LastSequence) && InFlight.Num() < MaxInFlight);
	LastSequence = Batch.Header.Sequence; bStarted = true;
	FInFlight Flight; Flight.Sequence = LastSequence;
	for (const auto& R : Batch.Records)
	{
		const auto Id = R.Value.State.SoldierId;
		RequiredBaseline.Remove(Id);
		if (R.bRemove) { Published.Remove(Id); Flight.Removed.Add(Id); }
		else
		{
			FValue V = R.Value;
			const auto* Previous = Published.Find(Id);
			const bool bPoseBarrier = !Previous || Previous->State.ActiveOrderId != V.State.ActiveOrderId
				|| Previous->State.DisplacementFrameFloor != V.State.DisplacementFrameFloor
				|| Previous->State.IsAlive() != V.State.IsAlive()
				|| Previous->State.bPhased != V.State.bPhased
				|| Previous->State.bExternalActionsLocked != V.State.bExternalActionsLocked;
			// Health-only changes must not repeatedly close the pose gate while fighting.
			V.PublishedSequence = bPoseBarrier ? LastSequence : Previous->PublishedSequence;
			Published.Add(Id, MoveTemp(V));
		}
	}
	for (auto Id : Batch.CompletedIds) Pending.Remove(Id);
	if (Batch.Header.bComplete)
	{
		CompleteSequence = LastSequence; CompleteCount = Batch.Header.RosterCount; CompleteHash = Batch.Header.IdHash;
	}
	InFlight.Add(MoveTemp(Flight));
}

bool FSender::Confirm(uint32 Sequence)
{
	if (!Sequence || Newer(Sequence, LastSequence) || (AckedSequence && Newer(AckedSequence, Sequence))) return false;
	AckedSequence = Sequence;
	InFlight.RemoveAll([Sequence](const auto& Flight) { return !Newer(Flight.Sequence, Sequence); });
	return true;
}

double FSender::OldestWait(double Now) const
{
	double Age = 0; for (const auto& P : Pending) Age = FMath::Max(Age, Now - P.Value.FirstWaitingTime); return Age;
}

bool FReceiver::Apply(const FGuLiEncodedStateBatch& Block, TArray<FRecord>& OutRecords)
{
	OutRecords.Reset(); FHeader Header;
	if (!ReadHeader(Block, Header)) return false;
	if (Header.ConnectionGeneration != Session.ConnectionGeneration || Header.Generation != Session.Generation || Header.Epoch != Session.Epoch)
		return false;
	if (Header.Sequence != Next(AppliedSequence) || Header.bStart != !bStarted) return false;
	FMemoryReader Reader(Block.Data); uint16 Count = 0; SerializeHeader(Reader, Header, Count);
	if (Count > MaxRecords || ((Header.bStart || Header.bComplete) && Count != 0)) return false;
	TMap<FGuLiSoldierId, FValue> Staged;
	TSet<FGuLiSoldierId> Removed;
	for (uint16 Index = 0; Index < Count; ++Index)
	{
		const int64 Offset = Reader.Tell(); uint32 IdValue = 0; uint16 Mask = 0;
		Reader.SerializeIntPacked(IdValue); Reader << Mask;
		if (Reader.IsError()) return false;
		Reader.Seek(Offset);
		const auto Id=FGuLiSoldierId(IdValue);
		const auto* Previous = Staged.Find(Id);
		if (!Previous && !Header.bStart && !Removed.Contains(Id)) Previous=Values.Find(Id);
		if (!Previous && Mask != All && Mask != Remove) return false;
		FRecord R; if (Previous) R.Value = *Previous;
		SerializeRecord(Reader, R);
		if (Reader.IsError() || (!R.bRemove && !Valid(R.Value))) return false;
		if (R.bRemove) { Staged.Remove(R.Value.State.SoldierId); Removed.Add(R.Value.State.SoldierId); }
		else { Removed.Remove(R.Value.State.SoldierId); R.Value.PublishedSequence = Header.Sequence; Staged.Add(R.Value.State.SoldierId, R.Value); }
		OutRecords.Add(MoveTemp(R));
	}
	if (Reader.IsError() || Reader.Tell() != Block.Data.Num()) return false;
	if (Header.bComplete && (CompleteSequence || Header.RosterCount != uint32(Values.Num()) || Header.IdHash != HashIds(Values))) return false;
	if (Header.bStart) Values.Reset();
	for (auto Id : Removed) Values.Remove(Id);
	for (auto& P : Staged) Values.Add(P.Key,MoveTemp(P.Value));
	AppliedSequence = Header.Sequence; bStarted = true;
	if (Header.bComplete) { CompleteSequence = Header.Sequence; CompleteCount = Header.RosterCount; CompleteHash = Header.IdHash; }
	return true;
}
}

bool FGuLiEncodedStateBatch::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	uint16 Size = uint16(Data.Num());
	if (Ar.IsSaving() && (Data.IsEmpty() || Data.Num() > GuLiCommanderStateStream::MaxWireBytes - 2))
	{
		Ar.SetError(); bOutSuccess = false; return true;
	}
	Ar.SerializeBits(&Size, 10);
	if (Size == 0 || Size > GuLiCommanderStateStream::MaxWireBytes - 2) { Ar.SetError(); bOutSuccess = false; return true; }
	if (Ar.IsLoading()) Data.SetNumUninitialized(Size);
	Ar.Serialize(Data.GetData(), Size); bOutSuccess = !Ar.IsError(); return true;
}
