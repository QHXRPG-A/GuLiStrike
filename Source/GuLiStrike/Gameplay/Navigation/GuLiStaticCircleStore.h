#pragma once
#include "Gameplay/Navigation/GuLiCircleAvoidance.h"

namespace GuLiCircleAvoidance
{
/** World-owned static groups: one building or original ore cluster per update. */
class FStaticCircleStore
{
public:
    bool Replace(uint64 Source,TConstArrayView<FCircle> Circles,FBox& ChangedBounds);
    void Remove(uint64 Source,FBox& ChangedBounds);
    void Reset();
    const FIndex& GetIndex() const {return Index;}
    uint64 GetRevision() const {return Revision;}
    SIZE_T GetAllocatedSize() const;
private:
    FIndex Index;
    TMap<uint64,TArray<uint64>> Groups;
    TMap<uint64,uint64> Owners;
    uint64 Revision=0;
};
}
