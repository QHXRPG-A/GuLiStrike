#include "GuLiFlightNavigationEditorLibrary.h"
#include "GuLiNavigationSourceHash.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationVolume.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "GuLiFlightNavigationEditorLibrary"

namespace
{
	bool IsBakedStaticCollisionSource(
        const UPrimitiveComponent* Component,
        const AGuLiFlightNavigationVolume* Volume,
        const FBox& Bounds,
        const ECollisionChannel Channel)
    {
        return IsValid(Component) && Component->GetOwner() != Volume
            && !Component->IsEditorOnly() && !Component->GetOwner()->IsEditorOnly()
            && Component->IsRegistered() && Component->Mobility == EComponentMobility::Static
            && Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision
            && Component->GetCollisionResponseToChannel(Channel) == ECR_Block
            && Component->Bounds.GetBox().Intersect(Bounds);
    }

    uint64 ComputeSourceGeometrySignatureInternal(
        UWorld* World, const AGuLiFlightNavigationVolume* Volume,
        const FGuLiFlightNavBakeSettings& Settings)
    {
        FGuLiNavigationSourceHash Hash;
        Hash.AddString(TEXT("GuLiFlightSource-v2"));
        const FBox Bounds = Volume->GetComponentsBoundingBox(true);
        const FBox ExpandedBounds = Bounds.ExpandBy(Settings.AgentRadius);
        Hash.AddVector(Bounds.Min);
        Hash.AddVector(Bounds.Max);
        Hash.AddUInt64(Settings.CollisionChannel.GetValue());
        Hash.AddUInt64(Settings.bTraceComplex);
        TArray<UPrimitiveComponent*> Components;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            TInlineComponentArray<UPrimitiveComponent*> ActorComponents;
            It->GetComponents(ActorComponents);
            for (UPrimitiveComponent* Component : ActorComponents)
                if (IsBakedStaticCollisionSource(Component, Volume, ExpandedBounds, Settings.CollisionChannel))
                    Components.Add(Component);
        }
        Components.Sort([](const UPrimitiveComponent& A, const UPrimitiveComponent& B)
        { return A.GetPathName() < B.GetPathName(); });
        Hash.AddUInt64(Components.Num());
        for (const UPrimitiveComponent* Component : Components) Hash.AddCollisionSource(Component);
        return Hash.Get();
    }
}

uint64 UGuLiFlightNavigationEditorLibrary::ComputeSourceGeometrySignature(
	UWorld* World,
	const AGuLiFlightNavigationVolume* Volume,
	const FGuLiFlightNavBakeSettings& Settings)
{
	if (!IsValid(World) || !IsValid(Volume))
	{
		return 0;
	}
	return ComputeSourceGeometrySignatureInternal(World, Volume, Settings);
}

