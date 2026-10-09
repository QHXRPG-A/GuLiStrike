#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
template<class T> UDataTable* LoadTable(const TSoftObjectPtr<UDataTable>& Ref)
{
	auto* Table = Ref.LoadSynchronous();
	return Table && Table->GetRowStruct() == T::StaticStruct() ? Table : nullptr;
}
template<class T> void ReadRows(const UDataTable& Table, TArray<T>& Rows)
{
	for (const auto& Pair : Table.GetRowMap()) Rows.Add(*reinterpret_cast<const T*>(Pair.Value));
}
bool Matches(const UObject* Object, const FGuLiStrikeModelsModelsRow& Row)
{
	return Object && ((Row.ResourceType == TEXT("StaticMesh") && Object->IsA<UStaticMesh>())
		|| (Row.ResourceType == TEXT("SkeletalMesh") && Object->IsA<USkeletalMesh>())
		|| (Row.ResourceType == TEXT("PresentationClass") && Cast<UClass>(Object) && Cast<UClass>(Object)->IsChildOf(AActor::StaticClass())));
}
bool Scope(const FString& Value, bool Candidate) { return Value == (Candidate ? TEXT("Candidate") : TEXT("Existing")); }
bool CanWrite(const FGuLiStrikeModelsMaterialParametersRow& B, bool Team)
{ return Team ? B.bTeamManaged : B.bRuntimeWritable && !B.bTeamManaged; }
bool ValidCPD(const FGuLiStrikeModelsMaterialParametersRow& B, int32 Count)
{ return B.CustomDataIndex >= 8 && B.CustomDataIndex + Count <= 18; }
bool HasCPDBinding(const UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& B)
{
	if (!Target) return false;
	const FName Name(*B.ParameterName);
	return (B.ParameterType == TEXT("Vector") ? Target->GetCustomPrimitiveDataIndexForVectorParameter(Name)
		: Target->GetCustomPrimitiveDataIndexForScalarParameter(Name)) == B.CustomDataIndex;
}
}

bool UGuLiModelRegistrySubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type==EWorldType::Game || Type==EWorldType::PIE || Type==EWorldType::Editor
		|| Type==EWorldType::EditorPreview || Type==EWorldType::GamePreview;
}

bool UGuLiModelRegistrySubsystem::EnsureCatalog()
{
	if (!Models.IsEmpty()) return true;
	const bool FirstAttempt=!bAttempted;
	bAttempted = true;
	const auto* S = GetDefault<UGuLiModelRegistrySettings>();
	auto* M = LoadTable<FGuLiStrikeModelsModelsRow>(S->ModelsTable);
	auto* P = LoadTable<FGuLiStrikeModelsPartsRow>(S->PartsTable);
	auto* A = LoadTable<FGuLiStrikeModelsMaterialParametersRow>(S->ParametersTable);
	auto* R = LoadTable<FGuLiStrikeModelsColorRegionsRow>(S->RegionsTable);
	if (!M || !P || !A || !R)
	{
		if (FirstAttempt) UE_LOG(LogTemp, Error, TEXT("Model catalogue missing/wrong schema; compile native rows and import all four model tables."));
		return false;
	}
	Tables = {M, P, A, R};
	TArray<FGuLiStrikeModelsModelsRow> Rows; ReadRows(*M, Rows);
	for (const auto& Row : Rows)
	{
		if (Row.Id <= 0 || Models.Contains(Row.Id) || Row.ResourcePath.IsNull()) { Models.Reset(); return false; }
		Models.Add(Row.Id, Row);
	}
	ReadRows(*P, Parts); ReadRows(*A, Parameters); ReadRows(*R, Regions);
	return !Models.IsEmpty();
}

bool UGuLiModelRegistrySubsystem::ReloadCatalog()
{
	Tables.Reset(); Models.Reset(); Parts.Reset(); Parameters.Reset(); Regions.Reset(); Resources.Reset(); bAttempted=false;
	return EnsureCatalog();
}

