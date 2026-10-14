#include "Gameplay/Capabilities/GuLiLogicalCooldownComponent.h"
#include "GuLiNetCooldown.h"
#include "Network/Private/GuLiNetConversions.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Engine/World.h"

struct UGuLiLogicalCooldownComponent::FImpl {GuLiNet::CooldownBook Book;};
namespace
{
GuLiNet::CooldownKey Key(FName Domain,FName Name) {return {TCHAR_TO_UTF8(*Domain.ToString()),TCHAR_TO_UTF8(*Name.ToString())};}
const FGuLiHybridComponentCapabilityRegistration CooldownCapability({TEXT("Cooldown"),1,
    []()->const UClass* {return UGuLiLogicalCooldownComponent::StaticClass();},
    [](const UActorComponent& C,TArray<uint8>& B) {return CastChecked<UGuLiLogicalCooldownComponent>(&C)->CaptureState(B);},
    [](const UActorComponent& C,TConstArrayView<uint8> B) {return CastChecked<UGuLiLogicalCooldownComponent>(&C)->ValidateState(B);},
    [](UActorComponent& C,TConstArrayView<uint8> B,EGuLiHybridRestoreStage Stage)
    {return Stage!=EGuLiHybridRestoreStage::Link || CastChecked<UGuLiLogicalCooldownComponent>(&C)->RestoreState(B);},nullptr,
    [](UActorComponent& C,uint64 At,double)
    {
        if (!CastChecked<UGuLiLogicalCooldownComponent>(&C)->Advance(At))
            C.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Cooldown logical time or revision exhausted"));
    }});
}
UGuLiLogicalCooldownComponent::UGuLiLogicalCooldownComponent():Runtime(MakeShared<FImpl>())
{PrimaryComponentTick.bCanEverTick=false;}
UGuLiLogicalCooldownComponent::~UGuLiLogicalCooldownComponent()=default;
uint64 UGuLiLogicalCooldownComponent::LogicalTick() const
{
    const auto* H=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    return H && Binding?H->GetObjectLogicalTick(Binding->GetObjectKey()):0;
}
bool UGuLiLogicalCooldownComponent::Reserve(FName Domain,FName Name,FGuid Token,float Seconds)
{
    if (Domain.IsNone() || Name.IsNone() || !UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !FMath::IsFinite(Seconds) || Seconds<=0 || Seconds>30*86400) return false;
    const auto Before=Runtime->Book.Snapshot().Version;
    const auto Result=Runtime->Book.Reserve(Key(Domain,Name),GuLiNetBridge::ToCore(Token),uint64(FMath::CeilToDouble(double(Seconds)*60.)),LogicalTick());
    if (Before!=Runtime->Book.Snapshot().Version) OnChanged.Broadcast();
    return Result==GuLiNet::CooldownDecision::Reserved || Result==GuLiNet::CooldownDecision::Duplicate;
}
bool UGuLiLogicalCooldownComponent::Rollback(FName Domain,FName Name,FGuid Token)
{
    if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !Runtime->Book.Rollback(Key(Domain,Name),GuLiNetBridge::ToCore(Token))) return false;
    OnChanged.Broadcast();return true;
}
bool UGuLiLogicalCooldownComponent::ClearDomain(FName Domain)
{
    if (Domain.IsNone()) return false;
    const auto Before=Runtime->Book.Snapshot().Version;
    if (!Runtime->Book.ClearDomain(TCHAR_TO_UTF8(*Domain.ToString()))) return false;
    if (Before!=Runtime->Book.Snapshot().Version) OnChanged.Broadcast();return true;
}
bool UGuLiLogicalCooldownComponent::IsActive(FName Domain,FName Name) const
{const auto* R=Runtime->Book.Find(Key(Domain,Name));return R && R->ExpiresAt>LogicalTick();}
double UGuLiLogicalCooldownComponent::EndsAtSeconds(FName Domain,FName Name) const
{const auto* R=Runtime->Book.Find(Key(Domain,Name));return R?double(R->ExpiresAt)/60.:0.;}
TArray<FName> UGuLiLogicalCooldownComponent::GetActiveNames(FName Domain) const
{
    TArray<FName> Out;const auto D=std::string(TCHAR_TO_UTF8(*Domain.ToString()));
    for (const auto& [K,R]:Runtime->Book.Snapshot().Active) if (K.Domain==D && R.ExpiresAt>LogicalTick()) Out.Add(FName(UTF8_TO_TCHAR(K.Name.c_str())));
    return Out;
}
bool UGuLiLogicalCooldownComponent::Advance(uint64 At)
{
    const auto Before=Runtime->Book.Snapshot().Version;if (!Runtime->Book.Advance(At)) return false;
    if (Before!=Runtime->Book.Snapshot().Version) OnChanged.Broadcast();return true;
}
bool UGuLiLogicalCooldownComponent::CaptureState(TArray<uint8>& Out) const
{
    const auto B=GuLiNet::EncodeCooldownState(Runtime->Book.Snapshot());if (B.empty()) return false;
    Out.Reset();Out.Append(B.data(),int32(B.size()));return true;
}
bool UGuLiLogicalCooldownComponent::ValidateState(TConstArrayView<uint8> B) const
{GuLiNet::CooldownState S;return GuLiNet::DecodeCooldownState(std::span<const uint8>(B.GetData(),B.Num()),S);}
bool UGuLiLogicalCooldownComponent::RestoreState(TConstArrayView<uint8> B)
{
    GuLiNet::CooldownState S;if (!GuLiNet::DecodeCooldownState(std::span<const uint8>(B.GetData(),B.Num()),S) || !Runtime->Book.Restore(MoveTemp(S))) return false;
    OnChanged.Broadcast();return true;
}
