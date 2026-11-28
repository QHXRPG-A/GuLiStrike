#include "Gameplay/Ship/Capabilities/GuLiXuntianBellyBattery.h"
#include "Misc/Crc.h"

bool UGuLiXuntianBellyBattery::ValidateConfiguration(const UDataAsset* Config, FString& Error) const
{
    const auto* Shell = Cast<UGuLiXuntianShellDefinition>(Config);
    if (Shell && Shell->Level >= 1 && Shell->Level <= 3 && !Shell->FieldRow.IsNone() &&
        Shell->RoundSeconds > (Shell->Level-1)*Shell->BurstInterval && Shell->BurstInterval > 0 &&
        Shell->Motion.IsValid() && !Shell->ImpactField.IsNull() && !Shell->GroundWarningStyle.IsNull()) return true;
    Error = TEXT("Invalid Xuntian belly battery configuration.");
    return false;
}

uint64 UGuLiXuntianBellyBattery::GetConfigurationChecksum(const UDataAsset* Config) const
{
    const auto* Shell = Cast<UGuLiXuntianShellDefinition>(Config);
    if (!Shell) return 0;
    return FCrc::StrCrc32(*FString::Printf(TEXT("%s/%s/%d/%.9g/%.9g/%.9g/%.9g"), *Shell->GetPathName(),
        *Shell->FieldRow.ToString(), Shell->Level, Shell->RoundSeconds, Shell->BurstInterval, Shell->Motion.Speed, Shell->Motion.SweepRadius));
}

// The gunner owns each socket's pose, cooldown and unfinished burst in the ship checkpoint.
bool UGuLiXuntianBellyBattery::CapturePrivateState(TArray<uint8>& Bytes) const
{
    if (!GetShell()) return false;
    Bytes = {uint8(GetShell()->Level)};
    return true;
}
bool UGuLiXuntianBellyBattery::ValidatePrivateState(TConstArrayView<uint8> Bytes, const UDataAsset* Config) const
{
    const auto* Shell = Cast<UGuLiXuntianShellDefinition>(Config);
    return Shell && Bytes.Num() == 1 && Bytes[0] == Shell->Level;
}
bool UGuLiXuntianBellyBattery::RestorePrivateState(TConstArrayView<uint8> Bytes)
{
    return ValidatePrivateState(Bytes, CapabilityConfiguration);
}
