#include "Gameplay/Presentation/GuLiWarMachineHoverPreviewComponent.h"
#include "Gameplay/Presentation/GuLiWarMachineHoverComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> HoverPreview(TEXT("gs.Commander.HoverPreview"), 1,
		TEXT("Isolated WarMachine samples in the Mass test map. Set 0 for the 500-unit performance comparison."));
}

UGuLiWarMachineHoverPreviewComponent::UGuLiWarMachineHoverPreviewComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false);
}

void UGuLiWarMachineHoverPreviewComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) { SetComponentTickEnabled(false); return; }
	Models = GetOwner()->FindComponentByClass<UInstancedStaticMeshComponent>();
	if (!Models || (Models->GetInstanceCount() != 7 && Models->GetInstanceCount() != 13)) { SetComponentTickEnabled(false); return; }
	Config = FGuLiMechanicalAnimationConfig::FromStaticMesh(Models->GetStaticMesh(), .2f);
	Models->SetNumCustomDataFloats(FGuLiMechanicalAnimationFrame::CustomDataFloats);
	for (int32 I = 0; I < Models->GetInstanceCount(); ++I)
	{
		auto& S = Samples.AddDefaulted_GetRef();
		Models->GetInstanceTransform(I, S.Origin, true);
		S.Origin.SetScale3D(FVector::OneVector); S.Previous = S.Origin;
	}
	Effects = NewObject<UGuLiWarMachineHoverComponent>(GetOwner());
	Effects->RegisterComponent();
}

void UGuLiWarMachineHoverPreviewComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	if (!Models || !Effects || Dt <= 0) return;
	const bool bEnabled = HoverPreview.GetValueOnGameThread() != 0;
	Models->SetVisibility(bEnabled);
	if (!bEnabled) { Effects->Reset(); return; }
	PreviewSeconds += Dt;
	FVector Camera; FRotator View;
	if (const auto* PC = GetWorld()->GetFirstPlayerController()) PC->GetPlayerViewPoint(Camera, View);
	else Camera = GetOwner()->GetActorLocation();
	Effects->BeginFrame(GetWorld()->GetTimeSeconds(), Camera);
	for (int32 I = 0; I < Samples.Num(); ++I)
	{
		auto& Sample = Samples[I]; auto& S = Sample.State;
		const int32 Stage = int32(PreviewSeconds / 2) % 5;
		const bool bLifecycle = I == 6;
		const bool bFrozen = bLifecycle && (Stage == 2 || Stage == 4);
		const bool bReset = Sample.LastStage < 0 || (bLifecycle && Stage != Sample.LastStage && (Stage == 0 || Stage == 3));
		float Speed = I == 3 ? 720 : I == 4 ? 1440 : I == 5 && Stage % 2 == 1 ? 720 : 0;
		Sample.Travel += Speed * Dt;
		const float Cycle = FMath::Fmod(Sample.Travel, 3600.0f);
		FTransform Root = Sample.Origin;
		Root.AddToTranslation(FVector(Cycle <= 1800 ? Cycle : 3600-Cycle, 0, 0));
		if (bLifecycle && Stage >= 3) Root.AddToTranslation(FVector(1800, 0, 0));
		float DesiredYaw = I == 2 ? float(PreviewSeconds * 45) : Cycle > 1800 ? 180 : 0;
		const int32 TurnStage = int32(PreviewSeconds / 3) % 4;
		if (I == 7) DesiredYaw = TurnStage % 2 ? 90 : 0;
		if (I == 8) DesiredYaw = TurnStage % 2 ? -90 : 0;
		if (I == 9)
		{
			const float WrapTargets[] = {0, 180, 179, -179, 0};
			DesiredYaw = WrapTargets[int32(PreviewSeconds / 3) % UE_ARRAY_COUNT(WrapTargets)];
		}
		if (I == 10) DesiredYaw = int32(PreviewSeconds / .75) % 2 ? -90 : 90;
		if (I == 11)
		{
			const float Phase = PreviewSeconds * .5;
			Root.AddToTranslation(FVector(1200*FMath::Sin(Phase), 1200*(1-FMath::Cos(Phase)), 0));
			DesiredYaw = FMath::RadiansToDegrees(Phase);
		}
		if (I == 12) DesiredYaw = TurnStage % 2 ? 90 : -90;
		// New angle-step samples expose the damping response directly; live units keep authority turn limits.
		Root.SetRotation(FRotator(0, I >= 7 ? DesiredYaw : FMath::FixedTurn(Sample.Previous.Rotator().Yaw, DesiredYaw, 90*Dt), 0).Quaternion());
		const float ActualSpeed = bReset ? 0 : (Root.GetLocation() - Sample.Previous.GetLocation()).Size2D()/Dt;
		auto PreviousFrame = Sample.Frame;
		if (!bFrozen)
		{
			GuLiMechanicalAnimation::StepHover(Config, 100000+I, ActualSpeed, Dt, PreviewSeconds, S);
			const FVector Target = I == 12 ? Sample.Origin.GetLocation() + FVector(15000, 0, 500)
				: Root.GetLocation() + FVector(15000, 2500*FMath::Sin(PreviewSeconds*.5), 500);
			GuLiMechanicalAnimation::StepAim(Config, Root, I == 1 || I == 2 || I == 12 ? &Target : nullptr, I == 4 ? 1440 : 720, Dt, S);
			const auto BeforeVisual = Sample.Visual.bInitialized ? Sample.Visual.Root : Root;
			GuLiMechanicalAnimation::StepVisualTurn(Config, S, Root, PreviewSeconds, bReset, Sample.Visual);
			GuLiMechanicalAnimation::StepLocomotion(Config, BeforeVisual, Sample.Visual.Root, Dt, bReset, S);
			if ((I == 1 || I == 12) && PreviewSeconds - Sample.LastShot >= .14)
			{
				GuLiMechanicalAnimation::AcceptShot(Config, S, Sample.ShotSide, PreviewSeconds);
				Sample.ShotSide ^= 1; Sample.LastShot = PreviewSeconds;
			}
			Sample.Frame = GuLiMechanicalAnimation::BuildVisualFrame(Config, S, Sample.Visual, PreviewSeconds);
			Sample.Frame.MissilePodVisible = I == 12 ? 1 : 0;
		}
		if (bReset) PreviousFrame = Sample.Frame;
		auto Render = Sample.Visual.bInitialized ? Sample.Visual.Root : Root; Render.SetScale3D(FVector(.2f));
		Models->UpdateInstanceTransform(I, Render, true, false, bReset);
		GuLiMechanicalAnimation::WriteInstance(*Models, I, Sample.Frame, PreviousFrame);
		if (!bFrozen) for (int32 Disc = 0; Disc < 4; ++Disc)
		{
			FTransform Nozzle;
			if (!GuLiMechanicalAnimation::ResolveVisualNozzle(Config, S, Sample.Visual, Disc, Nozzle)) continue;
			FGuLiHoverNozzleSource Source;
			Source.UnitId = 100000 + I; Source.Disc = Disc; Source.Position = Nozzle.GetLocation(); Source.Direction = Nozzle.GetUnitAxis(EAxis::X);
			Source.DiscDiameter = Config.DiscDiameters[Disc]; Source.HorizontalSpeed = ActualSpeed; Source.bReset = bReset;
			Effects->Submit(Source);
		}
		Sample.Previous = Root; Sample.LastStage = Stage;
	}
	Models->MarkRenderStateDirty();
	Effects->EndFrame();
}
