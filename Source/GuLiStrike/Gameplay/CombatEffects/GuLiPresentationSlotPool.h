#pragma once
#include "CoreMinimal.h"

/** Local-only handles. Never serialized or substituted for a network effect identity. */
struct FGuLiPresentationSlotHandle
{
	int32 Index = INDEX_NONE;
	uint32 Generation = 0, Epoch = 0;
};

/** Separate simulation heartbeats from changed GPU/CPU payloads, and publish a revision at most once per frame. */
struct FGuLiPresentationUploadStamp
{
	uint64 Frame=MAX_uint64, Revision=0;
	bool NeedsUpload(uint64 CurrentFrame,uint64 CurrentRevision,bool bForce=false) const
	{ return Frame!=CurrentFrame && (bForce || Revision!=CurrentRevision); }
	void Commit(uint64 CurrentFrame,uint64 CurrentRevision) { Frame=CurrentFrame; Revision=CurrentRevision; }
	void Reset() { Frame=MAX_uint64; Revision=0; }
};

/** Shared bookkeeping for cosmetic pools; payloads and assets belong to their consumer. */
class FGuLiPresentationSlotPool
{
public:
	explicit FGuLiPresentationSlotPool(int32 Initial=512, int32 Growth=256) : InitialSize(Initial), GrowthSize(Growth) {}
	FGuLiPresentationSlotHandle Acquire()
	{
		if (Free.IsEmpty()) Grow(Generations.IsEmpty() ? InitialSize : GrowthSize);
		const int32 Index=Free.Pop(EAllowShrinking::No);
		ActivePosition[Index]=Active.Num(); Active.Add(Index);
		++Revision;
		return {Index,Generations[Index],Epoch};
	}
	bool IsLive(FGuLiPresentationSlotHandle Handle) const
	{
		return Handle.Epoch==Epoch && Generations.IsValidIndex(Handle.Index)
			&& Generations[Handle.Index]==Handle.Generation && ActivePosition[Handle.Index]!=INDEX_NONE;
	}
	bool Release(FGuLiPresentationSlotHandle Handle)
	{
		if (!IsLive(Handle)) return false;
		const int32 Position=ActivePosition[Handle.Index], Last=Active.Last();
		Active.RemoveAtSwap(Position,1,EAllowShrinking::No);
		if (Active.IsValidIndex(Position)) ActivePosition[Last]=Position;
		ActivePosition[Handle.Index]=INDEX_NONE;
		if (++Generations[Handle.Index]==0) ++Generations[Handle.Index];
		Free.Add(Handle.Index); ++Revision;
		return true;
	}
	void Reset()
	{
		if (++Epoch==0) ++Epoch;
		Active.Reset(); Free.Reset();
		for (int32 I=Generations.Num()-1;I>=0;--I)
		{
			// Previous-frame channel rows carry slot/generation, not the CPU epoch.
			// Invalidate their identity as well as outstanding CPU handles.
			if (++Generations[I]==0) ++Generations[I];
			ActivePosition[I]=INDEX_NONE; Free.Add(I);
		}
		++Revision;
	}
	int32 Capacity() const { return Generations.Num(); }
	TConstArrayView<int32> ActiveIndices() const { return Active; }
	TArray<int32> ActiveGenerations() const
	{
		TArray<int32> Result; Result.Init(0,Capacity());
		for (const int32 I:Active) Result[I]=int32(Generations[I]);
		return Result;
	}
	uint64 GetRevision() const { return Revision; }
	void MarkPayloadChanged() { ++Revision; }
private:
	void Grow(int32 Count)
	{
		const int32 Begin=Generations.Num(); Generations.AddZeroed(Count); ActivePosition.AddUninitialized(Count);
		for (int32 I=Begin+Count-1;I>=Begin;--I) { Generations[I]=1; ActivePosition[I]=INDEX_NONE; Free.Add(I); }
		++Revision;
	}
	TArray<uint32> Generations;
	TArray<int32> ActivePosition, Active, Free;
	int32 InitialSize=512, GrowthSize=256;
	uint32 Epoch=1;
	uint64 Revision=1;
};
