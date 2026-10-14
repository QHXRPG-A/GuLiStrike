#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"

bool GuLiHybridWingman::AppendCatalog(const UGuLiShipBuildCatalog& Catalog,FName Domain,FGuLiHybridManifestData& Data,FString& Error)
{
    for (const auto& GroupDefinition:Catalog.Groups) if (GroupDefinition.bExecutable)
        for (const auto& Capability:GroupDefinition.Capabilities)
        {
            const auto* CDO=Capability.ComponentClass?Cast<UGuLiShipHangarCapabilityComponent>(Capability.ComponentClass->GetDefaultObject()):nullptr;
            if (!CDO) continue;
            const auto* Definition=Cast<UGuLiShipHangarDefinition>(Capability.Configuration);
            FString Why;if (!Definition || !CDO->ValidateConfiguration(Definition,Why)) {Error=TEXT("Invalid independent Wingman profile: ")+Why;return false;}
            const FString Id=Profile(Domain,GroupDefinition.ConfigurationId,Capability.CapabilityId);
            if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,Id,TEXT("WingmanMember"),Error)) return false;
            if (!FGuLiHybridComponentState::BindDefinitionLayout(Data,Id,*AGuLiWingmanPawn::StaticClass(),Error)) return false;
            Data.Numbers.Add(TEXT("relocation/target/")+Id,6);
            Data.Strings.Add(Id+TEXT("/configuration"),Definition->GetPathName());
            Data.Strings.Add(Id+TEXT("/configuration_hash"),LexToString(CDO->GetConfigurationChecksum(Definition)));
            Data.Strings.Add(Id+TEXT("/group_id"),GroupDefinition.GroupId.ToString().ToLower());
            Data.Strings.Add(Id+TEXT("/capability_id"),Capability.CapabilityId.ToString().ToLower());
            Data.Strings.Add(Id+TEXT("/spawn_rule"),TEXT("spawn/")+Id);
            auto& Spawn=Data.Rules.AddDefaulted_GetRef();Spawn.Id=TEXT("spawn/")+Id;Spawn.Kind=EGuLiHybridRuleKind::Spawn;
            Spawn.SourceDefinition=TEXT("player/ship");Spawn.TargetDefinition=Id;Spawn.SpawnKind=EGuLiHybridObjectKind::Wingman;
            Spawn.SpawnHealth=1000;Spawn.SpawnCost=1;Spawn.SpawnControllerSeat=uint8(EGuLiCommanderRole::Air);
            Spawn.MaximumLiveChildren=GULI_WINGMAN_GROUP_SIZE;Spawn.EffectTicks=600;
            Spawn.RequiredCapabilityCatalog=Domain.ToString().ToLower();Spawn.RequiredCapabilityGroup=GroupDefinition.ConfigurationId.ToString().ToLower();
            Spawn.RequiredCapability=Capability.CapabilityId.ToString().ToLower();
            TArray<FGuLiShipAbilityGrant> Grants;if (!Definition->AbilitySet->ResolveLoadout(Definition->Loadout,Grants,&Error)) return false;
            for (const auto& Grant:Grants) if (const auto* Weapon=Grant.WeaponDefinition.Get())
            {
                auto& Damage=Data.Rules.AddDefaulted_GetRef();Damage.Id=FGuLiHybridManifest::MakeDamageRuleId(Id,Grant.GetEffectiveSkillId(),Grant.GetEffectiveWeaponSlotId());
                Damage.SourceDefinition=Id;Damage.Damage=Weapon->Damage;
                Damage.CooldownTicks=FMath::Max<int64>(1,FMath::CeilToInt64(Weapon->CooldownSeconds*60.));
                Damage.EffectTicks=FMath::Max<int64>(600,FMath::CeilToInt64(Weapon->ProjectileLifetimeSeconds*60.));Damage.MaximumTargets=1;
            }
        }
    return true;
}
