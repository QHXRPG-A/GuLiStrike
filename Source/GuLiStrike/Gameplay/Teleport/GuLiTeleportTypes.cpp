#include "Gameplay/Teleport/GuLiTeleportTypes.h"

bool FGuLiTeleportFieldConfig::IsValid() const
{
	return Level >= 1 && Level <= 4 && bAllowPlayerVehicles == (Level == 4)
		&& FMath::IsFinite(RadiusCentimeters) && RadiusCentimeters > 0
		&& FMath::IsFinite(WindupSeconds) && WindupSeconds > 0
		&& FMath::IsFinite(RecoverySeconds) && RecoverySeconds > 0
		&& FMath::IsFinite(BeamHeightCentimeters) && BeamHeightCentimeters > 0
		&& FMath::IsFinite(MaxTargetWaitSeconds) && MaxTargetWaitSeconds > 0
		&& FMath::IsFinite(MaxShipHeightCentimeters) && MaxShipHeightCentimeters > 0;
}

bool GuLiTeleport::IsInsideDisc(const FVector& Location, const FVector& Center, const float Radius)
{
	return !Location.ContainsNaN() && !Center.ContainsNaN() && FMath::IsFinite(Radius) && Radius > 0
		&& FVector::DistSquared2D(Location, Center) <= FMath::Square(double(Radius));
}

bool GuLiTeleport::CanCollectVehicle(const FGuLiTeleportFieldConfig& Config, const bool bShip, const double Height)
{
	return Config.IsValid() && Config.Level == 4 && Config.bAllowPlayerVehicles
		&& (!bShip || (FMath::IsFinite(Height) && Height >= 0 && Height <= Config.MaxShipHeightCentimeters));
}

float GuLiTeleport::WindupProgress(const FGuLiTeleportCastState& State, const double ServerTime)
{
	return State.Config.WindupSeconds > 0
		? float(FMath::Clamp((ServerTime - State.PhaseStartTime) / State.Config.WindupSeconds, 0.0, 1.0)) : 0.f;
}
