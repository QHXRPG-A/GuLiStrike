#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiHybridObjectKey.h"
#include "GuLiNetMotionDelivery.h"
#include <memory>

struct FGuLiHybridObjectState;
struct FGuLiStrongholdTransitState;
class UWorld;

struct FGuLiVehiclePose
{
    FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector;
    FQuat Rotation=FQuat::Identity;
    TArray<uint8> Transit,Work;
    bool SetTransit(FGuLiStrongholdTransitState State);
    bool GetTransit(FGuLiStrongholdTransitState& Out) const;
};

// One stream per vehicle. An envelope tick identifies the exact reconstructed
// snapshot; a delta can reference only a snapshot its own recipient confirmed.
class FGuLiVehiclePoseStream
{
public:
    enum class ERead { Ready, MissingBaseline, Invalid };
    bool Publish(UWorld& World,const FGuLiHybridObjectState& Object,uint64 At,const FGuLiVehiclePose& Pose,bool WorkLease);
    ERead Read(UWorld& World,const FGuLiHybridObjectState& Object,uint64 At,TConstArrayView<uint8> Bytes,FGuLiVehiclePose& Out);
    void Applied(UWorld& World,const FGuLiHybridObjectState& Object,uint64 At,FGuLiVehiclePose Pose);
    bool Feedback(UWorld& World,const FGuLiHybridObjectState& Object,const FGuid& Peer,TConstArrayView<uint8> Bytes);
private:
    struct FEntry
    {
        uint64 At=0;
        TSharedPtr<const FGuLiVehiclePose> Pose;
        std::shared_ptr<GuLiNet::MotionDelivery> Delivery;
    };
    struct FStream
    {
        FGuLiHybridObjectKey Key;
        FGuid Connection;
        uint64 Owner=0,Control=0,Epoch=0,Latest=0,Confirmed=0,FirstOfState=0;
        TArray<FEntry> History;
        int32 Next=0;
        bool ForceFull=false;
        void Bind(const FGuLiHybridObjectState& Object,const FGuid& Pair,uint64 MatchEpoch);
        const FEntry* Find(uint64 At) const;
        void Store(FEntry Entry);
    };
    TMap<FGuid,FStream> Senders;
    FStream Receiver;
};
