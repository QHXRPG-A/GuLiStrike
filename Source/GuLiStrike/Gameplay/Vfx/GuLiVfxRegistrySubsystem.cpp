#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiVfx, Log, All);

namespace
{
	UDataTable* LoadCatalog()
	{
		auto* Table = GetDefault<UGuLiVfxRegistrySettings>()->EffectsTable.LoadSynchronous();
		if (!Table || Table->GetRowStruct() != FGuLiStrikeVfxEffectsRow::StaticStruct())
		{
			UE_LOG(LogGuLiVfx, Error, TEXT("VFX catalog is missing or has the wrong row structure."));
			return nullptr;
		}
		return Table;
	}
}

bool UGuLiVfxRegistrySubsystem::IsValidScale(const FVector& Scale)
{
	return !Scale.ContainsNaN() && FMath::IsFinite(Scale.X) && FMath::IsFinite(Scale.Y)
		&& FMath::IsFinite(Scale.Z) && Scale.X > 0 && Scale.Y > 0 && Scale.Z > 0;
}

FVector UGuLiVfxRegistrySubsystem::ComposeScale(FVector BaseScale, FVector DynamicScale)
{
	const FVector Result = BaseScale * DynamicScale;
	return IsValidScale(BaseScale) && IsValidScale(DynamicScale) && IsValidScale(Result) ? Result : FVector::ZeroVector;
}

bool UGuLiVfxRegistrySubsystem::SameDefinition(const FGuLiStrikeVfxEffectsRow& A, const FGuLiStrikeVfxEffectsRow& B)
{
	return A.ResourcePath.ToSoftObjectPath() == B.ResourcePath.ToSoftObjectPath() && A.Scale == B.Scale;
}

bool UGuLiVfxRegistrySubsystem::ValidateDefinitions(const TArray<FGuLiStrikeVfxEffectsRow>& Rows, FString& Error)
{
	TSet<int32> Ids;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const auto& Row = Rows[Index];
		if (Row.Id <= 0 || Ids.Contains(Row.Id) || Row.ResourcePath.IsNull() || !IsValidScale(Row.Scale))
		{
			Error = FString::Printf(TEXT("Invalid/duplicate VFX ID, path or scale: %d"), Row.Id);
			return false;
		}
		for (int32 Other = 0; Other < Index; ++Other) if (SameDefinition(Row, Rows[Other]))
		{
			Error = FString::Printf(TEXT("Duplicate VFX resource and base scale: %d / %d"), Row.Id, Rows[Other].Id);
			return false;
		}
		Ids.Add(Row.Id);
	}
	Error.Reset();
	return true;
}

bool UGuLiVfxRegistrySubsystem::EnsureCatalog()
{
	if (bCatalogAttempted) return Catalog != nullptr;
	bCatalogAttempted = true;
	auto* Table = LoadCatalog();
	if (!Table) return false;
	TArray<FGuLiStrikeVfxEffectsRow> Rows;
	for (const auto& Pair : Table->GetRowMap()) Rows.Add(*reinterpret_cast<const FGuLiStrikeVfxEffectsRow*>(Pair.Value));
	FString Error;
	if (!ValidateDefinitions(Rows, Error))
	{
		UE_LOG(LogGuLiVfx, Error, TEXT("%s"), *Error);
		return false;
	}
	Catalog = Table;
	for (const auto& Row : Rows) Definitions.Add(Row.Id, Row);
	return true;
}

bool UGuLiVfxRegistrySubsystem::GetDefinition(int32 VfxId, FGuLiStrikeVfxEffectsRow& Definition)
{
	Definition = {};
	if (VfxId == 0) return false;
	if (EnsureCatalog()) if (const auto* Found = Definitions.Find(VfxId)) { Definition = *Found; return true; }
	if (!ReportedFailures.Contains(VfxId))
	{
		ReportedFailures.Add(VfxId);
		UE_LOG(LogGuLiVfx, Error, TEXT("Unknown VfxId %d; skipping presentation."), VfxId);
	}
	return false;
}

bool UGuLiVfxRegistrySubsystem::GetDefinitionWithoutWorld(int32 VfxId, FGuLiStrikeVfxEffectsRow& Definition)
{
	Definition = {};
	if (VfxId <= 0) return false;
	if (const auto* Table = LoadCatalog())
	{
		for (const auto& Pair : Table->GetRowMap())
		{
			const auto* Row = reinterpret_cast<const FGuLiStrikeVfxEffectsRow*>(Pair.Value);
			if (Row->Id == VfxId && IsValidScale(Row->Scale) && !Row->ResourcePath.IsNull()) { Definition = *Row; return true; }
		}
	}
	return false;
}

