#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "UObject/CoreNet.h"

bool FGuLiFlightEvent::Serialize(FArchive& Ar)
{
	bool Success = true;
	State.NetSerialize(Ar, nullptr, Success);
	if (!Success) { Ar.SetError(); return false; }
	Ar.SerializeBits(&bBootstrap, 1);
	Ar.SerializeBits(&bHasMuzzle, 1);
	if (bHasMuzzle)
	{
		// Provenance, shot ID, launch point/direction and clock already exist in
		// State. Only animation/audio attachment metadata is added to a launch.
		bool Basic = Muzzle.SlotId==TEXT("BasicAttack"); Ar.SerializeBits(&Basic,1);
		if (Basic) { if (Ar.IsLoading()) Muzzle.SlotId=TEXT("BasicAttack"); } else Ar << Muzzle.SlotId;
		uint32 Type = Muzzle.UnitTypeId; Ar.SerializeIntPacked(Type);
		Ar << Muzzle.MuzzleIndex << Muzzle.KeepAliveSeconds;
		Ar.SerializeBits(&Muzzle.bMechanicalShot,1);
		if (State.Kind!=EGuLiCombatEffectKind::LinearProjectile)
		{
			FVector_NetQuantize Offset(Muzzle.MuzzleOffset); Offset.NetSerialize(Ar,nullptr,Success);
			if (Ar.IsLoading()) Muzzle.MuzzleOffset=Offset;
		}
		if (Muzzle.bMechanicalShot)
		{
			Muzzle.MuzzleDirection.NetSerialize(Ar,nullptr,Success);
			Ar << Muzzle.RecoilFromCentimeters << Muzzle.MechanicalPoseTimeSeconds;
		}
		if (Type>MAX_uint16 || !Success || !FMath::IsFinite(Muzzle.KeepAliveSeconds)
			|| !FMath::IsFinite(Muzzle.RecoilFromCentimeters) || !FMath::IsFinite(Muzzle.MechanicalPoseTimeSeconds)
			|| (Muzzle.bMechanicalShot && (Muzzle.MuzzleIndex>1 || Muzzle.RecoilFromCentimeters<0
				|| Muzzle.RecoilFromCentimeters>100 || Muzzle.MechanicalPoseTimeSeconds<0))) Ar.SetError();
		if (Ar.IsLoading())
		{
			Muzzle.MatchEpoch=State.MatchEpoch; Muzzle.ShotId=State.EffectId; Muzzle.Source=State.Source; Muzzle.Target=State.Target;
			Muzzle.UnitTypeId=static_cast<uint16>(Type); Muzzle.Start=State.LaunchLocation; Muzzle.ServerTime=State.StartTime;
			if (!Muzzle.bMechanicalShot) Muzzle.MuzzleDirection=State.LaunchDirection;
			Muzzle.bMuzzleOnly=true;
			Muzzle.End=State.Target.IsValid() ? FVector(State.LastTargetLocation) : FVector(State.LaunchLocation)+FVector(State.LaunchDirection)*State.Motion.Speed;
			if (State.Kind==EGuLiCombatEffectKind::LinearProjectile) Muzzle.MuzzleOffset=State.MuzzleOffset;
		}
	}
	if (State.Phase==EGuLiCombatEffectPhase::Finished)
	{
		Ar.SerializeBits(&bUseCatalogImpact,1);
		uint32 Vfx = static_cast<uint32>(ImpactVfxId); Ar.SerializeIntPacked(Vfx);
		ImpactNormal.NetSerialize(Ar,nullptr,Success);
		if (Ar.IsLoading()) ImpactVfxId = static_cast<int32>(Vfx);
		if (Vfx>MAX_int32 || !Success) Ar.SetError();
	}
	// Preserve the record layout for current logical flight producers.
	bool Reserved = false;
	Ar.SerializeBits(&Reserved, 1);
	if (Reserved) Ar.SetError();
	return !Ar.IsError();
}

bool GuLiFlightWire::EncodeRecord(const FGuLiFlightEvent& Event, TArray<uint8>& Bytes, uint16& Bits)
{
	FNetBitWriter Writer((MaximumBytes - 5) * 8); Writer.SetAllowResize(false);
	FGuLiFlightEvent Copy = Event;
	if (!Copy.Serialize(Writer) || Writer.IsError()) return false;
	Bits = static_cast<uint16>(Writer.GetNumBits());
	Bytes.Reset(); Bytes.Append(Writer.GetData(), Writer.GetNumBytes());
	return true;
}

int32 GuLiFlightWire::EncodeBatch(TConstArrayView<FGuLiFlightEvent> Events, TArray<uint8>& Payload)
{
	Payload.Reset(); Payload.Add(0);
	for (int32 Index = 0; Index < FMath::Min(Events.Num(), MaximumRecords); ++Index)
	{
		TArray<uint8> Record; uint16 Bits = 0;
		if (!EncodeRecord(Events[Index], Record, Bits) || Payload.Num() + 2 + Record.Num() > MaximumBytes) break;
		Payload.Add(static_cast<uint8>(Bits)); Payload.Add(static_cast<uint8>(Bits >> 8));
		Payload.Append(Record); ++Payload[0];
	}
	return Payload[0];
}

bool GuLiFlightWire::DecodeBatch(const TArray<uint8>& Payload, TArray<FGuLiFlightEvent>& Events)
{
	Events.Reset();
	if (Payload.IsEmpty() || Payload.Num() > MaximumBytes || !Payload[0] || Payload[0] > MaximumRecords) return false;
	int32 Cursor = 1;
	for (int32 Index = 0; Index < Payload[0]; ++Index)
	{
		if (Cursor + 2 > Payload.Num()) return false;
		const uint16 Bits = Payload[Cursor] | (Payload[Cursor+1] << 8); Cursor += 2;
		const int32 Bytes = (Bits + 7) / 8;
		if (!Bits || Cursor + Bytes > Payload.Num()) return false;
		FNetBitReader Reader(nullptr,Payload.GetData()+Cursor, Bits);
		auto& Event = Events.AddDefaulted_GetRef();
		if (!Event.Serialize(Reader) || Reader.IsError() || Reader.GetPosBits() != Bits) return false;
		Cursor += Bytes;
	}
	return Cursor == Payload.Num();
}
