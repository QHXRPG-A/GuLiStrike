#include "Commander/Presentation/GuLiCommanderLODPolicy.h"

bool FGuLiCommanderLODSettings::Validate(FString& Error) const
{
	const float Values[] = {NearEnterDistance, NearHoldDistance, NearEnterScreen, NearHoldScreen,
		MidEnterDistance, MidHoldDistance, MidEnterScreen, MidHoldScreen, MinimumResidenceSeconds};
	for (const float Value : Values)
	{
		if (!FMath::IsFinite(Value) || Value < 0)
		{
			Error = TEXT("LOD values must be finite and non-negative.");
			return false;
		}
	}
	if (!(0 < NearEnterDistance && NearEnterDistance < NearHoldDistance
		&& NearHoldDistance < MidEnterDistance && MidEnterDistance < MidHoldDistance
		&& MidHoldDistance <= 10000000.f))
	{
		Error = TEXT("Distances require 0 < near.enter < near.hold < mid.enter < mid.hold <= 10000000 cm.");
		return false;
	}
	if (!(MidHoldScreen < MidEnterScreen && MidEnterScreen < NearHoldScreen
		&& NearHoldScreen < NearEnterScreen && NearEnterScreen <= 1.f))
	{
		Error = TEXT("Screen thresholds require 0 <= mid.hold < mid.enter < near.hold < near.enter <= 1.");
		return false;
	}
	if (MinimumResidenceSeconds > 10.f)
	{
		Error = TEXT("minimum_residence_seconds must be in [0, 10].");
		return false;
	}
	Error.Reset();
	return true;
}

EGuLiCommanderLODLevel FGuLiCommanderLODSettings::Classify(const double Distance, const float Screen,
	const TOptional<EGuLiCommanderLODLevel> CurrentLevel) const
{
	const bool bWasFull = CurrentLevel.IsSet() && CurrentLevel.GetValue() == EGuLiCommanderLODLevel::Full;
	const bool bWasDetailed = CurrentLevel.IsSet() && CurrentLevel.GetValue() != EGuLiCommanderLODLevel::Minimal;
	if (Distance < (bWasFull ? NearHoldDistance : NearEnterDistance)
		&& Screen > (bWasFull ? NearHoldScreen : NearEnterScreen)) return EGuLiCommanderLODLevel::Full;
	if (Distance < (bWasDetailed ? MidHoldDistance : MidEnterDistance)
		&& Screen > (bWasDetailed ? MidHoldScreen : MidEnterScreen)) return EGuLiCommanderLODLevel::Reduced;
	return EGuLiCommanderLODLevel::Minimal;
}
