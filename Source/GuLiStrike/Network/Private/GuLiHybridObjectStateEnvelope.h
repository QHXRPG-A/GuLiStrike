#pragma once
#include "GuLiNetProtocol.h"

namespace GuLiHybridObjectState
{
inline constexpr std::uint32_t Schema=3,MaximumPiece=1024*1024,MaximumBytes=4*MaximumPiece+64*1024+64;
struct FParts {GuLiNet::Bytes Family,Components,Applications,Entities;bool HasActor=false;};
inline bool Decode(std::span<const std::uint8_t> Bytes,std::uint32_t FamilyVersion,FParts& Out)
{
    if (Bytes.empty() || Bytes.size()>MaximumBytes || !FamilyVersion) return false;
    GuLiNet::Reader R(Bytes);std::uint32_t Magic=0,Version=0,Family=0;std::uint8_t Actor=0;FParts V;
    if (!R.U32(Magic) || Magic!=0x474c4f53 || !R.U32(Version) || Version!=Schema
        || !R.U32(Family) || Family!=FamilyVersion || !R.U8(Actor) || Actor>1
        || !R.Blob(V.Family,MaximumPiece) || V.Family.empty()
        || !R.Blob(V.Components,MaximumPiece) || (Actor!=0)!=(!V.Components.empty())
        || !R.Blob(V.Applications,64*1024) || V.Applications.empty()
        || !R.Blob(V.Entities,2*MaximumPiece) || V.Entities.empty() || !R.Done()) return false;
    V.HasActor=Actor!=0;Out=std::move(V);return true;
}
inline GuLiNet::Bytes Encode(std::uint32_t FamilyVersion,std::span<const std::uint8_t> Family,
    bool HasActor,std::span<const std::uint8_t> Components,std::span<const std::uint8_t> Applications,std::span<const std::uint8_t> Entities)
{
    if (!FamilyVersion || Family.empty() || Family.size()>MaximumPiece || Components.size()>MaximumPiece
        || HasActor!=!Components.empty() || Applications.empty() || Applications.size()>64*1024
        || Entities.empty() || Entities.size()>2*MaximumPiece) return {};
    GuLiNet::Writer W(MaximumBytes);W.U32(0x474c4f53);W.U32(Schema);W.U32(FamilyVersion);W.U8(HasActor?1:0);
    W.Blob(Family);W.Blob(Components);W.Blob(Applications);W.Blob(Entities);return W.Take();
}
}
