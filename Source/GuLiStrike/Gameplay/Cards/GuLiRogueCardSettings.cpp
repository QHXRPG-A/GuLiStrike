#include "Gameplay/Cards/GuLiRogueCardSettings.h"
UGuLiRogueCardSettings::UGuLiRogueCardSettings()
{
	Cards=FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards.DT_GuLiStrikeRogueCards_Cards"));
	Texts=FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts.ST_GuLiStrikeGameTexts"));
	Candidates={TEXT("01.01"),TEXT("03.01"),TEXT("02.01")};
	DirectorClass=FSoftObjectPath(TEXT("/Game/GuLiStrike/CardSystem/RevealDemo/Blueprints/BP_CardRevealDirector.BP_CardRevealDirector_C"));
	CaptureMaterial=FSoftObjectPath(TEXT("/Game/GuLiStrike/CardSystem/RevealDemo/Materials/M_CardCaptureUI.M_CardCaptureUI"));
	FrameMaterial=FSoftObjectPath(TEXT("/Game/GuLiStrike/CardSystem/WarMachineTarot/Materials/MI_WarMachineFrame.MI_WarMachineFrame"));
}
