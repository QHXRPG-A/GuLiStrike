// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiResourceAuthoringLibrary.h"

#include "Features/IModularFeatures.h"
#include "IPIEAuthorizer.h"
#include "Modules/ModuleManager.h"

class FGuLiStrikeEditorModule final : public IModuleInterface, public IPIEAuthorizer
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
	}

	virtual void ShutdownModule() override
	{
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
		return true;
	}
};

IMPLEMENT_MODULE(FGuLiStrikeEditorModule, GuLiStrikeEditor)
