#pragma once
#include "GuLiNetCapability.h"

// Gameplay capability. Neither the transport nor Room knows about card IDs,
// unit models, weapon slots, or the contents of this catalog.
namespace GuLiNet::Army
{
inline constexpr const char* StateCapability="army_state";
inline constexpr const char* LinkCapability="army_rules";
inline constexpr std::uint32_t Version=1;
struct Slot
{
    std::uint16_t Unit=0;std::string Name;
    auto operator<=>(const Slot&) const=default;
};
struct Selector
{
    std::vector<std::uint16_t> Units;
    std::string SlotName,Skill;
    std::vector<std::string> Tags;
};
struct Modifier
{
    Selector Target;
    std::uint8_t Attribute=0,Operation=0;
    float Magnitude=0;std::int32_t Integer=0;
};
struct Replacement {Selector Target;std::string Skill;std::int32_t Priority=0;};
struct Source
{
    Id Identity;
    std::vector<Modifier> Modifiers;
    std::vector<Replacement> Replacements;
    std::vector<Selector> Unlocks;
    std::map<std::uint16_t,float> MovementPercent;
};
struct Profile
{
    Slot Key;std::string Skill,Executor;
    std::vector<std::string> Tags;
    float Damage=0,Rate=0,Range=0,Speed=0,Lifetime=0,Sweep=0,Spread=0;
    std::int32_t Count=1,MaximumCount=INT32_MAX;
    bool Default=false,Unlocked=true,Equipped=true,Active=false;
};
struct Card
{
    std::string Id;std::uint32_t Maximum=0;
    std::vector<std::string> Required,Excluded;
    Source Effect;
};
struct RuleBinding {Slot Key;std::string Skill;bool Admission=false;std::string SourceDefinition;};
struct Catalog
{
    std::vector<Profile> Profiles;
    std::map<std::string,Card> Cards;
    std::map<std::string,RuleBinding> Rules;
    std::map<std::uint16_t,float> MovementBase;
};
struct Offer
{
    Id Request,Session,PreviousSession;
    std::vector<std::string> Candidates;
    std::string Selected;
    Tick CommittedAt=0;
    bool Finished=false,PresentationClosed=false;
};
struct State
{
    Catalog Definitions;
    Revision RevisionValue=1;
    Id LastRequest;
    std::uint64_t Random=1;
    Offer Current;
    std::map<Id,Source> Sources;
    std::map<std::string,std::uint32_t> Acquired;
    std::map<Slot,std::string> Equipment;
};
struct Link
{
    std::array<ObjectKey,2> Roots;
    // A launched effect freezes the admitted numeric profile, independently
    // of subsequent loadout/card changes and of its source's lifetime.
    std::map<std::string,Profile> Frozen;
};
enum class Operation:std::uint8_t {Offer,Reroll,Choose,Equip,ClosePresentation};
struct Command
{
    Operation Op=Operation::Offer;
    Id Request,Session;
    Revision Expected=0;
    std::string Choice;
    Slot EquipmentSlot;
};
GULINETCORE_API bool Resolve(const Catalog&,const std::map<Id,Source>&,const std::map<Slot,std::string>&,
    std::vector<Profile>&,std::string& Error);
GULINETCORE_API bool ValidateCatalog(const Catalog&,std::string& Error);
GULINETCORE_API Bytes EncodeCatalog(const Catalog&);
GULINETCORE_API bool DecodeCatalog(std::span<const std::uint8_t>,Catalog&);
GULINETCORE_API Bytes EncodeState(const State&);
GULINETCORE_API bool DecodeState(std::span<const std::uint8_t>,State&);
GULINETCORE_API bool GetState(const ObjectRecord&,State&);
GULINETCORE_API bool SetState(ObjectRecord&,const State&);
GULINETCORE_API Bytes EncodeCommand(const Command&);
GULINETCORE_API bool DecodeCommand(std::span<const std::uint8_t>,Command&);
GULINETCORE_API Bytes EncodeLink(const Link&);
GULINETCORE_API bool DecodeLink(std::span<const std::uint8_t>,Link&);
}
