// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanQAGameMode.h"

#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "GuLiStrike.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanQAGameMode)

namespace GuLiWingmanQAFixture
{
	constexpr int32 RoleSlotCount = 10;
	constexpr float MinimumAirStartSeparation = 180000.0f;

	FTransform TransformForSlot(const uint8 SlotIndex)
	{
		// Slots are the production 5v5 directory: Red 0..4, Blue 5..9.
		// Air slots sit high enough for the authored 15 m vertical formation offset and
		// remain more than one 90 m outer-ring radius from each other and the nav bounds.
		switch (SlotIndex)
		{
		case 0: return FTransform(FRotator(-35.0, 0.0, 0.0), FVector(-30000.0, 0.0, 35000.0));
		case 1: return FTransform(FRotator::ZeroRotator, FVector(-80000.0, -30000.0, 26000.0));
		case 2: return FTransform(FRotator::ZeroRotator, FVector(-80000.0, 30000.0, 26000.0));
		case 3: return FTransform(FRotator(0.0, 0.0, 0.0), FVector(-220000.0, -150000.0, 34000.0));
		case 4: return FTransform(FRotator(0.0, 0.0, 0.0), FVector(-220000.0, 150000.0, 34000.0));
		case 5: return FTransform(FRotator(-35.0, 180.0, 0.0), FVector(30000.0, 0.0, 35000.0));
		case 6: return FTransform(FRotator::ZeroRotator, FVector(80000.0, -30000.0, 26000.0));
		case 7: return FTransform(FRotator::ZeroRotator, FVector(80000.0, 30000.0, 26000.0));
		case 8: return FTransform(FRotator(0.0, 180.0, 0.0), FVector(220000.0, -150000.0, 34000.0));
		case 9: return FTransform(FRotator(0.0, 180.0, 0.0), FVector(220000.0, 150000.0, 34000.0));
		default: return FTransform::Identity;
		}
	}

	bool IsFormationNavigable(
		const UGuLiFlightNavigationSubsystem& Navigation,
		const FTransform& CarrierTransform)
	{
		const FGuLiWingmanFormationRuntimeConfig Formation;
		const FVector CarrierLocation = CarrierTransform.GetLocation();
		if (Navigation.ValidateAuthoritativeSegment(
			CarrierLocation, CarrierLocation, Formation.AgentRadiusCentimeters)
			!= EGuLiFlightNavSegmentStatus::Valid)
		{
			return false;
		}

		for (int32 MemberIndex = 0; MemberIndex < GULI_WINGMAN_GROUP_SIZE; ++MemberIndex)
		{
			const int32 InnerRingSlots = static_cast<int32>(Formation.InnerRingSlots);
			const bool bInnerRing = MemberIndex < InnerRingSlots;
			const int32 RingIndex = bInnerRing ? MemberIndex : MemberIndex - InnerRingSlots;
			const int32 RingCount = bInnerRing
				? InnerRingSlots : static_cast<int32>(Formation.OuterRingSlots);
			const float Radius = bInnerRing
				? Formation.InnerRingRadiusCentimeters : Formation.OuterRingRadiusCentimeters;
			const float Height = bInnerRing
				? Formation.InnerRingHeightCentimeters : Formation.OuterRingHeightCentimeters;
			const float Phase = static_cast<float>(RingIndex) * UE_TWO_PI
				/ static_cast<float>(RingCount);
			const FVector Position = CarrierTransform.TransformPositionNoScale(FVector(
				Radius * FMath::Cos(Phase), Radius * FMath::Sin(Phase), Height));
			if (Navigation.ValidateEndpointsInSameComponent(
				CarrierLocation, Position, Formation.AgentRadiusCentimeters)
				!= EGuLiFlightNavSegmentStatus::Valid)
			{
				return false;
			}
		}
		return true;
	}
}

AGuLiWingmanQAGameMode::AGuLiWingmanQAGameMode()
{
	// First four real connections become the two Air slots on each team. Backup and
	// observer roles can then be added without accepting a client-supplied identity.
	InitialRolePriority = {
		EGuLiCommanderRole::Air,
		EGuLiCommanderRole::Commander,
		EGuLiCommanderRole::Ground
	};
}

void AGuLiWingmanQAGameMode::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanQA")))
	{
		// FlightNav volumes finish registration during actor BeginPlay. The next tick
		// still precedes a localhost login, while exposing the immutable runtime graph.
		GetWorldTimerManager().SetTimerForNextTick(
			this, &AGuLiWingmanQAGameMode::CreateDeterministicPlayerStarts);
	}
#endif
}

AActor* AGuLiWingmanQAGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
#if !UE_BUILD_SHIPPING
	if (AActor* Start = FindDeterministicPlayerStart(Player))
	{
		return Start;
	}
#endif
	return Super::ChoosePlayerStart_Implementation(Player);
}

AActor* AGuLiWingmanQAGameMode::FindPlayerStart_Implementation(
	AController* Player,
	const FString& IncomingName)
{
#if !UE_BUILD_SHIPPING
	// GenericPlayerInitialization assigns the authoritative role before RestartPlayer.
	// Bypass UE's earlier cached StartSpot so the role slot always owns its exact fixture start.
	if (AActor* Start = FindDeterministicPlayerStart(Player))
	{
		return Start;
	}
#endif
	return Super::FindPlayerStart_Implementation(Player, IncomingName);
}

