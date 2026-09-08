#include "Gameplay/Skills/GuLiWeaponChannelTypes.h"

bool FGuLiWeaponBindingKey::IsWellFormed() const
{
	return MatchEpoch != 0u && (Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue)
		&& static_cast<uint8>(Domain) <= static_cast<uint8>(EGuLiWeaponDomain::ShipMounted)
		&& !SubjectId.IsNone() && !SlotId.IsNone()
		&& (Domain == EGuLiWeaponDomain::Army ? !OwnerPlayerGuid.IsValid() : OwnerPlayerGuid.IsValid());
}

bool FGuLiWeaponBindingKey::operator==(const FGuLiWeaponBindingKey& Other) const
{
	return MatchEpoch == Other.MatchEpoch && Team == Other.Team && OwnerPlayerGuid == Other.OwnerPlayerGuid
		&& Domain == Other.Domain && SubjectId == Other.SubjectId && SlotId == Other.SlotId;
}

FGuLiWeaponBindingKey FGuLiWeaponBindingKey::Army(const uint32 Epoch, const EGuLiTeam InTeam,
	const uint16 UnitTypeId, const FName WeaponSlot)
{
	FGuLiWeaponBindingKey Key;
	Key.MatchEpoch = Epoch; Key.Team = InTeam;
	Key.SubjectId = FName(*FString::FromInt(UnitTypeId)); Key.SlotId = WeaponSlot;
	return Key;
}

FGuLiWeaponBindingKey FGuLiWeaponBindingKey::Wingman(
	const uint32 Epoch,
	const EGuLiTeam InTeam,
	const FGuid& InOwnerPlayerGuid,
	const FName WingmanTypeId,
	const FName WeaponSlot)
{
	FGuLiWeaponBindingKey Key;
	Key.MatchEpoch = Epoch;
	Key.Team = InTeam;
	Key.OwnerPlayerGuid = InOwnerPlayerGuid;
	Key.Domain = EGuLiWeaponDomain::Wingman;
	Key.SubjectId = WingmanTypeId;
	Key.SlotId = WeaponSlot;
	return Key;
}

uint32 GetTypeHash(const FGuLiWeaponBindingKey& Key)
{
	uint32 Hash = HashCombine(GetTypeHash(Key.MatchEpoch), GetTypeHash(static_cast<uint8>(Key.Team)));
	Hash = HashCombine(Hash, GetTypeHash(Key.OwnerPlayerGuid));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Key.Domain)));
	Hash = HashCombine(Hash, GetTypeHash(Key.SubjectId));
	return HashCombine(Hash, GetTypeHash(Key.SlotId));
}
