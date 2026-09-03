#include "Editor.h"
#include "GuLiFlightNavigationCookGate.h"
#include "GuLiFlightNavigationCookSettings.h"
#include "GuLiFlightNavigationLog.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"

class FGuLiFlightNavigationEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		PreSaveWorldHandle = FEditorDelegates::PreSaveWorldWithContext.AddRaw(
			this,
			&FGuLiFlightNavigationEditorModule::HandlePreSaveWorld);
	}

	virtual void ShutdownModule() override
	{
		if (PreSaveWorldHandle.IsValid())
		{
			FEditorDelegates::PreSaveWorldWithContext.Remove(PreSaveWorldHandle);
			PreSaveWorldHandle.Reset();
		}
	}

private:
	void HandlePreSaveWorld(UWorld* World, const FObjectPreSaveContext SaveContext)
	{
		if (!SaveContext.IsCooking())
		{
			return;
		}

		const UGuLiFlightNavigationCookSettings* Settings = GetDefault<UGuLiFlightNavigationCookSettings>();
		TArray<FGuLiFlightNavigationCookIssue> Issues;
		if (!FGuLiFlightNavigationCookGate::ValidateWorld(
			World,
			Settings->RequiredWorldPackages,
			Issues))
		{
			for (const FGuLiFlightNavigationCookIssue& Issue : Issues)
			{
				// UAT treats Error records as a cook failure even when callers omit
				// -RunAssetValidation. The Data/World validators remain the primary
				// package-rejection path when validation flags are enabled.
				UE_LOG(LogGuLiFlightNavigation, Error, TEXT("%s"), *Issue.ToLogString());
			}
		}
	}

	FDelegateHandle PreSaveWorldHandle;
};

IMPLEMENT_MODULE(FGuLiFlightNavigationEditorModule, GuLiFlightNavigationEditor)
