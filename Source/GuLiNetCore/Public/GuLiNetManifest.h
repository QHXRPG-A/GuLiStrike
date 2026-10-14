#pragma once
#include "GuLiNetGameplayTypes.h"
#include "GuLiNetUpgrade.h"
#include "GuLiNetOwnership.h"
#include <string_view>

namespace GuLiNet
{
inline constexpr std::uint32_t ManifestVersion = 24;
struct RuntimeManifest
{
    std::uint32_t Version = ManifestVersion;
    std::string MapPackage;
    std::map<ObjectKey, ObjectRecord> Objects;
    std::map<std::string, Rule> Rules;
    // Stable table/row/column keys. Assets and engine handles are not cloud inputs.
    std::map<std::string, double> Numbers;
    // Authored selectors and asset paths participate in the handshake. The service
    // treats them as opaque data and never resolves an engine object.
    std::map<std::string, std::string> Strings;
    Resources StartingRed, StartingBlue;
};
struct ManifestHashes { std::string Configuration, Map; };
GULINETCORE_API bool ValidateManifest(const RuntimeManifest& Value, std::string& Error);
GULINETCORE_API bool HashManifest(const RuntimeManifest& Value, ManifestHashes& Out, std::string& Error);
GULINETCORE_API bool ReadResourceCatalog(const RuntimeManifest& Value,ResourceLedgerState& Out,std::string& Error);
GULINETCORE_API bool ReadUpgradeCatalogs(const RuntimeManifest& Value,std::map<std::string,CompiledUpgradeCatalog>& Out,std::string& Error);
GULINETCORE_API bool ConfigureOwnerResources(class OwnerState& Target,const RuntimeManifest& Value,std::string& Error);
GULINETCORE_API bool ReadOwnershipSettings(const RuntimeManifest& Value,OwnershipSettings& Out,std::string& Error);
GULINETCORE_API bool ReadOwnershipDefinitions(const RuntimeManifest& Value,std::map<std::string,OwnershipDefinition>& Out,std::string& Error);
GULINETCORE_API bool RuntimeObjects(const RuntimeManifest& Value,std::map<ObjectKey,ObjectRecord>& Out,std::string& Error);
// JSON carries floating-point values as round-trippable decimal strings.
// UE and the standalone service must use this parser, not their JSON parser's
// potentially different decimal conversion, before hashing or applying a value.
GULINETCORE_API std::string EncodeManifestNumber(double Value);
GULINETCORE_API bool DecodeManifestNumber(std::string_view Text, double& Out);
GULINETCORE_API std::string DamageRuleId(const std::string& Definition, const std::string& Skill, const std::string& Slot);
}
