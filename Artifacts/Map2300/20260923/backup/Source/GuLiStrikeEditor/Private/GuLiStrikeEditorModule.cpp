// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiResourceAuthoringLibrary.h"
#include "GuLiNavigationBakeLibrary.h"

#include "Editor.h"
#include "Features/IModularFeatures.h"
#include "IPIEAuthorizer.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"

class FGuLiStrikeEditorModule final : public IModuleInterface, public IPIEAuthorizer
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
		PreSaveWorldHandle = FEditorDelegates::PreSaveWorldWithContext.AddRaw(this, &FGuLiStrikeEditorModule::ValidateCookNavigation);
	}

	virtual void ShutdownModule() override
	{
		FEditorDelegates::PreSaveWorldWithContext.Remove(PreSaveWorldHandle);
		if (IModularFeatures::Get().IsModularFeatureAvailable(IPIEAuthorizer::GetModularFeatureName()))
		{
			IModularFeatures::Get().UnregisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
		}
	}

	virtual bool RequestPIEPermission(bool bIsSimulateInEditor, FString& OutReason) const override
	{
		(void)bIsSimulateInEditor;
		const FGuLiResourceBakeResult Result = UGuLiResourceAuthoringLibrary::ValidateCurrentBake();
		if (!Result.bSuccess && !Result.Message.IsEmpty())
		{
			OutReason = Result.Message;
			return false;
		}
		const auto Navigation = UGuLiNavigationBakeLibrary::PrepareWorldNavigation(
			GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, true);
		if (!Navigation.bSuccess)
		{
			OutReason = Navigation.Message;
			return false;
		}
		return true;
	}

private:
	void ValidateCookNavigation(UWorld* World, FObjectPreSaveContext Context)
	{
		if (!Context.IsCooking()) return;
		const auto Result = UGuLiNavigationBakeLibrary::ValidateWorldNavigation(World);
		if (!Result.bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("[GULI_NAV_COOK_GATE] %s: %s Run -run=GuLiNavigationPrepare before cooking."),
				*GetPathNameSafe(World), *Result.Message);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[GULI_NAV_COOK_GATE] Ready world=%s check_s=%.6f"),
				*GetPathNameSafe(World), Result.TotalSeconds);
		}
	}
	FDelegateHandle PreSaveWorldHandle;
};

IMPLEMENT_MODULE(FGuLiStrikeEditorModule, GuLiStrikeEditor)
