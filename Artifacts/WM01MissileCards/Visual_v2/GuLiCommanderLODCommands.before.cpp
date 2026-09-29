#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"

#if !UE_BUILD_SHIPPING
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace GuLiCommanderLODCommands
{
	UGuLiCommanderLODSubsystem* Resolve(UWorld* World)
	{
		auto* LOD = World ? World->GetSubsystem<UGuLiCommanderLODSubsystem>() : nullptr;
		if (!LOD) UE_LOG(LogTemp, Warning, TEXT("gs.Commander.LOD requires a local Game/PIE World; unavailable on dedicated servers."));
		return LOD;
	}
	void PrintSetting(const FGuLiCommanderLODSettingView& Value)
	{
		UE_LOG(LogTemp, Display, TEXT("gs.Commander.LOD key=%s baseline=%.6g effective=%.6g source=%s"),
			*Value.Key, Value.Baseline, Value.Effective, *Value.Source);
	}
	FAutoConsoleCommandWithWorldAndArgs List(TEXT("gs.Commander.LOD.List"), TEXT("List shared commander LOD settings: [prefix]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() > 1) { UE_LOG(LogTemp, Warning, TEXT("Usage: gs.Commander.LOD.List [prefix]")); return; }
			if (const auto* LOD = Resolve(World)) for (const auto& Value : LOD->ListSettings(Args.IsEmpty() ? FString() : Args[0])) PrintSetting(Value);
		}));
	FAutoConsoleCommandWithWorldAndArgs Get(TEXT("gs.Commander.LOD.Get"), TEXT("Read a shared commander LOD setting: <key>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() != 1) { UE_LOG(LogTemp, Warning, TEXT("Usage: gs.Commander.LOD.Get <key>")); return; }
			if (const auto* LOD = Resolve(World))
			{
				for (const auto& Value : LOD->ListSettings()) if (Value.Key.Equals(Args[0], ESearchCase::IgnoreCase)) { PrintSetting(Value); return; }
				UE_LOG(LogTemp, Warning, TEXT("Unknown LOD key: %s"), *Args[0]);
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs Set(TEXT("gs.Commander.LOD.Set"), TEXT("Set a World-local commander LOD override: <key> <value>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() != 2) { UE_LOG(LogTemp, Warning, TEXT("Usage: gs.Commander.LOD.Set <key> <value>")); return; }
			if (auto* LOD = Resolve(World))
			{
				FString Error;
				if (!LOD->SetSetting(Args[0], Args[1], Error)) UE_LOG(LogTemp, Warning, TEXT("LOD override rejected: %s"), *Error);
				else for (const auto& Value : LOD->ListSettings()) if (Value.Key.Equals(Args[0], ESearchCase::IgnoreCase)) PrintSetting(Value);
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs Reset(TEXT("gs.Commander.LOD.Reset"), TEXT("Restore configuration baseline: <key|all>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() != 1) { UE_LOG(LogTemp, Warning, TEXT("Usage: gs.Commander.LOD.Reset <key|all>")); return; }
			if (auto* LOD = Resolve(World))
			{
				FString Error;
				if (!LOD->ResetSetting(Args[0], Error)) UE_LOG(LogTemp, Warning, TEXT("LOD reset rejected: %s"), *Error);
				else for (const auto& Value : LOD->ListSettings())
					if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase) || Value.Key.Equals(Args[0], ESearchCase::IgnoreCase)) PrintSetting(Value);
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs Stats(TEXT("gs.Commander.LOD.Stats"), TEXT("Read per-consumer target/applied LOD counts: [consumer]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() > 1) { UE_LOG(LogTemp, Warning, TEXT("Usage: gs.Commander.LOD.Stats [consumer]")); return; }
			const auto* LOD = Resolve(World);
			if (!LOD) return;
			UE_LOG(LogTemp, Display, TEXT("Commander LOD views=%d queries=%d view_update_ms=%.6f view_frame=%llu current_frame=%llu (latest sampled frame; resource budgets are consumer-local)"),
				LOD->GetLocalViewCount(), LOD->GetQueryCount(), LOD->GetViewUpdateMilliseconds(), LOD->GetViewFrame(), GFrameCounter);
			int32 Matched = 0;
			for (const auto& Entry : LOD->GetConsumerStats())
			{
				if (!Args.IsEmpty() && !Entry.Key.ToString().Equals(Args[0], ESearchCase::IgnoreCase)) continue;
				++Matched;
				const auto& S = Entry.Value;
				UE_LOG(LogTemp, Display, TEXT("consumer=%s objects=%d visible=%d target[0/1/2]=%d/%d/%d applied[0/1/2]=%d/%d/%d culled[no_view/frustum/invalid/distance/screen]=%d/%d/%d/%d/%d budget_limited=%d transition_pending=%d unavailable=%d age_frames=%llu"),
					*Entry.Key.ToString(), S.Objects, S.Visible, S.Target[0], S.Target[1], S.Target[2], S.Applied[0], S.Applied[1], S.Applied[2],
					S.NoView, S.FrustumCulled, S.InvalidBounds, S.DistanceCulled, S.ScreenCulled, S.BudgetDowngraded,
					S.TransitionPending, S.ResourcesUnavailable, GFrameCounter - S.ReportFrame);
			}
			if (!Matched) UE_LOG(LogTemp, Display, TEXT("No active consumer reports. WM01MissileBatches reports only when the missile pool runs."));
		}));
}
#endif
