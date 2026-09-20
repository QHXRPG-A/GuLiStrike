#include "Gameplay/GroundMech/GuLiGroundMechMovementNetwork.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"

void FGuLiGroundMechStorageDeleter::operator()(FGuLiGroundMechNetworkStorage *Storage) const
{
	delete Storage;
}

void FGuLiMassSupportState::Serialize(FArchive &Ar)
{
	if (Ar.IsLoading())
		bHasSourceReference = false;
	Ar << SoldierId.Value << Epoch << DisplacementRevision;
	if (SoldierId.IsValid())
	{
		Ar << BodyLocation << RelativeLocation << TopZ << Radius << SimulationSeconds;
		if (Ar.IsLoading() && (BodyLocation.ContainsNaN() || RelativeLocation.ContainsNaN() || !FMath::IsFinite(TopZ) ||
							   !FMath::IsFinite(Radius) || Radius <= 0.0f || !FMath::IsFinite(SimulationSeconds)))
			Ar.SetError();
	}
	else if (Ar.IsLoading())
		*this = {};
}

void FGuLiGroundMechSavedMove::Clear()
{
	FGuLiExternalSavedMove::Clear();
	Context = {};
	StartSupport = {};
	EndSupport = {};
	bContact = false;
}
void FGuLiGroundMechSavedMove::SetMoveFor(ACharacter *Character, float Delta, const FVector &Accel,
										  FNetworkPredictionData_Client_Character &Data)
{
	FGuLiExternalSavedMove::SetMoveFor(Character, Delta, Accel, Data);
	auto *Movement = CastChecked<UGuLiGroundMechMovementComponent>(Character->GetCharacterMovement());
	Context = Movement->CaptureCollisionMove(Delta);
	StartSupport = Movement->SupportState;
	Movement->PreparedMove = Context;
	Movement->bPreparedMove = true;
	Movement->bReplayingMove = false;
}
void FGuLiGroundMechSavedMove::PostUpdate(ACharacter *Character, EPostUpdateMode Mode)
{
	FGuLiExternalSavedMove::PostUpdate(Character, Mode);
	const auto *Movement = CastChecked<UGuLiGroundMechMovementComponent>(Character->GetCharacterMovement());
	Context = Movement->LastMoveContext;
	EndSupport = Movement->SupportState;
	bContact = Movement->bTouchedMass;
}
void FGuLiGroundMechSavedMove::PrepMoveFor(ACharacter *Character)
{
	FGuLiExternalSavedMove::PrepMoveFor(Character);
	auto *Movement = CastChecked<UGuLiGroundMechMovementComponent>(Character->GetCharacterMovement());
	Movement->PreparedMove = Context;
	Movement->bPreparedMove = true;
	Movement->bReplayingMove = true;
	// The first replay starts from the authoritative correction, subsequent moves from the previous replay.
	// Restoring StartSupport here would overwrite the correction and carry the platform twice.
}
bool FGuLiGroundMechSavedMove::CanCombineWith(const FSavedMovePtr &Move, ACharacter *Character, float MaxDelta) const
{
	const auto &Next = static_cast<const FGuLiGroundMechSavedMove &>(*Move);
	return !bContact && !Next.bContact && !StartSupport.IsValid() && !EndSupport.IsValid() &&
		   !Next.StartSupport.IsValid() && !Next.EndSupport.IsValid() && Context.Snapshot == Next.Context.Snapshot &&
		   FGuLiExternalSavedMove::CanCombineWith(Move, Character, MaxDelta);
}
void FGuLiGroundMechSavedMove::CombineWith(const FSavedMove_Character *Old, ACharacter *Character,
										   APlayerController *PC, const FVector &OldStart)
{
	FGuLiExternalSavedMove::CombineWith(Old, Character, PC, OldStart);
	const auto &Previous = static_cast<const FGuLiGroundMechSavedMove &>(*Old);
	Context = Previous.Context;
	Context.Duration = DeltaTime;
	StartSupport = Previous.StartSupport;
	auto *Movement = CastChecked<UGuLiGroundMechMovementComponent>(Character->GetCharacterMovement());
	Movement->PreparedMove = Context;
	Movement->bPreparedMove = true;
}
void FGuLiGroundMechMoveData::ClientFillNetworkMoveData(const FSavedMove_Character &Move, ENetworkMoveType Type)
{
	FGuLiExternalMoveData::ClientFillNetworkMoveData(Move, Type);
	const auto &State = static_cast<const FGuLiGroundMechSavedMove &>(Move).EndSupport;
	SupportEpoch = State.Epoch;
	SupportId = State.SoldierId.Value;
	SupportDisplacement = State.DisplacementRevision;
}
bool FGuLiGroundMechMoveData::Serialize(UCharacterMovementComponent &Movement, FArchive &Ar, UPackageMap *Map,
										ENetworkMoveType Type)
{
	const bool bOK = FGuLiExternalMoveData::Serialize(Movement, Ar, Map, Type);
	Ar << SupportEpoch << SupportId << SupportDisplacement;
	return bOK && !Ar.IsError();
}
void FGuLiGroundMechMoveResponse::ServerFillResponseData(const UCharacterMovementComponent &Movement,
														 const FClientAdjustment &Adjustment)
{
	FGuLiExternalMoveResponse::ServerFillResponseData(Movement, Adjustment);
	const auto &Mech = static_cast<const UGuLiGroundMechMovementComponent &>(Movement);
	// Captured together with PendingAdjustment, not from the server's newer live support state.
	if (IsCorrection())
	{
		checkf(Mech.PendingResponseTimeStamp == Adjustment.TimeStamp,
			   TEXT("Support baseline must accompany its CMC adjustment"));
		Support = Mech.PendingResponseSupport;
	}
	else
		Support = {};
}
bool FGuLiGroundMechMoveResponse::Serialize(UCharacterMovementComponent &Movement, FArchive &Ar, UPackageMap *Map)
{
	const bool bOK = FGuLiExternalMoveResponse::Serialize(Movement, Ar, Map);
	if (IsCorrection())
		Support.Serialize(Ar);
	else if (Ar.IsLoading())
		Support = {};
	return bOK && !Ar.IsError();
}
