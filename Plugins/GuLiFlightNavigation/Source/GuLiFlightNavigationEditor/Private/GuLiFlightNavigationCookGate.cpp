#include "GuLiFlightNavigationCookGate.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GuLiFlightNavigationBaker.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationEditorLibrary.h"
#include "GuLiFlightNavigationVolume.h"

namespace
{
	FGuLiFlightNavigationCookIssue MakeIssue(
		const EGuLiFlightNavigationCookIssue Code,
		const UObject* Subject,
		FString Message)
	{
		FGuLiFlightNavigationCookIssue Issue;
		Issue.Code = Code;
		Issue.SubjectPath = IsValid(Subject) ? Subject->GetPathName() : TEXT("<null>");
		Issue.Message = MoveTemp(Message);
		return Issue;
	}
}

FString FGuLiFlightNavigationCookIssue::ToLogString() const
{
	return FString::Printf(
		TEXT("[FLIGHTNAV_COOK_GATE][%s] %s: %s"),
		FGuLiFlightNavigationCookGate::LexToString(Code),
		*SubjectPath,
		*Message);
}

const TCHAR* FGuLiFlightNavigationCookGate::LexToString(const EGuLiFlightNavigationCookIssue Code)
{
	switch (Code)
	{
	case EGuLiFlightNavigationCookIssue::Configuration: return TEXT("Configuration");
	case EGuLiFlightNavigationCookIssue::MissingWorld: return TEXT("MissingWorld");
	case EGuLiFlightNavigationCookIssue::MissingVolume: return TEXT("MissingVolume");
	case EGuLiFlightNavigationCookIssue::MissingData: return TEXT("MissingData");
	case EGuLiFlightNavigationCookIssue::SchemaMismatch: return TEXT("SchemaMismatch");
	case EGuLiFlightNavigationCookIssue::ChecksumMismatch: return TEXT("ChecksumMismatch");
	case EGuLiFlightNavigationCookIssue::InvalidData: return TEXT("InvalidData");
	case EGuLiFlightNavigationCookIssue::StaleSourceWorld: return TEXT("StaleSourceWorld");
	case EGuLiFlightNavigationCookIssue::StaleSourceVolume: return TEXT("StaleSourceVolume");
	case EGuLiFlightNavigationCookIssue::StaleBounds: return TEXT("StaleBounds");
	case EGuLiFlightNavigationCookIssue::StaleSettings: return TEXT("StaleSettings");
	case EGuLiFlightNavigationCookIssue::StaleGeometry: return TEXT("StaleGeometry");
	default: return TEXT("Unknown");
	}
}

bool FGuLiFlightNavigationCookGate::IsWorldRequired(
	const FName WorldPackage,
	const TConstArrayView<FName> RequiredWorldPackages)
{
	return RequiredWorldPackages.Contains(WorldPackage);
}

bool FGuLiFlightNavigationCookGate::ValidateData(
	const UGuLiFlightNavigationData* NavigationData,
	FGuLiFlightNavigationCookIssue& OutIssue)
{
	if (!IsValid(NavigationData))
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::MissingData,
			NavigationData,
			TEXT("Assign a Flight Navigation data asset and run the explicit Bake command."));
		return false;
	}

	if (NavigationData->Metadata.FormatVersion != GuLiFlightNavigation::CurrentDataFormatVersion)
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::SchemaMismatch,
			NavigationData,
			FString::Printf(
				TEXT("Serialized schema %u does not match runtime schema %u; explicitly re-bake this map."),
				NavigationData->Metadata.FormatVersion,
				GuLiFlightNavigation::CurrentDataFormatVersion));
		return false;
	}

	FString ValidationError;
	if (!NavigationData->ValidateData(ValidationError))
	{
		const bool bChecksumMismatch = ValidationError.Contains(TEXT("checksum"), ESearchCase::IgnoreCase);
		OutIssue = MakeIssue(
			bChecksumMismatch
				? EGuLiFlightNavigationCookIssue::ChecksumMismatch
				: EGuLiFlightNavigationCookIssue::InvalidData,
			NavigationData,
			ValidationError);
		return false;
	}

	return true;
}