bool UGuLiFlightNavigationEditorLibrary::BakeVolume(
	AGuLiFlightNavigationVolume* Volume,
	const FGuLiFlightNavBakeSettings& Settings,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!IsInGameThread())
	{
		OutError = LOCTEXT("BakeGameThreadOnly", "Flight-navigation baking must run on the game thread.");
		return false;
	}
	if (!IsValid(Volume))
	{
		OutError = LOCTEXT("MissingVolume", "A valid flight-navigation volume is required.");
		return false;
	}
	if (!IsValid(Volume->NavigationData))
	{
		OutError = LOCTEXT("MissingData", "Assign a flight-navigation data asset to the volume before baking.");
		return false;
	}
	if (!Volume->GetActorRotation().IsNearlyZero(0.01f))
	{
		OutError = LOCTEXT("RotatedVolumeUnsupported", "Flight-navigation volumes must remain axis-aligned before baking.");
		return false;
	}

	UWorld* World = Volume->GetWorld();
	if (!IsValid(World))
	{
		OutError = LOCTEXT("MissingWorld", "The volume is not associated with a valid world.");
		return false;
	}
	const FBox WorldBounds = Volume->GetComponentsBoundingBox(true);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiFlightNavigationBake), Settings.bTraceComplex);
	QueryParams.AddIgnoredActor(Volume);
	const auto IsBlocked = [World, Volume, &Settings, QueryParams](const FBox& CandidateBounds)
	{
		const FVector Center = CandidateBounds.GetCenter();
		const FVector Extent = CandidateBounds.GetExtent();
		for (uint8 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector Direction(
				(CornerIndex & 1) != 0 ? 1.0 : -1.0,
				(CornerIndex & 2) != 0 ? 1.0 : -1.0,
				(CornerIndex & 4) != 0 ? 1.0 : -1.0);
			if (!Volume->EncompassesPoint(Center + Direction * Extent, 0.1f))
			{
				return true;
			}
		}

		const FVector ExpandedExtent = CandidateBounds.GetExtent() + FVector(Settings.AgentRadius);
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(
			Overlaps,
			CandidateBounds.GetCenter(),
			FQuat::Identity,
			Settings.CollisionChannel,
			FCollisionShape::MakeBox(ExpandedExtent),
			QueryParams);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const UPrimitiveComponent* Component = Overlap.Component.Get();
			if (IsBakedStaticCollisionSource(Component, Volume,
                CandidateBounds.ExpandBy(Settings.AgentRadius), Settings.CollisionChannel))
			{
				return true;
			}
		}
		return false;
	};

	UGuLiFlightNavigationData* TemporaryData = NewObject<UGuLiFlightNavigationData>();
	FString BakeError;
	const uint32 NextRevision = Volume->NavigationData->Metadata.DefinitionRevision + 1;
	if (!FGuLiFlightNavigationBaker::Build(
		WorldBounds,
		Settings,
		World->GetOutermost()->GetFName(),
		Volume->GetPathName(),
		NextRevision,
		IsBlocked,
		*TemporaryData,
		BakeError))
	{
		OutError = FText::FromString(BakeError);
		return false;
	}

	TemporaryData->Metadata.GeometrySignature = ComputeSourceGeometrySignature(World, Volume, Settings);
	TemporaryData->Metadata.SettingsHash = FGuLiFlightNavigationBaker::ComputeSettingsHash(Settings);
	TemporaryData->Metadata.ContentChecksum = TemporaryData->ComputeContentChecksum();
	FString FinalValidationError;
	if (!TemporaryData->ValidateData(FinalValidationError))
	{
		OutError = FText::FromString(FinalValidationError);
		return false;
	}
	if (!TemporaryData->RebuildSerializedPayload(FinalValidationError)
		|| !TemporaryData->ValidateSerializedPayload(FinalValidationError))
	{
		OutError = FText::FromString(FinalValidationError);
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("BakeTransaction", "Bake Flight Navigation"));
	Volume->Modify();
	Volume->NavigationData->Modify();
	Volume->AuthoringBakeSettings = Settings;
	Volume->NavigationData->Metadata = TemporaryData->Metadata;
	Volume->NavigationData->Nodes = MoveTemp(TemporaryData->Nodes);
	Volume->NavigationData->Cells = MoveTemp(TemporaryData->Cells);
	Volume->NavigationData->Portals = MoveTemp(TemporaryData->Portals);
	Volume->NavigationData->Links = MoveTemp(TemporaryData->Links);
	if (!Volume->NavigationData->RebuildSerializedPayload(FinalValidationError))
	{
		OutError = FText::FromString(FinalValidationError);
		return false;
	}
	Volume->NavigationData->MarkPackageDirty();
	Volume->MarkPackageDirty();
	return true;
}