AActor* AGuLiWingmanQAGameMode::FindDeterministicPlayerStart(AController* Player) const
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("GuLiWingmanQA")))
	{
		return nullptr;
	}
	const AGuLiBattlePlayerState* PlayerState = Player
		? Player->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const uint8 SlotIndex = PlayerState ? PlayerState->GetBattleSlotIndex()
		: AGuLiBattlePlayerState::InvalidSlotIndex;
	if (QAPlayerStarts.IsValidIndex(SlotIndex) && IsValid(QAPlayerStarts[SlotIndex]))
	{
		UE_LOG(LogGuLiWingmanQA, Display,
			TEXT("Wingman QA PlayerStart chosen: slot=%u location=%s rotation=%s"),
			SlotIndex,
			*QAPlayerStarts[SlotIndex]->GetActorLocation().ToCompactString(),
			*QAPlayerStarts[SlotIndex]->GetActorRotation().ToCompactString());
		return QAPlayerStarts[SlotIndex];
	}
#endif
	return nullptr;
}

void AGuLiWingmanQAGameMode::CreateDeterministicPlayerStarts()
{
	if (!HasAuthority() || !GetWorld() || QAPlayerStarts.Num() > 0)
	{
		return;
	}
	TArray<FTransform> SlotTransforms;
	SlotTransforms.Reserve(GuLiWingmanQAFixture::RoleSlotCount);
	for (uint8 SlotIndex = 0; SlotIndex < GuLiWingmanQAFixture::RoleSlotCount; ++SlotIndex)
	{
		SlotTransforms.Add(GuLiWingmanQAFixture::TransformForSlot(SlotIndex));
	}
	if (!SelectNavigableAirStarts(SlotTransforms))
	{
		UE_LOG(LogGuLiWingmanQA, Error,
			TEXT("Wingman QA could not select four complete-formation FlightNav starts."));
	}

	QAPlayerStarts.SetNum(GuLiWingmanQAFixture::RoleSlotCount);
	for (uint8 SlotIndex = 0; SlotIndex < GuLiWingmanQAFixture::RoleSlotCount; ++SlotIndex)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = FName(*FString::Printf(TEXT("WingmanQAPlayerStart_%u"), SlotIndex));
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APlayerStart* PlayerStart = GetWorld()->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(), SlotTransforms[SlotIndex], Parameters);
		if (PlayerStart)
		{
			PlayerStart->PlayerStartTag = FName(*FString::Printf(TEXT("WingmanQASlot%u"), SlotIndex));
			PlayerStart->Tags.AddUnique(TEXT("Wingman.QA.Invasion"));
			QAPlayerStarts[SlotIndex] = PlayerStart;
		}
	}
}

bool AGuLiWingmanQAGameMode::SelectNavigableAirStarts(
	TArray<FTransform>& InOutSlotTransforms) const
{
	const UWorld* World = GetWorld();
	const UGuLiFlightNavigationSubsystem* Navigation = World
		? World->GetSubsystem<UGuLiFlightNavigationSubsystem>() : nullptr;
	if (!Navigation || InOutSlotTransforms.Num() != GuLiWingmanQAFixture::RoleSlotCount)
	{
		return false;
	}

	struct FDesiredAirStart
	{
		uint8 SlotIndex;
		FVector DesiredLocation;
		float YawDegrees;
	};
	const FDesiredAirStart DesiredStarts[] = {
		{3u, FVector(-260000.0, -260000.0, 0.0), 0.0f},
		{8u, FVector(260000.0, 260000.0, 0.0), 180.0f},
		{4u, FVector(-260000.0, 260000.0, 0.0), 0.0f},
		{9u, FVector(260000.0, -260000.0, 0.0), 180.0f}
	};
	const float CandidateCoordinates[] = {
		-320000.0f, -240000.0f, -160000.0f, -80000.0f, 0.0f,
		80000.0f, 160000.0f, 240000.0f, 320000.0f
	};
	const float CandidateHeights[] = {-10000.0f, 0.0f, 10000.0f, 20000.0f, 30000.0f};
	TArray<FVector> SelectedLocations;
	SelectedLocations.Reserve(UE_ARRAY_COUNT(DesiredStarts));

	for (const FDesiredAirStart& Desired : DesiredStarts)
	{
		bool bFound = false;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		FTransform BestTransform;
		for (const float Z : CandidateHeights)
		{
			for (const float X : CandidateCoordinates)
			{
				for (const float Y : CandidateCoordinates)
				{
					const FVector Candidate(X, Y, Z);
					bool bSeparated = true;
					for (const FVector& Selected : SelectedLocations)
					{
						bSeparated &= FVector::DistSquared2D(Candidate, Selected)
							>= FMath::Square(GuLiWingmanQAFixture::MinimumAirStartSeparation);
					}
					if (!bSeparated)
					{
						continue;
					}

					const FTransform CandidateTransform(
						FRotator(0.0f, Desired.YawDegrees, 0.0f), Candidate);
					if (!GuLiWingmanQAFixture::IsFormationNavigable(*Navigation, CandidateTransform))
					{
						continue;
					}
					const double DistanceSquared = FVector::DistSquared(Candidate, Desired.DesiredLocation);
					if (!bFound || DistanceSquared < BestDistanceSquared)
					{
						bFound = true;
						BestDistanceSquared = DistanceSquared;
						BestTransform = CandidateTransform;
					}
				}
			}
		}
		if (!bFound)
		{
			return false;
		}
		InOutSlotTransforms[Desired.SlotIndex] = BestTransform;
		SelectedLocations.Add(BestTransform.GetLocation());
		UE_LOG(LogGuLiWingmanQA, Display,
			TEXT("Wingman QA FlightNav start selected: slot=%u location=%s yaw=%.1f"),
			Desired.SlotIndex, *BestTransform.GetLocation().ToCompactString(), Desired.YawDegrees);
	}
	return true;
}