bool UGuLiModelRegistrySubsystem::GetModelDefinition(int32 Id, FGuLiStrikeModelsModelsRow& Out)
{
	Out = {};
	if (EnsureCatalog()) if (const auto* Row = Models.Find(Id)) { Out = *Row; return true; }
	return false;
}
bool UGuLiModelRegistrySubsystem::Query(const UObject* Context, int32 Id, FGuLiStrikeModelsModelsRow& Out)
{
	if (const auto* W = Context ? Context->GetWorld() : nullptr)
		if (auto* Registry=W->GetSubsystem<UGuLiModelRegistrySubsystem>()) return Registry->GetModelDefinition(Id, Out);
	Out = {};
	if (const auto* Table = LoadTable<FGuLiStrikeModelsModelsRow>(GetDefault<UGuLiModelRegistrySettings>()->ModelsTable))
		for (const auto& Pair : Table->GetRowMap())
		{
			const auto& R = *reinterpret_cast<const FGuLiStrikeModelsModelsRow*>(Pair.Value);
			if (R.Id == Id && Id > 0) { Out = R; return true; }
		}
	return false;
}
UObject* UGuLiModelRegistrySubsystem::LoadModelResource(int32 Id, TSubclassOf<UObject> Expected, bool Candidate)
{
	FGuLiStrikeModelsModelsRow R;
	if (!Expected || !GetModelDefinition(Id, R)) return nullptr;
	// Candidates are editor review only. Never silently enable unapproved paint in a match.
	if (Candidate && (!GetWorld() || GetWorld()->IsGameWorld())) return nullptr;
	const auto Path = (Candidate ? R.CandidateResourcePath : R.ResourcePath).ToSoftObjectPath();
	if (Path.IsNull()) return nullptr;
	UObject* Object = Resources.FindRef(Path);
	if (!Object) Object = Path.TryLoad();
	if (!Matches(Object, R) || !Object->IsA(Expected)) return nullptr;
	Resources.Add(Path, Object);
	return Object;
}
UObject* UGuLiModelRegistrySubsystem::Resolve(const UObject* Context, int32 Id, UClass* Expected)
{
	if (const auto* W = Context ? Context->GetWorld() : nullptr)
		if (auto* Registry=W->GetSubsystem<UGuLiModelRegistrySubsystem>()) return Registry->LoadModelResource(Id, Expected);
	FGuLiStrikeModelsModelsRow R;
	if (!Expected || !Query(Context, Id, R)) return nullptr;
	auto* Object = R.ResourcePath.LoadSynchronous();
	return Matches(Object, R) && Object->IsA(Expected) ? Object : nullptr;
}
UStaticMesh* UGuLiModelRegistrySubsystem::LoadStaticModel(int32 Id, bool Candidate)
{ return Cast<UStaticMesh>(LoadModelResource(Id,UStaticMesh::StaticClass(),Candidate)); }
USkeletalMesh* UGuLiModelRegistrySubsystem::LoadSkeletalModel(int32 Id, bool Candidate)
{ return Cast<USkeletalMesh>(LoadModelResource(Id,USkeletalMesh::StaticClass(),Candidate)); }
TSubclassOf<AActor> UGuLiModelRegistrySubsystem::LoadPresentationClass(int32 Id, bool Candidate)
{
	auto* Class=Cast<UClass>(LoadModelResource(Id,UClass::StaticClass(),Candidate));
	return Class && Class->IsChildOf(AActor::StaticClass()) ? Class : nullptr;
}
TArray<FGuLiStrikeModelsPartsRow> UGuLiModelRegistrySubsystem::GetModelParts(int32 Id)
{
	EnsureCatalog(); return Parts.FilterByPredicate([Id](const auto& R) { return R.ModelId == Id; });
}
TArray<FGuLiStrikeModelsColorRegionsRow> UGuLiModelRegistrySubsystem::GetMutableColorRegions(int32 Id, bool Candidate)
{
	EnsureCatalog(); return Regions.FilterByPredicate([=](const auto& R) { return R.ModelId == Id && Scope(R.Scope, Candidate) && (R.PaintRole == 3 || R.PaintRole == 4 || R.PaintRole == 7); });
}
TArray<FGuLiStrikeModelsMaterialParametersRow> UGuLiModelRegistrySubsystem::GetMaterialParameters(int32 Id, bool Candidate)
{
	EnsureCatalog(); return Parameters.FilterByPredicate([=](const auto& R) { return R.ModelId == Id && Scope(R.Scope, Candidate); });
}
bool UGuLiModelRegistrySubsystem::ParseColor(const FString& Hex, FLinearColor& Out)
{
	if (Hex.Len() != 7 || Hex[0] != TEXT('#')) return false;
	for (int32 I=1; I<7; ++I) if (!FChar::IsHexDigit(Hex[I])) return false;
	Out = FLinearColor::FromSRGBColor(FColor::FromHex(Hex)); Out.A = 1;
	return true;
}
int32 UGuLiModelRegistrySubsystem::FindMaterialSlot(const UMeshComponent* Mesh, const FString& Slot)
{
	return Mesh ? Mesh->GetMaterialIndex(FName(*Slot)) : INDEX_NONE;
}
UMeshComponent* UGuLiModelRegistrySubsystem::FindPart(AActor* Actor, const FString& Path)
{
	if (!Actor) return nullptr;
	TArray<UMeshComponent*> Components; Actor->GetComponents(Components, true);
	for (auto* C : Components)
		if (Path.IsEmpty() || C->GetName() == Path || C->GetPathName().EndsWith(TEXT(".") + Path)) return C;
	return nullptr;
}
bool UGuLiModelRegistrySubsystem::ApplyModelParts(AActor* Actor, int32 Id)
{
	FGuLiStrikeModelsModelsRow Definition;
	if (!Actor || !GetModelDefinition(Id,Definition)) return false;
	bool Success = true;
	for (const auto& Part : GetModelParts(Id))
	{
		auto* Component = FindPart(Actor, Part.ComponentPath);
		UObject* Resource = LoadModelResource(Part.ChildModelId ? Part.ChildModelId : Id, UObject::StaticClass());
		if (auto* Static = Cast<UStaticMeshComponent>(Component); Static && Cast<UStaticMesh>(Resource))
		{
			if (Static->GetStaticMesh() != Resource) Static->SetStaticMesh(Cast<UStaticMesh>(Resource));
		}
		else if (auto* Skeletal = Cast<USkeletalMeshComponent>(Component); Skeletal && Cast<USkeletalMesh>(Resource))
		{
			if (Skeletal->GetSkeletalMeshAsset() != Resource) Skeletal->SetSkeletalMeshAsset(Cast<USkeletalMesh>(Resource));
		}
		else Success = false;
	}
	return Success;
}
int32 UGuLiModelRegistrySubsystem::FindModelIdForResource(const UObject* Resource)
{
	if (!Resource || !EnsureCatalog()) return 0;
	const FSoftObjectPath Path(Resource);
	int32 Match=0;
	for (const auto& Pair : Models) if (Pair.Value.ResourcePath.ToSoftObjectPath()==Path && (!Match || Pair.Key<Match)) Match=Pair.Key;
	return Match;
}
bool UGuLiModelRegistrySubsystem::FindBinding(int32 Id, const FString& Part, const FString& Slot, const FString& Key, bool Candidate, FGuLiStrikeModelsMaterialParametersRow& Out)
{
	EnsureCatalog();
	if (const auto* R = Parameters.FindByPredicate([&](const auto& B) { return B.ModelId == Id
		&& (B.PartKey == Part || (B.Driver == TEXT("CPD") && B.PartKey == TEXT("Root") && B.MaterialSlotName == TEXT("*")))
		&& (B.MaterialSlotName == Slot || B.MaterialSlotName == TEXT("*")) && B.ParameterKey == Key && Scope(B.Scope, Candidate); })) { Out = *R; return true; }
	return false;
}
bool UGuLiModelRegistrySubsystem::ReadVector(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& B, FLinearColor& Out)
{
	Out = FLinearColor::Transparent;
	if (!Target || B.ParameterType != TEXT("Vector")) return false;
	if (B.Driver == TEXT("CPD"))
	{
		const auto& Data = Target->GetCustomPrimitiveData().Data;
		if (!ValidCPD(B,4) || !HasCPDBinding(Target,B)) return false;
		// Unwritten CPD elements render as zero; a material default is not the effective value.
		auto Read = [&Data](int32 I) { return Data.IsValidIndex(I) ? Data[I] : 0.f; };
		Out = FLinearColor(Read(B.CustomDataIndex), Read(B.CustomDataIndex+1), Read(B.CustomDataIndex+2), Read(B.CustomDataIndex+3)); return true;
	}
	const auto* Material = Target->GetMaterial(FindMaterialSlot(Target, B.MaterialSlotName));
	return Material && Material->GetVectorParameterValue(FMaterialParameterInfo(FName(*B.ParameterName)), Out);
}
bool UGuLiModelRegistrySubsystem::ReadScalar(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& B, float& Out)
{
	Out = 0;
	if (!Target || B.ParameterType != TEXT("Scalar")) return false;
	if (B.Driver == TEXT("CPD"))
	{
		const auto& Data = Target->GetCustomPrimitiveData().Data;
		if (!ValidCPD(B,1) || !HasCPDBinding(Target,B)) return false;
		Out = Data.IsValidIndex(B.CustomDataIndex) ? Data[B.CustomDataIndex] : 0.f; return true;
	}
	const auto* Material = Target->GetMaterial(FindMaterialSlot(Target, B.MaterialSlotName));
	return Material && Material->GetScalarParameterValue(FMaterialParameterInfo(FName(*B.ParameterName)), Out);
}
bool UGuLiModelRegistrySubsystem::WriteVector(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& B, FLinearColor V, bool Team)
{
	if (!Target || B.ParameterType != TEXT("Vector") || !CanWrite(B,Team) || !FMath::IsFinite(V.R) || !FMath::IsFinite(V.G) || !FMath::IsFinite(V.B) || !FMath::IsFinite(V.A)) return false;
	if (B.Driver == TEXT("CPD")) { if (!ValidCPD(B,4) || !HasCPDBinding(Target,B)) return false; Target->SetCustomPrimitiveDataVector4(B.CustomDataIndex,FVector4(V.R,V.G,V.B,V.A)); return true; }
	FLinearColor Old; if (!ReadVector(Target,B,Old)) return false;
	auto* MID = Target->CreateDynamicMaterialInstance(FindMaterialSlot(Target,B.MaterialSlotName));
	if (!MID) return false; MID->SetVectorParameterValue(FName(*B.ParameterName),V); return true;
}
bool UGuLiModelRegistrySubsystem::WriteScalar(UMeshComponent* Target, const FGuLiStrikeModelsMaterialParametersRow& B, float V, bool Team)
{
	if (!Target || B.ParameterType != TEXT("Scalar") || !CanWrite(B,Team) || !FMath::IsFinite(V)) return false;
	if (B.Driver == TEXT("CPD")) { if (!ValidCPD(B,1) || !HasCPDBinding(Target,B)) return false; Target->SetCustomPrimitiveDataFloat(B.CustomDataIndex,V); return true; }
	float Old; if (!ReadScalar(Target,B,Old)) return false;
	auto* MID = Target->CreateDynamicMaterialInstance(FindMaterialSlot(Target,B.MaterialSlotName));
	if (!MID) return false; MID->SetScalarParameterValue(FName(*B.ParameterName),V); return true;
}
bool UGuLiModelRegistrySubsystem::SetVectorParameter(UMeshComponent* C,int32 Id,const FString& P,const FString& S,const FString& K,FLinearColor V,bool Candidate)
{ FGuLiStrikeModelsMaterialParametersRow B; return FindBinding(Id,P,S,K,Candidate,B) && WriteVector(C,B,V); }
bool UGuLiModelRegistrySubsystem::SetScalarParameter(UMeshComponent* C,int32 Id,const FString& P,const FString& S,const FString& K,float V,bool Candidate)
{ FGuLiStrikeModelsMaterialParametersRow B; return FindBinding(Id,P,S,K,Candidate,B) && WriteScalar(C,B,V); }
bool UGuLiModelRegistrySubsystem::GetVectorParameter(UMeshComponent* C,int32 Id,const FString& P,const FString& S,const FString& K,FLinearColor& V,bool Candidate)
{ FGuLiStrikeModelsMaterialParametersRow B; return FindBinding(Id,P,S,K,Candidate,B) && ReadVector(C,B,V); }
bool UGuLiModelRegistrySubsystem::GetScalarParameter(UMeshComponent* C,int32 Id,const FString& P,const FString& S,const FString& K,float& V,bool Candidate)
{ FGuLiStrikeModelsMaterialParametersRow B; return FindBinding(Id,P,S,K,Candidate,B) && ReadScalar(C,B,V); }
void UGuLiModelRegistrySubsystem::Deinitialize()
{ Tables.Reset(); Models.Reset(); Parts.Reset(); Parameters.Reset(); Regions.Reset(); Resources.Reset(); bAttempted=false; Super::Deinitialize(); }
FSoftObjectPath GuLiModels::Path(const UObject* C,int32 Id)
{ FGuLiStrikeModelsModelsRow R; return UGuLiModelRegistrySubsystem::Query(C,Id,R) ? R.ResourcePath.ToSoftObjectPath() : FSoftObjectPath(); }
