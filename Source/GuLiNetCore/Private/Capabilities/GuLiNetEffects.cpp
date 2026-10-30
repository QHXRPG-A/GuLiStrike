#include "GuLiNetEffects.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet::Effects
{
namespace
{
std::map<std::string,Handler>& Registry(){static std::map<std::string,Handler> R;return R;}
bool Name(const std::string& S){return !S.empty()&&S.size()<=192&&S.find('\0')==std::string::npos;}
bool ValidRicochet(const Ricochet& V)
{return V.Minimum&&V.Minimum<=V.Maximum&&V.Maximum<=MaximumRicochetHops&&V.PerStack&&std::isfinite(V.Range)&&V.Range>0&&V.Range<=1000000&&std::isfinite(V.Retention)&&V.Retention>0&&V.Retention<=1;}
}
bool Register(Handler H)
{if(!Name(H.Name)||!H.VersionValue||!H.Validate||!H.Accumulate)return false;const auto K=H.Name;return Registry().emplace(K,std::move(H)).second;}
const Handler* Find(const std::string& K){const auto I=Registry().find(K);return I==Registry().end()?nullptr:&I->second;}
bool Validate(const Definition& D)
{const auto* H=Find(D.Handler);return D.Id&&H&&D.VersionValue==H->VersionValue&&D.Parameters.size()<=4096&&H->Validate(D);}
bool Validate(const Set& S)
{if(S.size()>64)return false;std::set<std::uint32_t> Seen;for(const auto& D:S)if(!Seen.insert(D.Id).second||!Validate(D))return false;return true;}
Bytes Encode(const Set& S)
{if(!Validate(S))return {};Writer W(MaximumCapabilityBytes);W.U32(Version);W.U32(std::uint32_t(S.size()));for(const auto& D:S){W.U32(D.Id);W.U32(D.VersionValue);W.Text(D.Handler);W.Blob(D.Parameters);}return W.Take();}
bool Decode(std::span<const std::uint8_t> B,Set& Out)
{Reader R(B);Set S;std::uint32_t V,N;if(!R.U32(V)||V!=Version||!R.U32(N)||N>64)return false;S.resize(N);for(auto& D:S)if(!R.U32(D.Id)||!R.U32(D.VersionValue)||!R.Text(D.Handler,192)||!R.Blob(D.Parameters,4096))return false;if(!R.Done()||!Validate(S))return false;Out=std::move(S);return true;}
Bytes EncodeRicochet(const Ricochet& V)
{if(!ValidRicochet(V))return {};Writer W;W.U32(V.Minimum);W.U32(V.Maximum);W.U32(V.PerStack);W.F32(V.Range);W.F32(V.Retention);return W.Take();}
bool DecodeRicochet(std::span<const std::uint8_t> B,Ricochet& Out)
{Reader R(B);Ricochet V;if(!R.U32(V.Minimum)||!R.U32(V.Maximum)||!R.U32(V.PerStack)||!R.F32(V.Range)||!R.F32(V.Retention)||!R.Done()||!ValidRicochet(V))return false;Out=V;return true;}
Ricochet GetRicochet(const Set& S)
{Ricochet V;for(const auto& D:S)if(D.Handler=="ricochet"&&!DecodeRicochet(D.Parameters,V))return {};return V;}
bool AcceptedHit(const Set& S,const TransactionRequest& R,const CommitResult& Result,const std::map<ObjectKey,ObjectRecord>& Previous,std::map<ObjectKey,ObjectRecord>& Changes)
{std::set<std::string> Seen;for(const auto& D:S)if(Seen.insert(D.Handler).second){const auto* H=Find(D.Handler);if(!H||(H->AcceptedHit&&!H->AcceptedHit(S,R,Result,Previous,Changes)))return false;}return true;}
bool FreezePresentation(const Set& S,std::uint32_t& Bullet,std::uint32_t& Impact,std::uint32_t& Muzzle)
{for(const auto& D:S){const auto* H=Find(D.Handler);if(!H||(H->Presentation&&!H->Presentation(D,Bullet,Impact,Muzzle)))return false;}return true;}
namespace
{
const bool Registered=Register({"ricochet",1,[](const Definition& D){Ricochet V;return DecodeRicochet(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S)
    {
        Ricochet E;if(!N||!DecodeRicochet(D.Parameters,E))return false;
        const auto Extra=std::uint64_t(N-1)*E.PerStack;if(Extra+E.Maximum>MaximumRicochetHops)return false;
        E.Minimum+=std::uint32_t(Extra);E.Maximum+=std::uint32_t(Extra);
        auto I=std::find_if(S.begin(),S.end(),[](const auto& V){return V.Handler=="ricochet";});
        if(I!=S.end())
        {Ricochet Old;if(!DecodeRicochet(I->Parameters,Old)||Old.Range!=E.Range||Old.Retention!=E.Retention||std::uint64_t(Old.Maximum)+E.Maximum>MaximumRicochetHops)return false;E.Minimum+=Old.Minimum;E.Maximum+=Old.Maximum;I->Parameters=EncodeRicochet(E);}
        else {auto V=D;V.Parameters=EncodeRicochet(E);S.push_back(std::move(V));}return true;
    }});
}
}
