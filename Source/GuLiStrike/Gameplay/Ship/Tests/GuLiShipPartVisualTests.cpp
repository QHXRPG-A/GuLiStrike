#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiStrikeWeaponPart.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace GuLiShipPartVisualTests
{
	struct FWorldFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldFixture() { if (World && GEngine) { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); } }
		~FWorldFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) { GEngine->DestroyWorldContext(World); }
			}
		}
	};

	UStaticMesh* MakeSocketMesh()
	{
		UStaticMesh* Mesh = NewObject<UStaticMesh>();
		UStaticMeshSocket* Socket = NewObject<UStaticMeshSocket>(Mesh);
		Socket->SocketName = TEXT("PreservedMount");
		Socket->RelativeLocation = FVector(130, -50, 25);
		Socket->RelativeRotation = FRotator(10, 25, 180);
		Socket->RelativeScale = FVector(2);
		Socket->Tag = TEXT("KeepUserSocket");
		Mesh->Sockets.Add(Socket);
		return Mesh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipPartVisualLifecycleTest,
	"GuLiStrike.Ship.Parts.VisualLifecycleAndSockets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipPartVisualLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipPartVisualTests;
	FWorldFixture Fixture;
	UClass* PartClass = LoadClass<UGuLiStrikeWeaponPart>(nullptr, TEXT("/Game/GuLiStrike/Ship/Parts/BP_SC_Thor_MissilePod.BP_SC_Thor_MissilePod_C"));
	USkeletalMesh* Turret = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assets/Ships/ShipComponent/Rigged/SKM_SC_CIWS.SKM_SC_CIWS"));
	if (!TestNotNull(TEXT("Test world"), Fixture.World) || !TestNotNull(TEXT("Existing migrated weapon class"), PartClass)
		|| !TestNotNull(TEXT("Authored skeletal turret"), Turret)) { return false; }
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	UStaticMesh* SocketMesh = MakeSocketMesh();
	UStaticMeshComponent* Hull = NewObject<UStaticMeshComponent>(Owner);
	Owner->SetRootComponent(Hull);
	Hull->SetStaticMesh(SocketMesh);
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hull->SetMobility(EComponentMobility::Movable);
	Hull->RegisterComponent();
	Owner->SetActorTransform(FTransform(FRotator(5, 80, 0), FVector(500, 700, 900), FVector(1.5)));
	UGuLiStrikeWeaponPart* Part = NewObject<UGuLiStrikeWeaponPart>(Owner, PartClass);
	Part->StaticMesh = SocketMesh;
	Part->VisualType = EGuLiStrikeShipPartVisualType::StaticMesh;
	Part->RegisterComponent();
	Part->AttachToComponent(Hull, FAttachmentTransformRules::KeepRelativeTransform, TEXT("PreservedMount"));
	Part->SetRelativeTransform(FTransform(FRotator(0, 30, 0), FVector(3, 5, 7), FVector(.75)));
	TestTrue(TEXT("Static visual is registered"), Part->IsVisualMeshReady());
	TestTrue(TEXT("Static visual remains a separate component"), Part->GetVisualMeshComponent()->IsA<UStaticMeshComponent>());
	TestEqual(TEXT("Visual collision remains disabled"), Part->GetVisualMeshComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	Part->MuzzleSocketName = TEXT("PreservedMount");
	FTransform Local;
	TestTrue(TEXT("Static muzzle socket resolves"), Part->GetMuzzleTransformRelativeToPart(Local));
	TestTrue(TEXT("Static socket's local position is preserved"), Local.GetLocation().Equals(FVector(130,-50,25)));
	const FTransform OriginalMount = Part->GetComponentTransform();
	const FTransform OriginalRelative = Part->GetRelativeTransform();
	TWeakObjectPtr<UMeshComponent> OldVisual = Part->GetVisualMeshComponent();
	TestTrue(TEXT("Switch to skeletal visual"), Part->SetPartSkeletalMesh(Turret));
	TestFalse(TEXT("Old visual is destroyed"), OldVisual.IsValid());
	TestNotNull(TEXT("Skeletal component is accessible"), Part->GetSkeletalVisualComponent());
	TestTrue(TEXT("Installation world transform is unchanged"), Part->GetComponentTransform().Equals(OriginalMount));
	TestTrue(TEXT("Installation offset is unchanged"), Part->GetRelativeTransform().Equals(OriginalRelative));
	TestTrue(TEXT("Installation parent is unchanged"), Part->GetAttachParent() == Hull);
	TestEqual(TEXT("Installation socket is unchanged"), Part->GetAttachSocketName(), FName("PreservedMount"));
	Part->MuzzleSocketName = TEXT("Socket_1");
	TestTrue(TEXT("Bone-attached muzzle resolves"), Part->GetMuzzleTransformRelativeToPart(Local));
	TestTrue(TEXT("Bone-attached socket stays at the actual CIWS muzzle"), Local.GetLocation().Equals(FVector(-13.22722,413.21051,11.107655), .01));
	TestTrue(TEXT("Socket forward follows the barrel's +Y reference direction"), Local.GetRotation().GetForwardVector().Equals(FVector(0,1,0), .0001));
	TestTrue(TEXT("Scene component socket queries use the visual mesh"), Part->GetSocketTransform(TEXT("Socket_1")).Equals(Local * Part->GetComponentTransform(), .001));
	Part->MuzzleSocketName = TEXT("MissingMuzzle");
	TestFalse(TEXT("A missing explicit muzzle does not fire from the part origin"), Part->GetMuzzleTransformRelativeToPart(Local));
	Part->MuzzleSocketName = NAME_None;
	TestTrue(TEXT("Legacy muzzle offset still works"), Part->GetMuzzleTransformRelativeToPart(Local));
	TestTrue(TEXT("Legacy muzzle offset is identical"), Local.GetLocation().Equals(Part->MuzzleOffset));
	Part->SetVisibility(false);
	TestFalse(TEXT("Visibility propagates to the visual"), Part->GetVisualMeshComponent()->IsVisible());
	Part->SetHiddenInGame(true);
	TestTrue(TEXT("Hidden-in-game propagates"), Part->GetVisualMeshComponent()->bHiddenInGame);
	TWeakObjectPtr<UMeshComponent> BeforeUnregister = Part->GetVisualMeshComponent();
	Part->UnregisterComponent();
	TestFalse(TEXT("Unregister destroys the visual"), BeforeUnregister.IsValid());
	Part->RegisterComponent();
	TestTrue(TEXT("Reregister recreates one visual"), Part->IsVisualMeshReady());
	TWeakObjectPtr<UMeshComponent> BeforeDestroy = Part->GetVisualMeshComponent();
	Part->DestroyComponent();
	TestFalse(TEXT("Destroy removes the visual"), BeforeDestroy.IsValid());
	TestEqual(TEXT("Hull socket array was never rewritten"), SocketMesh->Sockets.Num(), 1);
	TestEqual(TEXT("Hull socket tag was never rewritten"), SocketMesh->Sockets[0]->Tag, FString("KeepUserSocket"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipPartInstallationTest,
	"GuLiStrike.Ship.Parts.InstallationRollback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipPartInstallationTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipPartVisualTests;
	FWorldFixture Fixture;
	UClass* ShipClass = LoadClass<AGuLiStrikeShip>(nullptr, TEXT("/Game/GuLiStrike/Ship/BP_CombatAvatarFly01.BP_CombatAvatarFly01_C"));
	UClass* PartClass = LoadClass<UGuLiStrikeWeaponPart>(nullptr, TEXT("/Game/GuLiStrike/Ship/Parts/BP_SC_Thor_MissilePod.BP_SC_Thor_MissilePod_C"));
	if (!TestNotNull(TEXT("Ship class"), ShipClass) || !TestNotNull(TEXT("Part class"), PartClass)) { return false; }
	AGuLiStrikeShip* Ship = Fixture.World->SpawnActor<AGuLiStrikeShip>(ShipClass);
	if (!TestNotNull(TEXT("Ship instance"), Ship)) { return false; }
	UStaticMeshComponent* Hull = Ship->FindComponentByClass<UStaticMeshComponent>();
	if (!TestNotNull(TEXT("Original hull component"), Hull)) { return false; }
	Hull->SetStaticMesh(MakeSocketMesh());
	UGuLiStrikeWeaponPart* Defaults = GetMutableDefault<UGuLiStrikeWeaponPart>(PartClass);
	UBlueprintGeneratedClass* BlueprintClass = CastChecked<UBlueprintGeneratedClass>(PartClass);
	// Rebuild the Blueprint initialization cache after the guarded defaults are restored.
	ON_SCOPE_EXIT { BlueprintClass->UpdateCustomPropertyListForPostConstruction(); };
	TGuardValue<TArray<FName>> RestoreSockets(Defaults->CompatibleSockets, {FName("PreservedMount")});
	TGuardValue<EGuLiStrikeShipPartVisualType> RestoreType(Defaults->VisualType, EGuLiStrikeShipPartVisualType::StaticMesh);
	TGuardValue<TObjectPtr<USkeletalMesh>> RestoreMesh(Defaults->SkeletalMesh, nullptr);
	BlueprintClass->UpdateCustomPropertyListForPostConstruction();
	TestTrue(TEXT("Existing static part installs through the original API"), Ship->InstallPart(PartClass, TEXT("PreservedMount")));
	UGuLiStrikeShipPartComponent* Previous = Ship->GetPartAt(TEXT("PreservedMount"));
	if (!TestNotNull(TEXT("Part is in the installation registry"), Previous)) { return false; }
	Defaults->VisualType = EGuLiStrikeShipPartVisualType::SkeletalMesh;
	BlueprintClass->UpdateCustomPropertyListForPostConstruction();
	TestFalse(TEXT("Unconfigured skeletal replacement is rejected"), Ship->InstallPart(PartClass, TEXT("PreservedMount")));
	TestTrue(TEXT("Rejected replacement keeps the installed part"), Ship->GetPartAt(TEXT("PreservedMount")) == Previous);
	Defaults->SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assets/Ships/ShipComponent/Rigged/SKM_SC_CIWS.SKM_SC_CIWS"));
	BlueprintClass->UpdateCustomPropertyListForPostConstruction();
	TestTrue(TEXT("Configured skeletal replacement installs"), Ship->InstallPart(PartClass, TEXT("PreservedMount")));
	UGuLiStrikeShipPartComponent* Replacement = Ship->GetPartAt(TEXT("PreservedMount"));
	TestTrue(TEXT("Replacement uses a skeletal visual"), Replacement && Replacement->GetSkeletalVisualComponent());
	TestTrue(TEXT("Original socket still exists"), Hull->DoesSocketExist(TEXT("PreservedMount")));
	TestTrue(TEXT("Original uninstall API cleans up the skeletal part"), Ship->UninstallPart(TEXT("PreservedMount")));
	TestNull(TEXT("Installation registry is empty"), Ship->GetPartAt(TEXT("PreservedMount")));
	return true;
}
#endif