int32 UGuLiFlightNavigationEditorLibrary::BakeAllVolumes(
	UObject* WorldContextObject,
	const FGuLiFlightNavBakeSettings& Settings,
	TArray<FText>& OutErrors)
{
	OutErrors.Reset();
	if (GEngine == nullptr)
	{
		OutErrors.Add(LOCTEXT("MissingEngine", "The engine is not available."));
		return 0;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		OutErrors.Add(LOCTEXT("MissingWorldContext", "The supplied context does not resolve to a world."));
		return 0;
	}

	int32 SuccessfulBakes = 0;
	for (TActorIterator<AGuLiFlightNavigationVolume> Iterator(World); Iterator; ++Iterator)
	{
		FText Error;
		if (BakeVolume(*Iterator, Settings, Error))
		{
			++SuccessfulBakes;
		}
		else
		{
			OutErrors.Add(FText::Format(
				LOCTEXT("VolumeBakeFailed", "{0}: {1}"),
				FText::FromString(Iterator->GetPathName()),
				Error));
		}
	}
	return SuccessfulBakes;
}

bool UGuLiFlightNavigationEditorLibrary::ValidateNavigationData(
	const UGuLiFlightNavigationData* NavigationData,
	FText& OutError)
{
	if (!IsValid(NavigationData))
	{
		OutError = LOCTEXT("MissingValidationData", "A valid flight-navigation data asset is required.");
		return false;
	}

	FString ValidationError;
	const bool bValid = NavigationData->ValidateData(ValidationError);
	OutError = bValid ? FText::GetEmpty() : FText::FromString(ValidationError);
	return bValid;
}

bool UGuLiFlightNavigationEditorLibrary::ValidateNavigationDataPoint(
	const UGuLiFlightNavigationData* NavigationData,
	const FVector& Point,
	const float AgentRadius,
	int32& OutCellIndex,
	uint8& OutStatus,
	FText& OutError)
{
	OutCellIndex = INDEX_NONE;
	OutStatus = static_cast<uint8>(EGuLiFlightNavSegmentStatus::InvalidData);
	OutError = FText::GetEmpty();
	if (!IsValid(NavigationData)
		|| Point.ContainsNaN()
		|| !FMath::IsFinite(Point.X)
		|| !FMath::IsFinite(Point.Y)
		|| !FMath::IsFinite(Point.Z)
		|| !FMath::IsFinite(AgentRadius)
		|| AgentRadius < 0.0f)
	{
		OutError = LOCTEXT(
			"InvalidNavigationPointProbe",
			"A valid navigation asset, finite point, and non-negative agent radius are required.");
		return false;
	}

	int32 DuplicateEndCell = INDEX_NONE;
	return ValidateNavigationDataEndpoints(
		NavigationData,
		Point,
		Point,
		AgentRadius,
		OutCellIndex,
		DuplicateEndCell,
		OutStatus,
		OutError);
}

bool UGuLiFlightNavigationEditorLibrary::ValidateNavigationDataEndpoints(
	const UGuLiFlightNavigationData* NavigationData,
	const FVector& Start,
	const FVector& End,
	const float AgentRadius,
	int32& OutStartCellIndex,
	int32& OutEndCellIndex,
	uint8& OutStatus,
	FText& OutError)
{
	OutStartCellIndex = INDEX_NONE;
	OutEndCellIndex = INDEX_NONE;
	OutStatus = static_cast<uint8>(EGuLiFlightNavSegmentStatus::InvalidData);
	OutError = FText::GetEmpty();
	const auto IsFiniteVector = [](const FVector& Value)
	{
		return !Value.ContainsNaN()
			&& FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	};
	if (!IsValid(NavigationData)
		|| !IsFiniteVector(Start)
		|| !IsFiniteVector(End)
		|| !FMath::IsFinite(AgentRadius)
		|| AgentRadius < 0.0f)
	{
		OutError = LOCTEXT(
			"InvalidNavigationEndpointProbe",
			"A valid navigation asset, finite endpoints, and non-negative agent radius are required.");
		return false;
	}

	FString GraphError;
	const FGuLiFlightNavigationQuery Query(NavigationData->CreateRuntimeGraph(&GraphError));
	if (!Query.IsValid())
	{
		OutError = FText::FromString(GraphError.IsEmpty()
			? TEXT("The saved flight-navigation graph is invalid.")
			: GraphError);
		return false;
	}

	const EGuLiFlightNavSegmentStatus Status = Query.ValidateEndpointsInSameComponent(
		Start,
		End,
		AgentRadius,
		&OutStartCellIndex,
		&OutEndCellIndex);
	OutStatus = static_cast<uint8>(Status);
	if (Status != EGuLiFlightNavSegmentStatus::Valid)
	{
		OutError = FText::FromString(FString::Printf(
			TEXT("Endpoints are not usable in one saved-graph component (status=%d)."),
			static_cast<int32>(Status)));
		return false;
	}
	return true;
}

