#pragma once
#include "GuLiNetCapability.h"
namespace GuLiNet::ObjectBatch
{
inline constexpr const char* Capability="object_batch";
inline constexpr std::uint32_t Version=1;
struct Placement {double X=0,Y=0,Z=0;float Yaw=0;};
GULINETCORE_API Bytes Encode(const std::vector<Placement>&);
GULINETCORE_API ObjectKey Child(ObjectKey Source,Id Event,ObjectKind Kind,std::uint32_t Index);
}
