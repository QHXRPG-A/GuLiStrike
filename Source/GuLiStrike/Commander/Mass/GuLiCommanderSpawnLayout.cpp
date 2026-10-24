// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderSpawnLayout.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Engine/World.h"

bool UGuLiBattleAuthoritySubsystem::BuildInitialArmySpawnLayout(
	const TArray<FGuLiSoldierDefinition>& Definitions,
	const FVector& RedAssembly, const FVector& BlueAssembly,
	TArray<FGuLiCommanderInitialSpawnSlot>& OutSlots, FString& OutError,
	const float InwardInsetCentimeters, const int32 Population,UWorld* AuthoringWorld) const
{
	OutSlots.Reset();
	OutError.Reset();
	if (Population<2 || Population>10000 || Population%2!=0
		|| Definitions.IsEmpty() || RedAssembly.ContainsNaN() || BlueAssembly.ContainsNaN()
		|| !FMath::IsFinite(InwardInsetCentimeters) || InwardInsetCentimeters < 0.0f
		|| FVector::DistSquared2D(RedAssembly, BlueAssembly) < 1.0
		|| !FMath::IsFinite(GroupSpacingCentimeters) || GroupSpacingCentimeters <= 0.0f
		|| !FMath::IsFinite(MemberSpacingCentimeters) || MemberSpacingCentimeters <= 0.0f
		|| !FMath::IsFinite(MemberAgentRadiusCentimeters) || MemberAgentRadiusCentimeters <= 0.0f)
	{
		OutError = TEXT("Initial army requires valid Mass definitions, distinct anchors and positive spacing/radius.");
		return false;
	}
	constexpr int32 Columns = 5;
    auto* World=AuthoringWorld?AuthoringWorld:GetWorld();
    const auto GridData=World?GuLiGrid::AuthoredSnapshot(*World):nullptr;
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
	const int32 MembersPerTeam=Population/2;
	const int32 FormationCount=FMath::DivideAndRoundUp(MembersPerTeam,GuLiCommanderInitialSpawn::MembersPerFormation);
	OutSlots.Reserve(Population);
	for (const EGuLiTeam Team : { EGuLiTeam::Red, EGuLiTeam::Blue })
	{
		const FVector Center = Team == EGuLiTeam::Red ? RedAssembly : BlueAssembly;
		const FVector Opposing = Team == EGuLiTeam::Red ? BlueAssembly : RedAssembly;
		const FVector Forward = (Opposing - Center).GetSafeNormal2D();
		const FVector Right(-Forward.Y, Forward.X, 0.0);
		for (int32 Formation = 0; Formation < FormationCount; ++Formation)
		{
			const auto& Definition = Definitions[Formation % FMath::Min(2, Definitions.Num())];
			const float Radius = Definition.GetMassAvoidanceRadius(MemberAgentRadiusCentimeters);
			const float Spacing = FMath::Max(MemberSpacingCentimeters, Radius * 2.0f + 20.0f);
			// Both ranks extend into the battlefield from assembly. A centered grid put the
			// rear War Machine rank inside the home outpost/facilities on the 300 m board.
			const FVector Anchor = Center + Right * ((Formation % Columns - 2) * GroupSpacing)
				+ Forward * ((Formation / Columns + 0.5f) * GroupSpacing + InwardInsetCentimeters);
			const float Yaw = (Opposing - Anchor).GetSafeNormal2D().Rotation().Yaw;
			const int32 Members=FMath::Min(GuLiCommanderInitialSpawn::MembersPerFormation,
				MembersPerTeam-Formation*GuLiCommanderInitialSpawn::MembersPerFormation);
			for (int32 Member = 0; Member < Members; ++Member)
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
                if (GridData)
                {
                    const auto A=GridData->AnchorAt(Anchor,Definition.FootprintCells);
                    Slot.Location=GridData->Center(A+FIntPoint(Member%5,Member/5)*Definition.FootprintCells,Definition.FootprintCells);
                }
			}
		}
	}
	return true;
}
