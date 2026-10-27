#include "Gameplay/GroundMech/Network/GuLiHybridGroundUpgrade.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiHybridUpgradeCatalog.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Engine/World.h"
#include <algorithm>

bool GuLiHybridGroundUpgrade::Append(const UGuLiGroundMechWeaponComponent& Weapon,FGuLiHybridManifestData& Data,FString& Error)
{
    if (!Weapon.UpgradeTable || !Weapon.SkillTable) {Error=TEXT("Missing Ground upgrade tables");return false;}
    TArray<const FGuLiStrikeMechUpgradesRow*> Rows;
    for (const auto& [_,Value]:Weapon.UpgradeTable->GetRowMap()) Rows.Add(reinterpret_cast<const FGuLiStrikeMechUpgradesRow*>(Value));
    Rows.Sort([](const auto& A,const auto& B) {return A.SkillId!=B.SkillId?A.SkillId<B.SkillId:A.Level<B.Level;});
    GuLiNet::UpgradeCatalog Catalog;TMap<int32,FString> Previous;TMap<int32,int32> Levels;
    TArray<FString> Initial;bool FoundInitial=false;
    for (const auto* Row:Rows)
    {
        const FGuLiStrikeMechSkillsRow* Skill=nullptr;
        for (const auto& [_,Value]:Weapon.SkillTable->GetRowMap())
        {const auto* S=reinterpret_cast<const FGuLiStrikeMechSkillsRow*>(Value);if (S->Id==Row->SkillId) Skill=S;}
        if (!Skill || Row->Id.IsEmpty() || Row->SkillId<=0 || Row->Level<=0 || Row->Level<=Levels.FindRef(Row->SkillId)
            || !FMath::IsFinite(Row->Damage) || Row->Damage<=0 || !FMath::IsFinite(Row->FireRate) || Row->FireRate<=0 || Row->FireRate>30
            || !FMath::IsFinite(Skill->ProjectileLifetime) || Skill->ProjectileLifetime<=0)
        {Error=TEXT("Invalid Ground weapon upgrade row: ")+Row->Id;return false;}
        const std::string Id=TCHAR_TO_UTF8(*Row->Id.ToLower());
        GuLiNet::UpgradeNode Node;Node.Id=Node.Group=Id;Node.Route="skill/"+std::to_string(Row->SkillId);Node.Level=Row->Level;
        Node.Replaces=TCHAR_TO_UTF8(*Previous.FindRef(Row->SkillId).ToLower());Catalog.Nodes.push_back(Node);
        GuLiNet::UpgradeGroup Group;Group.Configuration=Id;Group.Identity=Node.Route;Group.Slots={"basicattack"};Group.Executable=true;
        Group.Capabilities.push_back({"fire/"+Id,"ground_weapon",{}, {}});Catalog.Groups.push_back(std::move(Group));
        Previous.Add(Row->SkillId,Row->Id);Levels.Add(Row->SkillId,Row->Level);
        auto& Rule=Data.Rules.AddDefaulted_GetRef();Rule.SourceDefinition=TEXT("player/ground");
        Rule.Id=FGuLiHybridManifest::MakeDamageRuleId(Rule.SourceDefinition,FName(*Row->Id),TEXT("BasicAttack"));
        Rule.Damage=Row->Damage;Rule.CooldownTicks=FMath::Max<int64>(1,FMath::CeilToInt64(60./Row->FireRate));
        Rule.EffectTicks=FMath::Max<int64>(600,FMath::CeilToInt64(Skill->ProjectileLifetime*60.));Rule.MaximumTargets=1;
        Rule.RequiredCapabilityCatalog=Domain;Rule.RequiredCapabilityGroup=Row->Id.ToLower();Rule.RequiredCapability=TEXT("fire/")+Row->Id.ToLower();
        if (Row->Id==Weapon.CurrentUpgradeId)
        {
            FoundInitial=true;std::string Current=Id;
            while (!Current.empty())
            {
                Initial.Insert(UTF8_TO_TCHAR(Current.c_str()),0);
                const auto I=std::find_if(Catalog.Nodes.begin(),Catalog.Nodes.end(),[&](const auto& N){return N.Id==Current;});
                if (I==Catalog.Nodes.end()) {Error=TEXT("Ground initial upgrade predecessor is missing");return false;}Current=I->Replaces;
            }
        }
    }
    if (!FoundInitial || !GuLiHybridUpgradeCatalog::Append(Data,Domain,Catalog,Error)) return false;
    GuLiHybridUpgradeCatalog::Initial(Data,TEXT("player/ground"),Domain,Initial);
    auto& Rule=Data.Rules.AddDefaulted_GetRef();Rule.Id=TEXT("upgrade/player/ground");Rule.Kind=EGuLiHybridRuleKind::Upgrade;
    Rule.SourceDefinition=Rule.TargetDefinition=TEXT("player/ground");Rule.UpgradeCatalog=Domain;return true;
}
FGuid UGuLiGroundMechWeaponComponent::RequestUpgradeById(const FString& Id)
{
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Binding || !Binding->CanControl() || !H || Id.IsEmpty()) return {};
    const auto* Selection=Binding->GetState().Upgrades.Find(GuLiHybridGroundUpgrade::Domain);if (!Selection) return {};
    FGuLiHybridUpgradeRequest R;R.Source=Binding->GetObjectKey();R.Rule=TEXT("upgrade/player/ground");R.Node=FName(*Id.ToLower());
    R.ExpectedSelectionRevision=Selection->Revision;return H->SubmitUpgrade(R);
}
bool UGuLiGroundMechWeaponComponent::ApplyHybridUpgrade(const FGuLiHybridObjectState& State)
{
    const auto* Selection=State.Upgrades.Find(GuLiHybridGroundUpgrade::Domain);
    if (!Selection || Selection->Chosen.IsEmpty()) return false;
    const FString Id=Selection->Chosen.Last().ToString();if (CurrentUpgradeId.Equals(Id,ESearchCase::IgnoreCase) && bConfigured) return true;
    const float Before=ActiveUpgrade.FireRate;
    if (!LoadConfiguration(Id)) return false;
    NextShotTime=GuLiMechFire::RescaleCooldown(UGuLiHybridActorComponent::BattleSeconds(GetOwner()),NextShotTime,Before,ActiveUpgrade.FireRate);
    CurrentUpgradeId=Id;return true;
}
namespace
{
bool StateVersion(TConstArrayView<uint8> Bytes)
{FGuLiHybridStateArchive A(Bytes,16);uint32 Version=0;A.Value(Version);return Version==1 && A.Done();}
const FGuLiHybridComponentCapabilityRegistration GroundWeaponCapability({TEXT("GroundWeaponUpgrade"),1,
    []()->const UClass* {return UGuLiGroundMechWeaponComponent::StaticClass();},
    [](const UActorComponent&,TArray<uint8>& Bytes) {FGuLiHybridStateArchive A(16);uint32 Version=1;A.Value(Version);return A.Finish(Bytes);},
    [](const UActorComponent&,TConstArrayView<uint8> Bytes) {return StateVersion(Bytes);},
    [](UActorComponent&,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage) {return StateVersion(Bytes);},nullptr,nullptr,
    [](UActorComponent& C,const FGuLiHybridObjectState& State,uint64,bool)
    {return CastChecked<UGuLiGroundMechWeaponComponent>(&C)->ApplyHybridUpgrade(State);}});
}
