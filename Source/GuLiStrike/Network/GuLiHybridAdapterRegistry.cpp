#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridEntityState.h"
#include "Network/Private/GuLiHybridObjectStateEnvelope.h"
#include "Network/GuLiHybridManifest.h"

namespace
{
constexpr const TCHAR* DefinitionPrefix=TEXT("adapter/definition/");
TArray<const FGuLiHybridAdapterRegistration*>& Registrations()
{
    // Registrations may be destroyed in another translation unit during module unload.
    // Keep the container alive; the registrations themselves are removed explicitly.
    static auto* Items=new TArray<const FGuLiHybridAdapterRegistration*>;
    return *Items;
}
FString Metadata(FName Family,const TCHAR* Suffix)
{return TEXT("adapter/family/")+Family.ToString()+TEXT("/")+Suffix;}
bool Check(const FGuLiHybridManifestData& Manifest,const FString& Definition,
    EGuLiHybridObjectKind Kind,const TArray<FGuLiHybridAdapterFamily>& Families,FString& Error)
{
    const auto* Name=Manifest.Strings.Find(FString(DefinitionPrefix)+Definition);
    const auto* Family=Name?Families.FindByPredicate([&](const auto& F){return F.Name.ToString()==*Name;}):nullptr;
    const auto* Version=Family?Manifest.Numbers.Find(Metadata(Family->Name,TEXT("state_version"))):nullptr;
    const auto* DeclaredKind=Family?Manifest.Numbers.Find(Metadata(Family->Name,TEXT("kind"))):nullptr;
    const auto* TargetBinding=Family?Manifest.Numbers.Find(Metadata(Family->Name,TEXT("target_binding"))):nullptr;
    const FString Override=TEXT("ownership/definition/")+Definition+TEXT("/");
    const auto* Policy=Manifest.Numbers.Find(Override+TEXT("ownership_policy"));
    if (!Policy && Family) Policy=Manifest.Numbers.Find(Metadata(Family->Name,TEXT("ownership_policy")));
    const FString Group=Manifest.Strings.Contains(Override+TEXT("group"))?Manifest.Strings.FindRef(Override+TEXT("group")):
        Family?Manifest.Strings.FindRef(Metadata(Family->Name,TEXT("group"))):FString();
    const auto* Seat=Family?Manifest.Numbers.Find(Metadata(Family->Name,TEXT("controller_seat"))):nullptr;
    const auto* Retire=Family?Manifest.Numbers.Find(Metadata(Family->Name,TEXT("retire_on_vacancy"))):nullptr;
    if (!Family || Family->Kind!=Kind || !Version || *Version!=Family->StateVersion
        || !DeclaredKind || *DeclaredKind!=static_cast<uint8>(Kind)
        || !TargetBinding || *TargetBinding!=(Family->ResolveTarget?1:0)
        || !Policy || (*Policy!=uint8(Family->OwnershipPolicy) && (*Policy!=uint8(EGuLiHybridOwnershipPolicy::SharedGroup) || Family->OwnershipPolicy==EGuLiHybridOwnershipPolicy::ControllerPinned))
        || ((*Policy==uint8(EGuLiHybridOwnershipPolicy::SharedGroup))!=!Group.IsEmpty()) || !Seat || *Seat!=Family->ControllerSeat
        || !Retire || *Retire!=(Family->bRetireOnVacancy?1:0))
    {
        Error=FString::Printf(TEXT("Missing or incompatible gameplay adapter contract: %s (kind %u)"),*Definition,uint8(Kind));
        return false;
    }
    return true;
}
}
FGuLiHybridAdapterRegistration::FGuLiHybridAdapterRegistration(FGuLiHybridAdapterFamily InFamily):Family(MoveTemp(InFamily))
{Registrations().Add(this);}
FGuLiHybridAdapterRegistration::~FGuLiHybridAdapterRegistration() {Registrations().Remove(this);}