bool UGuLiFlightNavigationEditorLibrary::ValidateVolume(
	const AGuLiFlightNavigationVolume* Volume,
	const FGuLiFlightNavBakeSettings& Settings,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!IsInGameThread())
	{
		OutError = LOCTEXT("ValidateGameThreadOnly", "Flight-navigation source validation must run on the game thread.");
		return false;
	}
	if (!IsValid(Volume) || !IsValid(Volume->NavigationData))
	{
		OutError = LOCTEXT("MissingVolumeValidationTarget", "A volume with assigned navigation data is required.");
		return false;
	}

	UWorld* World = Volume->GetWorld();
	if (!IsValid(World))
	{
		OutError = LOCTEXT("MissingValidationWorld", "The volume is not associated with a valid world.");
		return false;
	}
	if (!Volume->GetActorRotation().IsNearlyZero(0.01f))
	{
		OutError = LOCTEXT("StaleVolumeRotation", "Stale flight-navigation bake: the volume is no longer axis-aligned.");
		return false;
	}

	FString ValidationError;
	if (!Volume->NavigationData->ValidateData(ValidationError))
	{
		OutError = FText::FromString(ValidationError);
		return false;
	}

	const FGuLiFlightNavBakeMetadata& Metadata = Volume->NavigationData->Metadata;
	if (Metadata.SourceWorldPackage != World->GetOutermost()->GetFName())
	{
		OutError = LOCTEXT("StaleWorld", "Stale flight-navigation bake: the source world no longer matches.");
		return false;
	}
	if (Metadata.SourceVolumePath != Volume->GetPathName())
	{
		OutError = LOCTEXT("StaleVolume", "Stale flight-navigation bake: the source volume identity no longer matches.");
		return false;
	}

	const FBox CurrentBounds = Volume->GetComponentsBoundingBox(true);
	if (CurrentBounds.IsValid == 0
		|| !CurrentBounds.Min.Equals(Metadata.Bounds.Min, 1.0)
		|| !CurrentBounds.Max.Equals(Metadata.Bounds.Max, 1.0))
	{
		OutError = LOCTEXT("StaleBounds", "Stale flight-navigation bake: the volume bounds changed.");
		return false;
	}
	if (FGuLiFlightNavigationBaker::ComputeSettingsHash(Settings) != Metadata.SettingsHash)
	{
		OutError = LOCTEXT("StaleSettings", "Stale flight-navigation bake: the bake settings changed.");
		return false;
	}
	if (ComputeSourceGeometrySignature(World, Volume, Settings) != Metadata.GeometrySignature)
	{
		OutError = LOCTEXT("StaleGeometry", "Stale flight-navigation bake: static blocking geometry changed.");
		return false;
	}
	return true;
}

bool UGuLiFlightNavigationEditorLibrary::ClearVolume(
	AGuLiFlightNavigationVolume* Volume,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!IsValid(Volume) || !IsValid(Volume->NavigationData))
	{
		OutError = LOCTEXT("MissingClearTarget", "A volume with an assigned navigation data asset is required.");
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("ClearTransaction", "Clear Flight Navigation"));
	Volume->Modify();
	Volume->NavigationData->Modify();
	Volume->NavigationData->ResetBakedData();
	Volume->NavigationData->MarkPackageDirty();
	Volume->MarkPackageDirty();
	return true;
}

#undef LOCTEXT_NAMESPACE
