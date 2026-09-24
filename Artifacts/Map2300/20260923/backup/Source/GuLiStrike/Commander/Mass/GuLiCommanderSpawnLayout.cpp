// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderSpawnLayout.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"

bool UGuLiBattleAuthoritySubsystem::BuildInitialArmySpawnLayout(
	const TArray<FGuLiSoldierDefinition>& Definitions,
	const FVector& RedAssembly, const FVector& BlueAssembly,
	TArray<FGuLiCommanderInitialSpawnSlot>& OutSlots, FString& OutError) const
{
	OutSlots.Reset();
	OutError.Reset();
	if (Definitions.IsEmpty() || RedAssembly.ContainsNaN() || BlueAssembly.ContainsNaN()
		|| FVector::DistSquared2D(RedAssembly, BlueAssembly) < 1.0
		|| !FMath::IsFinite(GroupSpacingCentimeters) || GroupSpacingCentimeters <= 0.0f
		|| !FMath::IsFinite(MemberSpacingCentimeters) || MemberSpacingCentimeters <= 0.0f
		|| !FMath::IsFinite(MemberAgentRadiusCentimeters) || MemberAgentRadiusCentimeters <= 0.0f)
	{
		OutError = TEXT("Initial army requires valid Mass definitions, distinct anchors and positive spacing/radius.");
		return false;
	}
	constexpr int32 Columns = 5;
	float GroupSpacing = GroupSpacingCentimeters;
	for (int32 Index = 0; Index < FMath::Min(2, Definitions.Num()); ++Index)
	{
		const auto& Definition = Definitions[Index];
		if (!Definition.UsesMass() || Definition.UnitTypeId == 0)
		{
			OutError = TEXT("Initial army definitions must be Mass units with valid type IDs.");
			return false;
		}
		GroupSpacing = FMath::Max(GroupSpacing,
			Definition.GetMassAvoidanceRadius(MemberAgentRadiusCentimeters) * 2.0f * Columns * UE_SQRT_2);
	}
	OutSlots.Reserve(GuLiCommanderInitialSpawn::Population);
	for (const EGuLiTeam Team : { EGuLiTeam::Red, EGuLiTeam::Blue })
	{
		const FVector Center = Team == EGuLiTeam::Red ? RedAssembly : BlueAssembly;
		const FVector Opposing = Team == EGuLiTeam::Red ? BlueAssembly : RedAssembly;
		const FVector Forward = (Opposing - Center).GetSafeNormal2D();
		const FVector Right(-Forward.Y, Forward.X, 0.0);
		for (int32 Formation = 0; Formation < GuLiCommanderInitialSpawn::FormationsPerTeam; ++Formation)
		{
			const auto& Definition = Definitions[Formation % FMath::Min(2, Definitions.Num())];
			const float Radius = Definition.GetMassAvoidanceRadius(MemberAgentRadiusCentimeters);
			const float Spacing = FMath::Max(MemberSpacingCentimeters, Radius * 2.0f + 20.0f);
			// Both ranks extend into the battlefield from assembly. A centered grid put the
			// rear War Machine rank inside the home outpost/facilities on the 300 m board.
			const FVector Anchor = Center + Right * ((Formation % Columns - 2) * GroupSpacing)
				+ Forward * ((Formation / Columns + 0.5f) * GroupSpacing);
			const float Yaw = (Opposing - Anchor).GetSafeNormal2D().Rotation().Yaw;
			for (int32 Member = 0; Member < GuLiCommanderInitialSpawn::MembersPerFormation; ++Member)
			{
				auto& Slot = OutSlots.AddDefaulted_GetRef();
				Slot.Team = Team;
				Slot.UnitTypeId = Definition.UnitTypeId;
				Slot.FormationIndex = Formation;
				Slot.SlotIndex = Member;
				Slot.FacingYawDegrees = Yaw;
				Slot.RadiusCentimeters = Radius;
				Slot.Location = Anchor + FRotator(0, Yaw, 0).RotateVector(
					GuLiCommanderNavigationPolicy::MakeFormationSlotOffset(Member, Spacing));
			}
		}
	}
	return true;
}
