#pragma once
#include "CoreMinimal.h"
#include "GuLiNetManifest.h"

namespace GuLiNetBridge
{
// Runtime-private bridge: editor and reflected interfaces use the UE manifest.
bool ReadPortableManifest(const FString& File,GuLiNet::RuntimeManifest& Out,FString& Error);
}
