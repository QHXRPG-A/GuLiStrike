#include "GuLiNetLinearFlightRecipe.h"
#include "GuLiNetManifest.h"
#include <algorithm>

namespace GuLiNet::LinearFlightRecipe
{
namespace
{
bool Family(const ObjectRecord& O)
{return O.Grant.Object.Kind==ObjectKind::Flight && O.Definition.starts_with("effect/linear/");}
bool Bound(const ObjectRecord& O,const CapabilityState& S)
{
    Recipe R;return S.Version==Version && Family(O) && Decode(S.Data,R)
        && R.Flight==O.Grant.Object && R.Source==O.Parent && R.Admission==O.Grant.SourceLease;
}
}
Bytes Encode(const Recipe& V)
{Writer W(MaximumCapabilityBytes);W.U32(Version);W.Key(V.Flight);W.Key(V.Source);W.Identity(V.Admission);W.Blob(V.State);return W.Take();}
bool Decode(std::span<const std::uint8_t> Data,Recipe& Out)
{
    Reader R(Data);Recipe V;std::uint32_t Ver;
    if (!R.U32(Ver) || Ver!=Version || !R.Key(V.Flight) || V.Flight.Kind!=ObjectKind::Flight || !V.Flight.Valid()
        || !R.Key(V.Source) || !V.Source.Valid() || !R.Identity(V.Admission) || !V.Admission.Valid()
        || !R.Blob(V.State,MaximumCapabilityBytes-128) || V.State.empty() || !R.Done()) return false;
    Out=std::move(V);return true;
}
namespace
{
const bool Registered=[]
{
    CapabilityContract C;C.Name=Name;C.Version=Version;C.ValidateState=Bound;
    C.ValidateRule=[](const Rule&) {return false;};
    C.Prepare=[](const CapabilityContext&,CapabilityDelta&) {return CommitStatus::InvalidRule;};
    C.ValidateTransition=[](const ObjectRecord& Before,const ObjectRecord& After)
    {
        const auto A=Before.Capabilities.find(Name),B=After.Capabilities.find(Name);
        return A!=Before.Capabilities.end() && B!=After.Capabilities.end()
            && A->second.Version==B->second.Version && A->second.Data==B->second.Data;
    };
    C.InitializeManifest=[](RuntimeManifest& M,std::string& Error)
    {
        const std::string Key=std::string("capability/")+Name+"/version";
        const bool Used=std::any_of(M.Rules.begin(),M.Rules.end(),[](const auto& P){return P.second.TargetDefinition.starts_with("effect/linear/");});
        if (!Used) return true;
        if (const auto I=M.Numbers.find(Key);I!=M.Numbers.end() && I->second!=Version) {Error="Unsupported linear flight recipe version";return false;}
        M.Numbers[Key]=Version;return true;
    };
    C.FreezeSpawn=[](const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest& R,ObjectRecord& Child,bool& PayloadClaimed)
    {
        if (!Family(Child)) return true;
        CapabilityState S{Version,R.CapabilityPayload};
        if (!Bound(Child,S)) return false;
        if (!Child.Capabilities.emplace(Name,std::move(S)).second) return false;
        PayloadClaimed=true;return true;
    };
    return RegisterCapability(std::move(C));
}();
}
}
