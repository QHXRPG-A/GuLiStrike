#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Engine/World.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	TAutoConsoleVariable<int32> UpgradeQuality(TEXT("gs.RogueCards.UpgradeQuality"),2,
		TEXT("Upgrade independent motes: 0=ring/glow only, 1=six motes, 2=eighteen motes. Clamped by sg.EffectsQuality."));
	TAutoConsoleVariable<float> UpgradeEnemyR(TEXT("gs.RogueCards.EnemyUpgradeR"),1.f,TEXT("Enemy upgrade linear red channel."));
	TAutoConsoleVariable<float> UpgradeEnemyG(TEXT("gs.RogueCards.EnemyUpgradeG"),0.f,TEXT("Enemy upgrade linear green channel."));
	TAutoConsoleVariable<float> UpgradeEnemyB(TEXT("gs.RogueCards.EnemyUpgradeB"),0.f,TEXT("Enemy upgrade linear blue channel."));
	void ReleaseUpgradeComponent(FGuLiRogueUpgradeBlock& Block)
	{
		if (IsValid(Block.Component)) { Block.Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Block.Component); Block.Component->ReleaseToPool(); }
		Block.Component=nullptr;
	}
}

void UGuLiCombatEffectPresentationSubsystem::ApplyRogueUpgrade(const FGuLiRogueUpgradeCue& Cue)
{
	if (!Cue.Session.IsValid() || !Cue.MatchEpoch || Cue.MatchEpoch<Epoch || Cue.System.IsNull()
		|| Cue.Soldiers.Num()>GuLiRogueUpgrade::NetworkBatchSize || !FMath::IsFinite(Cue.Scale) || Cue.Scale<=0
		|| !FMath::IsFinite(Cue.StartTime) || !FMath::IsFinite(Cue.Color.R) || !FMath::IsFinite(Cue.Color.G)
		|| !FMath::IsFinite(Cue.Color.B) || !FMath::IsFinite(Cue.Color.A)) return;
	const float Now=ServerTime();
	if (Now-Cue.StartTime>=GuLiRogueUpgrade::Duration || Cue.StartTime>Now+2.f) return;
	BeginEpoch(Cue.MatchEpoch);
	auto& Receipt=UpgradeReceipts.FindOrAdd(Cue.Session);
	if (Receipt.Batches.Contains(Cue.BatchIndex)) return;
	Receipt.Batches.Add(Cue.BatchIndex); Receipt.Expires=Now+5.f;
	UNiagaraSystem* System=Cue.System.LoadSynchronous();
	if (!System) { UE_LOG(LogTemp,Warning,TEXT("Rogue upgrade VFX unavailable: %s"),*Cue.System.ToString()); return; }
	for (FGuLiSoldierId Soldier : Cue.Soldiers)
	{
		if (!Soldier.IsValid()) continue;
		int32 Slot=INDEX_NONE;
		if (const int32* Existing=UpgradeSlots.Find(Soldier))
		{
			auto& OldBlock=UpgradeBlocks[*Existing/GuLiRogueUpgrade::BlockSize];
			if (OldBlock.System==System) Slot=*Existing;
			else { const int32 Local=*Existing%GuLiRogueUpgrade::BlockSize;
				OldBlock.Slots[Local].bActive=false; OldBlock.Free.Add(Local); UpgradeSlots.Remove(Soldier); }
		}
		if (Slot==INDEX_NONE)
		{
			int32 BlockIndex=UpgradeBlocks.IndexOfByPredicate([System](const auto& B) { return B.System==System && !B.Free.IsEmpty(); });
			if (BlockIndex==INDEX_NONE)
			{
				BlockIndex=UpgradeBlocks.Num(); auto& B=UpgradeBlocks.AddDefaulted_GetRef(); B.System=System;
				B.Slots.SetNum(GuLiRogueUpgrade::BlockSize);
				B.Positions.Init(FVector::ZeroVector,GuLiRogueUpgrade::BlockSize); B.Parameters=B.Positions;
				B.Colors.Init(FLinearColor::Transparent,GuLiRogueUpgrade::BlockSize);
				for (int32 I=GuLiRogueUpgrade::BlockSize-1;I>=0;--I) B.Free.Add(I);
			}
			Slot=BlockIndex*GuLiRogueUpgrade::BlockSize+UpgradeBlocks[BlockIndex].Free.Pop(EAllowShrinking::No);
			UpgradeSlots.Add(Soldier,Slot);
		}
		auto& S=UpgradeBlocks[Slot/GuLiRogueUpgrade::BlockSize].Slots[Slot%GuLiRogueUpgrade::BlockSize];
		S.Soldier=Soldier; S.Team=Cue.Team; S.UnitTypeId=Cue.UnitTypeId; S.StartTime=Cue.StartTime;
		S.Scale=Cue.Scale; S.Color=Cue.Color; S.bActive=true;
	}
}

void UGuLiCombatEffectPresentationSubsystem::ResetRogueUpgradePool()
{
	for (auto& B : UpgradeBlocks) ReleaseUpgradeComponent(B);
	UpgradeBlocks.Reset(); UpgradeSlots.Reset(); UpgradeReceipts.Reset(); UpgradeRoster.Reset();
}

