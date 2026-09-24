#include "GuLiConstructionShapeSubsystem.h"
#include "GuLiConstructionShapeLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectSaveContext.h"

void UGuLiConstructionShapeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection); Collection.InitializeDependency<UImportSubsystem>();
	auto* Import = GEditor->GetEditorSubsystem<UImportSubsystem>();
	ImportHandle = Import->OnAssetPostImport.AddUObject(this, &ThisClass::Imported);
	ReimportHandle = Import->OnAssetReimport.AddUObject(this, &ThisClass::ObjectChanged);
	SaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddWeakLambda(this, [this](UObject* Object, FObjectPreSaveContext Context) { ObjectChanged(Object); });
	BlueprintHandle = GEditor->OnBlueprintCompiled().AddUObject(this, &ThisClass::BlueprintCompiled);
	PIEHandle = FEditorDelegates::PreBeginPIE.AddUObject(this, &ThisClass::BeforePIE);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 1.f);
}

void UGuLiConstructionShapeSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle); FEditorDelegates::PreBeginPIE.Remove(PIEHandle);
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintHandle);
		if (auto* Import = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			Import->OnAssetPostImport.Remove(ImportHandle); Import->OnAssetReimport.Remove(ReimportHandle);
		}
	}
	Super::Deinitialize();
}

void UGuLiConstructionShapeSubsystem::ObjectChanged(UObject* Object)
{
	if (bPreparing || !Object || Object->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Construction/Shapes/"))) return;
	if (Object->IsA<UStaticMesh>() || Object->IsA<UBlueprint>()
		|| Object->GetName() == TEXT("DT_GuLiStrikeBuildings_Buildings") || Object->GetName() == TEXT("DA_ResourceEconomy")) bPending = true;
}
void UGuLiConstructionShapeSubsystem::Imported(UFactory* Factory, UObject* Object) { ObjectChanged(Object); }
void UGuLiConstructionShapeSubsystem::BlueprintCompiled() { if (!bPreparing) bPending = true; }
void UGuLiConstructionShapeSubsystem::BeforePIE(bool bSimulate) { Prepare(); }
bool UGuLiConstructionShapeSubsystem::Tick(float Delta)
{
	if (bPending && GEditor && !GEditor->PlayWorld && !GIsSavingPackage
		&& !FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().IsLoadingAssets()) Prepare();
	return true;
}
void UGuLiConstructionShapeSubsystem::Prepare()
{
	if (bPreparing) return;
	TGuardValue<bool> Guard(bPreparing, true); bPending = false;
	const FString Result = UGuLiConstructionShapeLibrary::PrepareConstructionShapes();
	UE_LOG(LogTemp, Display, TEXT("[ConstructionShape] %s"), *Result);
}