bool FGuLiFlightNavigationCookGate::ValidateVolume(
	const AGuLiFlightNavigationVolume* Volume,
	FGuLiFlightNavigationCookIssue& OutIssue)
{
	if (!IsValid(Volume))
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::MissingVolume,
			Volume,
			TEXT("A valid enabled Flight Navigation volume is required."));
		return false;
	}

	if (!IsValid(Volume->NavigationData))
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::MissingData,
			Volume,
			TEXT("The enabled volume has no assigned Flight Navigation data asset."));
		return false;
	}

	if (!ValidateData(Volume->NavigationData, OutIssue))
	{
		return false;
	}

	UWorld* World = Volume->GetWorld();
	if (!IsValid(World))
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::MissingWorld,
			Volume,
			TEXT("The volume is not associated with a loaded source world."));
		return false;
	}

	if (!Volume->GetActorRotation().IsNearlyZero(0.01f))
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::StaleBounds,
			Volume,
			TEXT("The source volume is no longer axis-aligned; restore it or explicitly re-bake."));
		return false;
	}

	const FGuLiFlightNavBakeMetadata& Metadata = Volume->NavigationData->Metadata;
	if (Metadata.SourceWorldPackage != World->GetOutermost()->GetFName())
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::StaleSourceWorld,
			Volume,
			FString::Printf(
				TEXT("Baked source world '%s' does not match loaded world '%s'; explicitly re-bake."),
				*Metadata.SourceWorldPackage.ToString(),
				*World->GetOutermost()->GetName()));
		return false;
	}

	if (Metadata.SourceVolumePath != Volume->GetPathName())
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::StaleSourceVolume,
			Volume,
			TEXT("Baked source volume identity no longer matches this actor; explicitly re-bake."));
		return false;
	}

	const FBox CurrentBounds = Volume->GetComponentsBoundingBox(true);
	if (CurrentBounds.IsValid == 0
		|| !CurrentBounds.Min.Equals(Metadata.Bounds.Min, 1.0)
		|| !CurrentBounds.Max.Equals(Metadata.Bounds.Max, 1.0))
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::StaleBounds,
			Volume,
			TEXT("Flight volume bounds changed after Bake; explicitly re-bake."));
		return false;
	}

	if (FGuLiFlightNavigationBaker::ComputeSettingsHash(Volume->AuthoringBakeSettings) != Metadata.SettingsHash)
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::StaleSettings,
			Volume,
			TEXT("Authoring bake settings changed after Bake; explicitly re-bake."));
		return false;
	}

	const uint64 CurrentGeometrySignature =
		UGuLiFlightNavigationEditorLibrary::ComputeSourceGeometrySignature(
			World,
			Volume,
			Volume->AuthoringBakeSettings);
	if (CurrentGeometrySignature == 0 || CurrentGeometrySignature != Metadata.GeometrySignature)
	{
		OutIssue = MakeIssue(
			EGuLiFlightNavigationCookIssue::StaleGeometry,
			Volume,
			TEXT("Static blocking geometry changed after Bake; explicitly re-bake."));
		return false;
	}

	return true;
}

bool FGuLiFlightNavigationCookGate::ValidateWorld(
	UWorld* World,
	const TConstArrayView<FName> RequiredWorldPackages,
	TArray<FGuLiFlightNavigationCookIssue>& OutIssues)
{
	OutIssues.Reset();
	if (!IsValid(World))
	{
		OutIssues.Add(MakeIssue(
			EGuLiFlightNavigationCookIssue::MissingWorld,
			World,
			TEXT("The source world could not be loaded for Flight Navigation validation.")));
		return false;
	}

	const FName WorldPackage = World->GetOutermost()->GetFName();
	const bool bRequiredWorld = IsWorldRequired(WorldPackage, RequiredWorldPackages);
	int32 EnabledVolumeCount = 0;
	for (TActorIterator<AGuLiFlightNavigationVolume> Iterator(World); Iterator; ++Iterator)
	{
		const AGuLiFlightNavigationVolume* Volume = *Iterator;
		if (!IsValid(Volume) || !Volume->bNavigationEnabled)
		{
			continue;
		}

		++EnabledVolumeCount;
		FGuLiFlightNavigationCookIssue Issue;
		if (!ValidateVolume(Volume, Issue))
		{
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	if (bRequiredWorld && EnabledVolumeCount == 0)
	{
		OutIssues.Add(MakeIssue(
			EGuLiFlightNavigationCookIssue::MissingVolume,
			World,
			FString::Printf(
				TEXT("Required world '%s' contains no enabled Flight Navigation volume."),
				*WorldPackage.ToString())));
	}

	return OutIssues.IsEmpty();
}
