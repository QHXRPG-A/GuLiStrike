#pragma once
#include "CoreMinimal.h"
class UGuLiGroundMechWeaponComponent;
struct FGuLiHybridManifestData;
namespace GuLiHybridGroundUpgrade
{
inline constexpr const TCHAR* Domain=TEXT("ground_weapon");
bool Append(const UGuLiGroundMechWeaponComponent& Weapon,FGuLiHybridManifestData& Data,FString& Error);
}
