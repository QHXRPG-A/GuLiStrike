// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_EDITOR

#include "Camera/CameraComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Disposable, opt-in rendering fixture. No asset saves, authority writes, timers or added ticks.
namespace GuLiCommanderRingBenchmark
{
	struct FSnapshot
	{
		TWeakObjectPtr<AActor> Fixture;
		TWeakObjectPtr<AGuLiCommanderPresentationActor> Original;
		TWeakObjectPtr<AGuLiCommanderPlayerController> PC;
		TWeakObjectPtr<AGuLiCommanderCameraPawn> Pawn;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Units, Rings;
		FTransform PawnTransform;
		FRotator ArmRotation;
		float ArmLength = 0, FOV = 0;
		bool Hidden = false, PCTick = false, PawnTick = false, ArmTick = false;
		bool Collision = false, Lag = false, RotationLag = false, ControlRotation = false;
		FString Stage = TEXT("source"), View = TEXT("original");
	} State;

	UWorld* LocalWorld()
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (World && World->WorldType == EWorldType::PIE && World->GetNetMode() == NM_Standalone) { return World; }
		UE_LOG(LogTemp, Error, TEXT("Ring benchmark requires a local standalone PIE match."));
		return nullptr;
	}

	void Record(UWorld* World, const TCHAR* Action)
	{
		UGuLiBattleAuthoritySubsystem* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		UInstancedStaticMeshComponent* Rings = State.Fixture.IsValid() ? State.Rings.Get() : nullptr;
		AGuLiCommanderCameraPawn* Pawn = State.Pawn.Get();
		int32 Width = 0, Height = 0;
		if (State.PC.IsValid()) { State.PC->GetViewportSize(Width, Height); }
		const FString Line = FString::Printf(TEXT("%s action=%s authority=%d units=%d rings=%d originalTick=%d grid=25x20 spacingCm=1000 colors=source-snapshot stage=%s view=%s viewport=%dx%d pawn=%s arm=%.0f pitch=%.1f fov=%.1f unitMesh=%s ringMesh=%s ringMaterial=%s\n"),
			*FDateTime::UtcNow().ToIso8601(), Action, Authority ? Authority->GetAuthoritativeMemberCount() : -1,
			Rings && State.Units.IsValid() ? State.Units->GetInstanceCount() : 0, Rings ? Rings->GetInstanceCount() : 0,
			State.Original.IsValid() && State.Original->IsActorTickEnabled(), *State.Stage, *State.View, Width, Height,
			Pawn ? *Pawn->GetActorLocation().ToString() : TEXT("none"), Pawn ? Pawn->GetCommanderSpringArm()->TargetArmLength : 0,
			Pawn ? Pawn->GetCommanderSpringArm()->GetRelativeRotation().Pitch : 0, Pawn ? Pawn->GetCommanderCamera()->FieldOfView : 0,
			*GetPathNameSafe(State.Units.IsValid() ? State.Units->GetStaticMesh() : nullptr),
			*GetPathNameSafe(Rings ? Rings->GetStaticMesh() : nullptr), *GetPathNameSafe(Rings ? Rings->GetMaterial(0) : nullptr));
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		const FString Directory = FPaths::ProjectDir() / TEXT("outputs/commander-ring-hud-20260830");
		IFileManager::Get().MakeDirectory(*Directory, true);
		if (!FFileHelper::SaveStringToFile(Line, *(Directory / TEXT("ring-benchmark-fixture.log")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
		{ UE_LOG(LogTemp, Warning, TEXT("Ring benchmark evidence log could not be written.")); }
	}

	void Restore()
	{
		UWorld* World = LocalWorld();
		if (!World || (State.Fixture.IsValid() && State.Fixture->GetWorld() != World)) { return; }
		if (State.Original.IsValid()) { State.Original->SetActorHiddenInGame(State.Hidden); }
		if (State.PC.IsValid()) { State.PC->SetActorTickEnabled(State.PCTick); }
		if (AGuLiCommanderCameraPawn* Pawn = State.Pawn.Get())
		{
			Pawn->SetActorTransform(State.PawnTransform); Pawn->SetActorTickEnabled(State.PawnTick);
			USpringArmComponent* Arm = Pawn->GetCommanderSpringArm();
			Arm->TargetArmLength = State.ArmLength; Arm->SetRelativeRotation(State.ArmRotation);
			Arm->bDoCollisionTest = State.Collision; Arm->bEnableCameraLag = State.Lag;
			Arm->bEnableCameraRotationLag = State.RotationLag; Arm->bUsePawnControlRotation = State.ControlRotation;
			Arm->SetComponentTickEnabled(State.ArmTick); Pawn->GetCommanderCamera()->SetFieldOfView(State.FOV);
		}
		if (State.Fixture.IsValid()) { State.Fixture->Destroy(); }
		State.Fixture.Reset(); Record(World, TEXT("restore")); State = FSnapshot();
	}

	void Setup()
	{
		UWorld* World = LocalWorld();
		if (!World) { return; }
		if (State.Fixture.IsValid()) { UE_LOG(LogTemp, Error, TEXT("Restore the current ring fixture before setting up another.")); return; }
		AGuLiCommanderPlayerController* PC = Cast<AGuLiCommanderPlayerController>(World->GetFirstPlayerController());
		AGuLiCommanderCameraPawn* Pawn = PC ? Cast<AGuLiCommanderCameraPawn>(PC->GetPawn()) : nullptr;
		AGuLiCommanderPresentationActor* Original = nullptr;
		for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It) { Original = *It; break; }
		UGuLiBattleAuthoritySubsystem* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		UInstancedStaticMeshComponent* SourceUnits = Original ? Original->GetUnitInstances() : nullptr;
		UInstancedStaticMeshComponent* SourceRings = Original ? Original->GetRingInstances() : nullptr;
		if (!PC || !PC->HasAuthority() || !PC->IsLocalController() || !Pawn || !Pawn->GetCommanderSpringArm() || !Pawn->GetCommanderCamera()
			|| !Authority || Authority->GetAuthoritativeMemberCount() != 500 || !Original || !Original->IsActorTickEnabled()
			|| !SourceUnits || !SourceRings || SourceUnits->GetInstanceCount() != 500 || SourceRings->GetInstanceCount() != 500
			|| !SourceUnits->GetStaticMesh() || !SourceRings->GetStaticMesh() || !SourceRings->GetMaterial(0)
			|| SourceRings->NumCustomDataFloats != 4 || SourceRings->PerInstanceSMCustomData.Num() != 2000)
		{ UE_LOG(LogTemp, Error, TEXT("Ring fixture needs authority=500 and ready original 500-unit/500-ring presentation with RGBA data.")); return; }
		for (int32 Index = 0; Index < 2000; ++Index)
			if (!FMath::IsFinite(SourceRings->PerInstanceSMCustomData[Index]) || (Index % 4 == 3 && SourceRings->PerInstanceSMCustomData[Index] <= 0))
			{ UE_LOG(LogTemp, Error, TEXT("Original ring color data is not ready for all 500 instances.")); return; }
		State = FSnapshot(); State.Original = Original; State.PC = PC; State.Pawn = Pawn;
		State.Hidden = Original->IsHidden(); State.PCTick = PC->IsActorTickEnabled(); State.PawnTick = Pawn->IsActorTickEnabled();
		State.PawnTransform = Pawn->GetActorTransform(); State.FOV = Pawn->GetCommanderCamera()->FieldOfView;
		USpringArmComponent* Arm = Pawn->GetCommanderSpringArm();
		State.ArmLength = Arm->TargetArmLength; State.ArmRotation = Arm->GetRelativeRotation(); State.ArmTick = Arm->IsComponentTickEnabled();
		State.Collision = Arm->bDoCollisionTest; State.Lag = Arm->bEnableCameraLag;
		State.RotationLag = Arm->bEnableCameraRotationLag; State.ControlRotation = Arm->bUsePawnControlRotation;
		FActorSpawnParameters Spawn; Spawn.ObjectFlags = RF_Transient; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Fixture = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		if (!Fixture) { State = FSnapshot(); return; }
		State.Fixture = Fixture; Fixture->Tags.Add(TEXT("CommanderRingBench")); Fixture->SetReplicates(false); Fixture->SetActorTickEnabled(false);
		auto AddMesh = [Fixture](UInstancedStaticMeshComponent* Source)
		{
			UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(Fixture, NAME_None, RF_Transient);
			Fixture->AddInstanceComponent(Mesh);
			if (Fixture->GetRootComponent()) { Mesh->SetupAttachment(Fixture->GetRootComponent()); } else { Fixture->SetRootComponent(Mesh); }
			Mesh->SetMobility(EComponentMobility::Movable); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCanEverAffectNavigation(false); Mesh->SetIsReplicated(false); Mesh->SetComponentTickEnabled(false);
			Mesh->SetCastShadow(false); Mesh->SetAffectDistanceFieldLighting(false); Mesh->SetAffectDynamicIndirectLighting(false); Mesh->SetVisibleInRayTracing(false);
			Mesh->SetCullDistances(Source->InstanceStartCullDistance, Source->InstanceEndCullDistance);
			Mesh->bRenderInMainPass = Source->bRenderInMainPass; Mesh->bRenderInDepthPass = Source->bRenderInDepthPass;
			Mesh->bReceivesDecals = Source->bReceivesDecals; Mesh->bUseAsOccluder = Source->bUseAsOccluder;
			Mesh->TranslucencySortPriority = Source->TranslucencySortPriority; Mesh->BoundsScale = Source->BoundsScale;
			Mesh->NumCustomDataFloats = 4; Mesh->SetStaticMesh(Source->GetStaticMesh());
			for (int32 Slot = 0; Slot < Source->GetNumMaterials(); ++Slot) { Mesh->SetMaterial(Slot, Source->GetMaterial(Slot)); }
			Mesh->RegisterComponent(); Mesh->PreAllocateInstancesMemory(500); return Mesh;
		};
		State.Units = AddMesh(SourceUnits); State.Rings = AddMesh(SourceRings);
		const FVector Origin = Pawn->GetActorLocation() - FVector(0, 0, 150);
		for (int32 Index = 0; Index < 500; ++Index)
		{
			const FVector Position = Origin + FVector((Index % 25 - 12) * 1000, (Index / 25 - 9.5) * 1000, 0);
			State.Units->AddInstance(FTransform(FQuat::Identity, Position, FVector::OneVector));
			State.Rings->AddInstance(FTransform(FQuat::Identity, Position + FVector(0, 0, 35), FVector(12, 12, 0.02)));
			State.Rings->SetCustomData(Index, MakeArrayView(SourceRings->PerInstanceSMCustomData.GetData() + Index * 4, 4));
		}
		State.Rings->MarkRenderStateDirty(); Original->SetActorHiddenInGame(true);
		PC->SetActorTickEnabled(false); Pawn->SetActorTickEnabled(false); Arm->SetComponentTickEnabled(true);
		Arm->bDoCollisionTest = false; Arm->bEnableCameraLag = false; Arm->bEnableCameraRotationLag = false; Arm->bUsePawnControlRotation = false;
		Record(World, TEXT("setup"));
	}

	void Change(const TArray<FString>& Args, bool bStage)
	{
		UWorld* World = LocalWorld();
		if (!World || !State.Fixture.IsValid() || State.Fixture->GetWorld() != World || !State.Rings.IsValid() || !State.Pawn.IsValid())
		{ UE_LOG(LogTemp, Error, TEXT("Set up the ring benchmark in this PIE first.")); return; }
		if (Args.Num() != 1 || (bStage ? Args[0] != TEXT("before") && Args[0] != TEXT("after") : Args[0] != TEXT("near") && Args[0] != TEXT("far")))
		{ UE_LOG(LogTemp, Error, TEXT("Expected stage before|after or view near|far.")); return; }
		if (bStage)
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Args[0] == TEXT("before") ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Game/Commander/Units/SM_CommanderUnitRing.SM_CommanderUnitRing"));
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Args[0] == TEXT("before") ? TEXT("/Game/Commander/QA/M_CommanderUnitRing_LegacyBenchmark.M_CommanderUnitRing_LegacyBenchmark") : TEXT("/Game/Commander/UI/M_CommanderUnitRing.M_CommanderUnitRing"));
			if (!Mesh || !Material) { UE_LOG(LogTemp, Error, TEXT("Ring benchmark stage assets are missing; fixture unchanged.")); return; }
			State.Rings->SetStaticMesh(Mesh); State.Rings->SetMaterial(0, Material); State.Stage = Args[0];
		}
		else
		{
			AGuLiCommanderCameraPawn* Pawn = State.Pawn.Get(); USpringArmComponent* Arm = Pawn->GetCommanderSpringArm();
			Pawn->SetActorLocationAndRotation(State.PawnTransform.GetLocation(), FRotator::ZeroRotator);
			Arm->TargetArmLength = Args[0] == TEXT("near") ? 25000.0f : 80000.0f; Arm->SetRelativeRotation(FRotator(-55, 0, 0));
			Pawn->GetCommanderCamera()->SetFieldOfView(45.0f); State.View = Args[0];
		}
		Record(World, bStage ? TEXT("stage") : TEXT("view"));
	}

	FAutoConsoleCommand SetupCommand(TEXT("gs.Commander.RingBenchSetup"), TEXT("Create a transient 500-soldier rendering fixture in standalone PIE."), FConsoleCommandDelegate::CreateStatic(&Setup));
	FAutoConsoleCommand StageCommand(TEXT("gs.Commander.RingBenchStage"), TEXT("Switch only fixture rings: before|after."), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { Change(Args, true); }));
	FAutoConsoleCommand ViewCommand(TEXT("gs.Commander.RingBenchView"), TEXT("Fix the fixture camera: near|far."), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { Change(Args, false); }));
	FAutoConsoleCommand RestoreCommand(TEXT("gs.Commander.RingBenchRestore"), TEXT("Destroy the fixture and restore original presentation, camera and actor ticks. Ending PIE also discards it."), FConsoleCommandDelegate::CreateStatic(&Restore));
}
#endif
