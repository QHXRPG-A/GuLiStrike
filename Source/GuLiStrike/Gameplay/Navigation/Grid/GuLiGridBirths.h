#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiHybridObjectKey.h"

class UWorld;
class UGuLiGroundGridSubsystem;
class FGuLiHybridStateArchive;
struct FGuLiHybridObjectState;
enum class EGuLiGridPermit : uint8;

/** A complete summon batch is prepared once, then retained through the existing transaction. */
struct FGuLiGridBirthBatch
{
    FGuid Request,Event;
    FGuLiHybridObjectKey Source;
    int32 Size=1,Count=0,CasterSize=1,Gap=0,Rings=0,Samples=0,Cursor=0;
    FIntPoint Origin=FIntPoint::ZeroValue;
    TArray<FVector> Positions;
    int32 CheckCursor=0;
    uint64 CheckGeneration=0;
    bool bAwaitingGrant=false;
    bool bSubmitted=false;
};
namespace GuLiGridBirths
{
FGuid EventId(const FGuid& Request,const FGuLiHybridObjectKey& Source);
FGuLiHybridObjectKey Target(const FGuLiGridBirthBatch& Batch,int32 Index);
EGuLiGridPermit Prepare(UGuLiGroundGridSubsystem& Grid,const FGuLiHybridObjectState& Source,const FGuid& Request,
    FVector Position,int32 CasterSize,int32 Size,int32 Count,double Clearance,int32 Rings,int32 Samples);
void Cancel(UGuLiGroundGridSubsystem& Grid,const FGuLiHybridObjectKey& Source,const FGuid& Request,bool bDetach=false);
void Recover(UGuLiGroundGridSubsystem& Grid,const FGuLiHybridObjectState& Source);
void Advance(UGuLiGroundGridSubsystem& Grid);
void Serialize(FGuLiHybridStateArchive& Archive,FGuLiGridBirthBatch& Batch);
}