bool UGuLiVfxRegistrySubsystem::MatchesType(const UObject* Resource, const UClass* ExpectedClass)
{
	return IsValid(Resource) && ExpectedClass && Resource->IsA(ExpectedClass);
}

TSubclassOf<AActor> UGuLiVfxRegistrySubsystem::LoadActorClass(int32 VfxId, TSubclassOf<AActor> ExpectedBaseClass)
{
	UClass* Class = Cast<UClass>(LoadResource(VfxId, UClass::StaticClass()));
	if (Class && ExpectedBaseClass && Class->IsChildOf(ExpectedBaseClass)) return Class;
	if (Class) UE_LOG(LogGuLiVfx, Error, TEXT("VfxId %d resolves to an incompatible actor class; skipping presentation."), VfxId);
	return nullptr;
}

UObject* UGuLiVfxRegistrySubsystem::LoadResource(int32 VfxId, TSubclassOf<UObject> ExpectedClass, bool LoadIfNeeded)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer || !ExpectedClass) return nullptr;
	FGuLiStrikeVfxEffectsRow Definition;
	if (!GetDefinition(VfxId, Definition)) return nullptr;
	const auto Path = Definition.ResourcePath.ToSoftObjectPath();
	UObject* Object = Resources.FindRef(Path);
	if (!Object) Object = LoadIfNeeded ? Path.TryLoad() : Path.ResolveObject();
	if (!MatchesType(Object, ExpectedClass))
	{
		if (LoadIfNeeded || Object) UE_LOG(LogGuLiVfx, Error, TEXT("VfxId %d (%s) cannot resolve as %s; skipping presentation."),
			VfxId, *Path.ToString(), *ExpectedClass->GetName());
		return nullptr;
	}
	Resources.Add(Path, Object);
	return Object;
}

UObject* UGuLiVfxRegistrySubsystem::Resolve(const UObject* Context, int32 VfxId, UClass* ExpectedClass, bool LoadIfNeeded)
{
	if (const UWorld* World = Context ? Context->GetWorld() : nullptr)
		if (auto* Registry = World->GetSubsystem<UGuLiVfxRegistrySubsystem>()) return Registry->LoadResource(VfxId, ExpectedClass, LoadIfNeeded);
#if WITH_EDITOR
	FGuLiStrikeVfxEffectsRow Definition;
	if (GetDefinitionWithoutWorld(VfxId, Definition))
	{
		UObject* Object = LoadIfNeeded ? Definition.ResourcePath.LoadSynchronous() : Definition.ResourcePath.Get();
		if (MatchesType(Object, ExpectedClass)) return Object;
		UE_LOG(LogGuLiVfx, Error, TEXT("VfxId %d has missing resource or wrong resource type."), VfxId);
	}
#endif
	return nullptr;
}

bool UGuLiVfxRegistrySubsystem::Query(const UObject* Context, int32 VfxId, FGuLiStrikeVfxEffectsRow& Definition)
{
	if (const UWorld* World = Context ? Context->GetWorld() : nullptr)
		if (auto* Registry = World->GetSubsystem<UGuLiVfxRegistrySubsystem>()) return Registry->GetDefinition(VfxId, Definition);
	return GetDefinitionWithoutWorld(VfxId, Definition);
}

void UGuLiVfxRegistrySubsystem::Deinitialize()
{
	Resources.Reset(); Definitions.Reset(); Catalog = nullptr; ReportedFailures.Reset(); bCatalogAttempted = false;
	Super::Deinitialize();
}

FVector GuLiVfx::Scale(const UObject* Context, int32 VfxId, const FVector& DynamicScale)
{
	FGuLiStrikeVfxEffectsRow Definition;
	return UGuLiVfxRegistrySubsystem::Query(Context, VfxId, Definition)
		? UGuLiVfxRegistrySubsystem::ComposeScale(Definition.Scale, DynamicScale) : FVector::ZeroVector;
}

FSoftObjectPath GuLiVfx::Path(const UObject* Context, int32 VfxId)
{
	FGuLiStrikeVfxEffectsRow Definition;
	return UGuLiVfxRegistrySubsystem::Query(Context, VfxId, Definition) ? Definition.ResourcePath.ToSoftObjectPath() : FSoftObjectPath();
}
