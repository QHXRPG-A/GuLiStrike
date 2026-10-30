#include "Gameplay/Skills/GuLiHybridArmy.h"
#include "Gameplay/Skills/Private/GuLiHybridArmyConversions.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSettings.h"
#include "Gameplay/Data/GuLiCommanderSkillCatalog.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Gameplay/Cards/GuLiRogueCardEffect.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "Gameplay/CommanderSkills/GuLiPointSkill.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

namespace
{
bool Append(UWorld&,FGuLiHybridManifestData& M,FString& Error)
{
    const auto* Table=GetDefault<UGuLiRogueCardSettings>()->Cards.LoadSynchronous();
    if (!Table || Table->GetRowStruct()!=FGuLiStrikeRogueCardsCardsRow::StaticStruct()) {Error=TEXT("Army/card catalog unavailable");return false;}
    TArray<FGuLiSoldierDefinition> Units;if (!UGuLiUnitDataSubsystem::ResolveAuthoredDefinitions(Units,Error)) return false;
    const auto FindUnit=[&](uint16 Id){return Units.FindByPredicate([Id](const auto& D){return D.UnitTypeId==Id;});};
    TMap<FName,FGuLiSpellFieldConfig> Fields;
    TArray<FGuLiSkillDefinition> Skills;TArray<FGuLiUnitSkillConfig> UnitSkills;
    if (!GuLiCommanderSkillCatalog::Resolve(GetDefault<UGuLiCommanderDataSettings>(),
        [&](uint16 Id){return FindUnit(Id)!=nullptr;},[&](FName Id)->const FGuLiSpellFieldConfig*
        {if (const auto* Field=Fields.Find(Id)) return Field;FGuLiSpellFieldConfig Field;
         return UGuLiSpellFieldDataSubsystem::ResolveAuthoredConfig(Id,Field)?&Fields.Add(Id,MoveTemp(Field)):nullptr;},
        Skills,UnitSkills,Error)) return false;
    GuLiNet::Army::Catalog C;
    const auto* ActiveCatalog=GetDefault<UGuLiCommanderSkillSettings>()->Catalog.LoadSynchronous();
    for (const auto& Row:UnitSkills)
    {
        const auto* Def=Skills.FindByPredicate([&](const auto& D){return D.SkillId==Row.SkillId;});
        if (!Def) {Error=TEXT("Missing authored skill definition");return false;}
        GuLiNet::Army::Profile P;P.Key={Row.UnitTypeId,GuLiArmyBridge::Text(Row.SlotId)};P.Skill=GuLiArmyBridge::Text(Row.SkillId);P.Executor=GuLiArmyBridge::Text(Def->ExecutorId);
        TArray<FGameplayTag> Tags;Def->Tags.GetGameplayTagArray(Tags);for (auto T:Tags) P.Tags.push_back(GuLiArmyBridge::Text(T.GetTagName()));
        P.Damage=Row.Damage;P.Rate=Row.AttackRatePerSecond;P.Range=Row.RangeCentimeters;P.Count=Row.ProjectileCount;
        P.Speed=Row.ProjectileSpeedCentimetersPerSecond;P.Lifetime=Row.ProjectileLifetimeSeconds;P.Sweep=Row.ProjectileSweepRadiusCentimeters;P.Spread=Row.ProjectileSpreadAngleDegrees;
        P.Default=Row.bDefault;P.Unlocked=Row.bInitiallyUnlocked;P.Active=Row.TriggerMode==EGuLiWeaponTriggerMode::Active;
        const auto* Active=ActiveCatalog?ActiveCatalog->FindUnitSkill(Row.UnitTypeId):nullptr;
        const auto* Point=Active?Cast<UGuLiPointSkillConfiguration>(Active->Configuration):nullptr;
        if (Point && Point->SourceWeaponSlot==Row.SlotId && Point->MaxProjectilesPerActivation>0) P.MaximumCount=Point->MaxProjectilesPerActivation;
        C.Profiles.push_back(P);
        const auto* Unit=FindUnit(Row.UnitTypeId);
        if (!Unit) {Error=TEXT("Army profile has no unit data definition");return false;}
        C.MovementBase.emplace(Row.UnitTypeId,Unit->MovementSpeedCmPerSecond);
        const FString Definition=FString::Printf(TEXT("unit/%u"),Row.UnitTypeId);
        M.Numbers.Add(TEXT("army/definition/")+Definition,Row.UnitTypeId);
        const FString Base=FGuLiHybridManifest::MakeDamageRuleId(Definition,Row.SkillId,Row.SlotId);
        for (const FString Id:{Base,TEXT("admit/linear/")+Base,TEXT("admit/runtime/")+Base,TEXT("settle/effect/linear/")+Base,TEXT("settle/effect/runtime/")+Base})
            if (const auto* R=M.Rules.FindByPredicate([&](const auto& R){return R.Id==Id;}))
                C.Rules.emplace(TCHAR_TO_UTF8(*Id),GuLiNet::Army::RuleBinding{P.Key,P.Skill,R->Kind==EGuLiHybridRuleKind::Spawn,TCHAR_TO_UTF8(*R->SourceDefinition)});
    }
    for (const auto Name:Table->GetRowNames())
    {
        const auto* Row=Table->FindRow<FGuLiStrikeRogueCardsCardsRow>(Name,TEXT("Hybrid army export"));
        if(Row&&Row->Type!=1)continue;
        UClass* Class=Row?Row->ImplementationClass.LoadSynchronous():nullptr;
        const auto* Effect=Class?Cast<UGuLiRogueCardEffect>(Class->GetDefaultObject()):nullptr;FGuLiRogueEffectDefinition Desc;
        if (!Row || !Effect || Row->UnitTypeId<=0 || Row->UnitTypeId>MAX_uint16 || Row->MaxAcquisitions<0 || !Effect->Describe(*Row,Desc,Error)) return false;
        GuLiNet::Army::Card Card;Card.Id=TCHAR_TO_UTF8(*Row->Id);Card.Maximum=uint32(Row->MaxAcquisitions);Card.Effect=GuLiArmyBridge::Source(Desc.Source);
        for (const auto& Id:Row->RequiredCardIds) Card.Required.push_back(TCHAR_TO_UTF8(*Id));
        for (const auto& Id:Row->ExcludedCardIds) Card.Excluded.push_back(TCHAR_TO_UTF8(*Id));
        for (const auto& [Unit,V]:Desc.MovementPercent) Card.Effect.MovementPercent.emplace(Unit,V);
        const auto Id=Card.Id;if (!C.Cards.emplace(Id,std::move(Card)).second) {Error=TEXT("Duplicate card identity");return false;}
    }
    std::string Why;if (!GuLiNet::Army::ValidateCatalog(C,Why)) {Error=UTF8_TO_TCHAR(Why.c_str());return false;}
    const auto Bytes=GuLiNet::Army::EncodeCatalog(C);if (Bytes.empty()) {Error=TEXT("Army catalog exceeds its capacity");return false;}
    M.Numbers.Add(TEXT("army/catalog/chunks"),double((Bytes.size()+2047)/2048));
    constexpr TCHAR Hex[]=TEXT("0123456789abcdef");
    for (SIZE_T Offset=0;Offset<Bytes.size();Offset+=2048)
    {FString Text;for (SIZE_T I=Offset;I<FMath::Min<SIZE_T>(Offset+2048,Bytes.size());++I) {Text.AppendChar(Hex[Bytes[I]>>4]);Text.AppendChar(Hex[Bytes[I]&15]);}M.Strings.Add(FString::Printf(TEXT("army/catalog/%llu"),uint64(Offset/2048)),MoveTemp(Text));}
    const FString Definition=TEXT("team/army");M.Strings.Add(TEXT("army/root_definition"),Definition);
    M.Numbers.Add(TEXT("capability/army_state/version"),GuLiNet::Army::Version);M.Numbers.Add(TEXT("capability/army_rules/version"),GuLiNet::Army::Version);
    if (!FGuLiHybridAdapterRegistry::BindDefinition(M,Definition,TEXT("ArmyState"),Error)) return false;
    for (auto Team:{EGuLiTeam::Red,EGuLiTeam::Blue})
    {
        auto& O=M.Objects.AddDefaulted_GetRef();O.Definition=Definition;O.Team=Team;O.EstimatedCost=.01;
        O.Key=FGuLiHybridManifest::MakeStableKey(M.MapPackage,EGuLiHybridObjectKind::BusinessWork,FString::Printf(TEXT("army/%u"),uint8(Team)));
    }
    auto& R=M.Rules.AddDefaulted_GetRef();R.Id=TEXT("capability/army/mutate");R.SourceDefinition=Definition;R.Kind=EGuLiHybridRuleKind::Capability;
    R.Capability=TEXT("army_state");R.CapabilityVersion=GuLiNet::Army::Version;R.CapabilityOperation=TEXT("mutate");R.EffectTicks=3600;
    return true;
}
const FGuLiHybridManifestRegistration Provider({TEXT("ArmyState"),EGuLiHybridManifestPhase::Derived,{TEXT("Flights")},Append});
}
