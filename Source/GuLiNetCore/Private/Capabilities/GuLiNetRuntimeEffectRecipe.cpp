#include "GuLiNetRuntimeEffectRecipe.h"
#include "GuLiNetManifest.h"
#include <algorithm>

namespace GuLiNet::RuntimeEffectRecipe
{
namespace
{
bool Family(const ObjectRecord& O)
{return O.Grant.Object.Kind==ObjectKind::Field && O.Definition.starts_with("effect/runtime/");}
bool Bound(const ObjectRecord& O,const CapabilityState& S)
{
    Recipe R;return S.Version==Version && Family(O) && Decode(S.Data,R)
        && R.Effect==O.Grant.Object && R.Source==O.Parent && R.Admission==O.Grant.SourceLease;
}
}
Bytes Encode(const Recipe& V)
{Writer W(MaximumCapabilityBytes);W.U32(Version);W.Key(V.Effect);W.Key(V.Source);W.Identity(V.Admission);W.Blob(V.State);return W.Take();}
bool Decode(std::span<const std::uint8_t> Data,Recipe& Out)
{
    Reader R(Data);Recipe V;std::uint32_t Ver;
    if (!R.U32(Ver) || Ver!=Version || !R.Key(V.Effect) || V.Effect.Kind!=ObjectKind::Field || !V.Effect.Valid()
        || !R.Key(V.Source) || !V.Source.Valid() || !R.Identity(V.Admission) || !V.Admission.Valid()
        || !R.Blob(V.State,MaximumCapabilityBytes-128) || V.State.empty() || !R.Done()) return false;
    Out=std::move(V);return true;
}
namespace
{
const bool Registered=[]
{
    CapabilityContract C;C.Name=Name;C.Version=Version;C.ValidateState=Bound;
    C.ValidateRule=[](const Rule&){return false;};
    C.Prepare=[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;};
    C.ValidateTransition=[](const ObjectRecord& A,const ObjectRecord& B)
    {
        const auto X=A.Capabilities.find(Name),Y=B.Capabilities.find(Name);
        return X!=A.Capabilities.end() && Y!=B.Capabilities.end() && X->second.Version==Y->second.Version && X->second.Data==Y->second.Data;
    };
    C.InitializeManifest=[](RuntimeManifest& M,std::string& Error)
    {
        const std::string Key=std::string("capability/")+Name+"/version";
        if (!std::any_of(M.Rules.begin(),M.Rules.end(),[](const auto& P){return P.second.TargetDefinition.starts_with("effect/runtime/");})) return true;
        if (const auto I=M.Numbers.find(Key);I!=M.Numbers.end() && I->second!=Version) {Error="Unsupported runtime effect recipe version";return false;}
        M.Numbers[Key]=Version;return true;
    };
    C.FreezeSpawn=[](const ObjectRecord&,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest& R,ObjectRecord& Child,bool& Claimed)
    {
        if (!Family(Child)) return true;CapabilityState S{Version,R.CapabilityPayload};
        if (!Bound(Child,S) || !Child.Capabilities.emplace(Name,std::move(S)).second) return false;
        Claimed=true;return true;
    };
    return RegisterCapability(std::move(C));
}();
}
}
