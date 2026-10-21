#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Gameplay/Building/GuLiBuildingVisuals.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCommanderMeshLODLifecycleTest,
	"GuLiStrike.Commander.LOD.MeshLifecycleAndWorldIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderMeshLODLifecycleTest::RunTest(const FString&)
{
	auto* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("gs.Commander.MeshLOD"));
	const int32 Previous = Enabled->GetInt();
	Enabled->Set(1, ECVF_SetByConsole);
	UWorld* A = UWorld::CreateWorld(EWorldType::Game, false);
	UWorld* B = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT
	{
		Enabled->Set(Previous, ECVF_SetByConsole);
		for (UWorld* World : {A, B}) if (World)
		{ World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); }
	};
	if (!TestNotNull(TEXT("World A"), A) || !TestNotNull(TEXT("World B"), B)) return false;
	for (UWorld* World : {A, B}) if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* PA = A->GetSubsystem<UGuLiCommanderLODSubsystem>();
	auto* PB = B->GetSubsystem<UGuLiCommanderLODSubsystem>();
	if (!TestNotNull(TEXT("Policy A"), PA) || !TestNotNull(TEXT("Policy B"), PB)) return false;
	// Supply local-view requirements without needing interactive windows in this isolated test.
	auto Views = [](UGuLiCommanderLODSubsystem* Policy, TArray<int32> Tiers)
	{
		Policy->Views.Reset();
		for (int32 Tier : Tiers)
		{
			auto& V = Policy->Views.AddDefaulted_GetRef();
			if (Tier >= 0) V.PresentationTier = EGuLiPresentationTier(Tier);
		}
		Policy->ViewFrame = GFrameCounter;
		Policy->EffectiveSettings.MinimumResidenceSeconds = 0;
		Policy->Tick(0);
	};
	auto* Model = LoadObject<UStaticMesh>(nullptr,TEXT("/Game/GuLiStrike/Commander/Units/WM01/LOD_3Tier_v1/Meshes/SM_WarMachine_Rigid.SM_WarMachine_Rigid"));
	auto* OneLOD = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto* Skeleton = LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/GuLiStrike/Buildings/SSFStylized/Blue/AirBase/Meshes/SK_Blue_AirBase.SK_Blue_AirBase"));
	if (!TestNotNull(TEXT("Production three-resource-LOD mesh"), Model) || !TestNotNull(TEXT("Fallback mesh"),OneLOD)
		|| !TestNotNull(TEXT("Production skeletal mesh"),Skeleton)) return false;
	auto MakeMesh = [&](UWorld* World)
	{
		auto* Actor = World->SpawnActor<APawn>();
		auto* Mesh = NewObject<UInstancedStaticMeshComponent>(Actor);
		Actor->SetRootComponent(Mesh); Actor->AddInstanceComponent(Mesh);
		Mesh->SetStaticMesh(Model); Mesh->RegisterComponent();
		World->GetSubsystem<UGuLiCommanderLODSubsystem>()->RegisterMesh(Mesh);
		return Mesh;
	};
	auto* MA = MakeMesh(A); auto* MB = MakeMesh(B);
	auto* OriginalOutline = MA->GetMaterial(0);
	auto* OriginalBody = MA->GetMaterial(1);
	Views(PA,{1}); Views(PB,{0});
	TestTrue(TEXT("ISM uses a minimum, preserving automatic coarser selection"), MA->bOverrideMinLOD && MA->MinLOD==1 && MA->GetForcedLodModel()==0);
	TestFalse(TEXT("Another World retains full detail"), MB->bOverrideMinLOD);
	TestTrue(TEXT("Reduced tier suppresses only the outline slot"),
		MA->GetMaterial(0) == PA->NoModelOutlineMaterial && MA->GetMaterial(1) == OriginalBody);
	TestTrue(TEXT("Full-detail World retains its contour"), MB->GetMaterial(0) == OriginalOutline);
	Views(PA,{2,0});
	TestFalse(TEXT("Highest local-view precision wins"), MA->bOverrideMinLOD);
	TestTrue(TEXT("Returning to full detail restores outline and override length"),
		MA->GetMaterial(0) == OriginalOutline && MA->OverrideMaterials.Num() == 0);
	Views(PA,{2,-1});
	TestFalse(TEXT("A driving view restores normal mesh selection"), MA->bOverrideMinLOD);
	Views(PA,{2});
	auto* Skin = NewObject<USkeletalMeshComponent>(MA->GetOwner());
	MA->GetOwner()->AddInstanceComponent(Skin); Skin->SetSkeletalMeshAsset(Skeleton); Skin->RegisterComponent();
	PA->RegisterMesh(Skin);
	TestTrue(TEXT("Dynamic skeletal registration applies current tier"), Skin->bOverrideMinLod && Skin->MinLodModel>=1);
	auto* Copy = Cast<UStaticMeshComponent>(GuLiBuildingVisuals::CopyMesh(*MA->GetOwner(),*MA,*MA,FTransform::Identity,nullptr,false));
	TestTrue(TEXT("Construction copy applies current tier"), Copy && Copy->bOverrideMinLOD && Copy->MinLOD==1);
	PA->UnregisterMesh(Copy);
	TestTrue(TEXT("Unregister restores copied component"), Copy && !Copy->bOverrideMinLOD && Copy->MinLOD==0);
	TestTrue(TEXT("A proxy copied during suppression restores the source contour"), Copy && Copy->GetMaterial(0) == OriginalOutline);
	MA->SetStaticMesh(OneLOD); PA->RegisterMesh(MA);
	TestTrue(TEXT("Missing MeshLOD1 retains original settings"), !MA->bOverrideMinLOD && MA->MinLOD==0);
	MA->SetStaticMesh(Model); PA->RegisterMesh(MA);
	TestTrue(TEXT("Pool reuse with a new resource reapplies cap"), MA->bOverrideMinLOD && MA->MinLOD==1);
	Enabled->Set(0, ECVF_SetByConsole); Views(PA,{1});
	TestTrue(TEXT("Disable restores static and skeletal settings"), !MA->bOverrideMinLOD && !Skin->bOverrideMinLod);
	TestTrue(TEXT("Disable restores model outlines"), MA->GetMaterial(0) == OriginalOutline);
	PA->UnregisterMesh(MA); MA->MinLOD=2; MA->bOverrideMinLOD=true; MA->SetForcedLodModel(3);
	PA->RegisterMesh(MA); Enabled->Set(1, ECVF_SetByConsole); Views(PA,{1});
	TestTrue(TEXT("Existing coarser minimum and forced resource are preserved"), MA->MinLOD==2 && MA->GetForcedLodModel()==3);
	PA->UnregisterMesh(MA);
	TestTrue(TEXT("Exact pre-registration override survives unregister"), MA->bOverrideMinLOD && MA->MinLOD==2 && MA->GetForcedLodModel()==3);
	PA->RegisterMesh(MA); MA->GetOwner()->Destroy(); PA->UnregisterActorMeshes(MA->GetOwner());
	TestFalse(TEXT("Destroyed actor is no longer registered"), PA->MeshEntries.Contains(MA));
	return !HasAnyErrors();
}
#endif