bool FGuLiHybridAdapterRegistry::GetFamilies(TArray<FGuLiHybridAdapterFamily>& Out,FString& Error)
{
    Out.Reset();TSet<FName> Names;
    for (const auto* Entry:Registrations())
    {
        const auto& F=Entry->GetFamily();
        if (F.Name.IsNone() || F.Kind==EGuLiHybridObjectKind::None || !F.StateVersion || !F.Create || Names.Contains(F.Name))
        {Error=TEXT("Invalid or duplicate gameplay adapter family: ")+F.Name.ToString();Out.Reset();return false;}
        Names.Add(F.Name);Out.Add(F);
    }
    Out.Sort([](const auto& A,const auto& B){return A.Name.LexicalLess(B.Name);});Error.Reset();return true;
}
bool FGuLiHybridAdapterRegistry::BindDefinition(FGuLiHybridManifestData& Manifest,const FString& Definition,FName Name,FString& Error)
{
    TArray<FGuLiHybridAdapterFamily> Families;if (!GetFamilies(Families,Error)) return false;
    const auto* Family=Families.FindByPredicate([&](const auto& F){return F.Name==Name;});
    const FString Key=FString(DefinitionPrefix)+Definition;
    const auto* Previous=Manifest.Strings.Find(Key);
    if (Definition.IsEmpty() || Definition.Len()>192 || !Family || (Previous && *Previous!=Name.ToString()))
    {Error=TEXT("Conflicting or unknown gameplay adapter family for ")+Definition;return false;}
    Manifest.Strings.Add(Key,Name.ToString());
    Manifest.Numbers.Add(Metadata(Name,TEXT("kind")),uint8(Family->Kind));
    Manifest.Numbers.Add(Metadata(Name,TEXT("state_version")),Family->StateVersion);
    Manifest.Numbers.Add(Metadata(Name,TEXT("target_binding")),Family->ResolveTarget?1:0);
    Manifest.Numbers.Add(Metadata(Name,TEXT("ownership_policy")),uint8(Family->OwnershipPolicy));
    if (!Family->OwnershipGroup.IsNone()) Manifest.Strings.Add(Metadata(Name,TEXT("group")),Family->OwnershipGroup.ToString());
    Manifest.Numbers.Add(Metadata(Name,TEXT("controller_seat")),Family->ControllerSeat);
    Manifest.Numbers.Add(Metadata(Name,TEXT("retire_on_vacancy")),Family->bRetireOnVacancy?1:0);
    Manifest.Strings.Add(TEXT("adapter/contract"),TEXT("3"));
    Manifest.Strings.Add(TEXT("adapter/actor_components"),FString::FromInt(GuLiHybridObjectState::Schema));
    if (!FGuLiHybridComponentState::BindContracts(Manifest,Error)) return false;
    Error.Reset();return true;
}
bool FGuLiHybridAdapterRegistry::Validate(const FGuLiHybridManifestData& Manifest,FString& Error)
{
    if (!FGuLiHybridComponentState::ValidateContracts(Manifest,Error) || !FGuLiHybridEntityState::ValidateManifest(Manifest,Error)) return false;
    TArray<FGuLiHybridAdapterFamily> Families;if (!GetFamilies(Families,Error)) return false;
    if (Families.IsEmpty() || Manifest.Strings.FindRef(TEXT("adapter/contract"))!=TEXT("3")
        || Manifest.Strings.FindRef(TEXT("adapter/actor_components"))!=FString::FromInt(GuLiHybridObjectState::Schema))
    {Error=TEXT("The manifest has no compatible gameplay adapter contract; export it with the current runtime");return false;}
    for (const auto& [Key,Name]:Manifest.Strings) if (Key.StartsWith(DefinitionPrefix))
    {
        const auto* Family=Families.FindByPredicate([&](const auto& F){return F.Name.ToString()==Name;});
        if (!Family) {Error=TEXT("Unknown gameplay adapter family: ")+Name;return false;}
        if (!Check(Manifest,Key.RightChop(FCString::Strlen(DefinitionPrefix)),Family->Kind,Families,Error)) return false;
    }
    for (const auto& Object:Manifest.Objects)
        if (!Check(Manifest,Object.Definition,Object.Key.Kind,Families,Error)) return false;
    for (const auto& Rule:Manifest.Rules)
        if (Rule.SpawnKind!=EGuLiHybridObjectKind::None
            && !Check(Manifest,Rule.TargetDefinition,Rule.SpawnKind,Families,Error)) return false;
    Error.Reset();return true;
}
bool FGuLiHybridAdapterRegistry::Resolve(const FGuLiHybridManifestData& Manifest,const FGuLiHybridObjectState& State,
    FGuLiHybridAdapterFamily& Out,FString& Error)
{
    TArray<FGuLiHybridAdapterFamily> Families;if (!GetFamilies(Families,Error)
        || !Check(Manifest,State.Definition.ToString(),State.Key.Kind,Families,Error)) return false;
    const auto Name=Manifest.Strings.FindRef(FString(DefinitionPrefix)+State.Definition.ToString());
    Out=*Families.FindByPredicate([&](const auto& F){return F.Name.ToString()==Name;});return true;
}
bool FGuLiHybridAdapterRegistry::ResolveDefinition(const FGuLiHybridManifestData& Manifest,const FString& Definition,
    FGuLiHybridAdapterFamily& Out,FString& Error)
{
    TArray<FGuLiHybridAdapterFamily> Families;if (!GetFamilies(Families,Error)) return false;
    const FString Name=Manifest.Strings.FindRef(FString(DefinitionPrefix)+Definition);
    const auto* Family=Families.FindByPredicate([&](const auto& F){return F.Name.ToString()==Name;});
    if (!Family) {Error=TEXT("No registered capability family for ")+Definition;return false;}
    if (!Check(Manifest,Definition,Family->Kind,Families,Error)) return false;
    Out=*Family;return true;
}