void UGuLiCombatEffectPresentationSubsystem::UpdateRogueUpgradePool(float Now,bool bEnabled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiRogueUpgradePresentation);
	const double Started=FPlatformTime::Seconds();
	Counters.UpgradeActive=0; Counters.UpgradeVisible=0; Counters.UpgradeComponents=0; Counters.UpgradeVisibleParticles=0;
	for (auto It=UpgradeReceipts.CreateIterator(); It; ++It) if (It.Value().Expires<Now) It.RemoveCurrent();
	if (UpgradeSlots.IsEmpty())
	{
		for (auto& B : UpgradeBlocks) ReleaseUpgradeComponent(B);
		// Slots and asset references do not accumulate across cards; Niagara's bounded pool owns reusable components.
		UpgradeBlocks.Reset(); Counters.UpgradeUpdateMilliseconds=0; return;
	}
	if (!UpgradeRoster.IsValid()) for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld());It;++It) { UpgradeRoster=*It; break; }
	APlayerController* Viewer=nullptr; EGuLiTeam LocalTeam=EGuLiTeam::Unassigned;
	for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
		if (auto* PC=It->Get();PC && PC->IsLocalController()) { Viewer=PC;
			if (auto* PS=PC->GetPlayerState<AGuLiBattlePlayerState>()) LocalTeam=PS->GetTeam(); break; }
	int32 Width=0,Height=0; if (Viewer) Viewer->GetViewportSize(Width,Height);
	const auto* EffectsQuality=IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
	const int32 Quality=FMath::Clamp(FMath::Min(UpgradeQuality.GetValueOnGameThread(),EffectsQuality?EffectsQuality->GetInt():2),0,2);
	const int32 Motes=Quality==2 ? 18 : Quality==1 ? 6 : 0;
	const auto* Roster=UpgradeRoster.Get();
	for (auto& B : UpgradeBlocks)
	{
		B.Colors.Init(FLinearColor::Transparent,GuLiRogueUpgrade::BlockSize);
		B.Parameters.Init(FVector::ZeroVector,GuLiRogueUpgrade::BlockSize);
		FBox Bounds(ForceInit); float MaxScale=1.f; bool bVisible=false;
		for (int32 I=0;I<B.Slots.Num();++I)
		{
			auto& S=B.Slots[I]; if (!S.bActive) continue;
			const auto* State=Roster ? Roster->FindSoldierState(S.Soldier) : nullptr;
			const float Age=Now-S.StartTime;
			if (Age>=GuLiRogueUpgrade::Duration || (State && (!State->IsAlive() || State->Team!=S.Team || State->UnitTypeId!=S.UnitTypeId)))
			{ UpgradeSlots.Remove(S.Soldier); S.bActive=false; B.Free.Add(I); continue; }
			++Counters.UpgradeActive;
			if (!bEnabled || !Viewer || !State || State->bPhased || Roster->GetSnapshotMatchEpoch()!=Epoch) continue;
			FTransform Pose; int32 UnitType=0;
			if (!ResolvePose(GuLiCombatTargets::MakeCommanderSoldierTargetHandle(Epoch,S.Soldier.Value),Pose,UnitType) || UnitType!=S.UnitTypeId) continue;
			const FVector Position=Pose.GetLocation(); FVector2D Screen;
			// Screen-space culling supports the commander's entire zoom range, not a 200m camera-height cutoff.
			if (!Viewer->ProjectWorldLocationToScreen(Position,Screen) || Screen.X < -128 || Screen.Y < -128
				|| Screen.X>Width+128 || Screen.Y>Height+128) continue;
			FLinearColor Color=S.Color;
			if (LocalTeam!=EGuLiTeam::Unassigned && S.Team!=LocalTeam)
			{
				const float Peak=FMath::Max3(Color.R,Color.G,Color.B);
				Color.R=Peak*FMath::Clamp(UpgradeEnemyR.GetValueOnGameThread(),0.f,1.f);
				Color.G=Peak*FMath::Clamp(UpgradeEnemyG.GetValueOnGameThread(),0.f,1.f);
				Color.B=Peak*FMath::Clamp(UpgradeEnemyB.GetValueOnGameThread(),0.f,1.f);
			}
			B.Positions[I]=Position;
			B.Parameters[I]=FVector(FMath::Max(Age,0.f),S.Scale,Motes); B.Colors[I]=Color;
			Bounds+=Position; MaxScale=FMath::Max(MaxScale,S.Scale); bVisible=true;
			++Counters.UpgradeVisible; Counters.UpgradeVisibleParticles+=2+Motes;
		}
		if (!bVisible) { ReleaseUpgradeComponent(B); continue; }
		bool bActivate=false;
		if (!IsValid(B.Component))
		{
			B.Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),B.System,FVector::ZeroVector,
				FRotator::ZeroRotator,FVector::OneVector,false,false,ENCPoolMethod::ManualRelease,false);
			if (!B.Component) continue;
			if (auto* Overview = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(B.Component);
			B.Component->SetCastShadow(false); bActivate=true;
		}
		using Arrays=UNiagaraDataInterfaceArrayFunctionLibrary;
		Arrays::SetNiagaraArrayPosition(B.Component,TEXT("User.UpgradePositions"),B.Positions);
		Arrays::SetNiagaraArrayVector(B.Component,TEXT("User.UpgradeParameters"),B.Parameters);
		Arrays::SetNiagaraArrayColor(B.Component,TEXT("User.UpgradeColors"),B.Colors);
		B.Component->SetSystemFixedBounds(Bounds.ExpandBy(500.f*MaxScale));
		if (bActivate) B.Component->Activate(true);
		++Counters.UpgradeComponents;
	}
	Counters.UpgradeUpdateMilliseconds=(FPlatformTime::Seconds()-Started)*1000.;
}
