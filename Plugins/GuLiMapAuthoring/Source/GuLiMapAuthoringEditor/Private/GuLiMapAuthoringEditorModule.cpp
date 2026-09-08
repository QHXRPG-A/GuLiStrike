#include "Modules/ModuleManager.h"
#include "GuLiMapViewport.h"
#include "SGuLiMapPanel.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/CoreDelegates.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

class FGuLiMapAuthoringEditorModule : public IModuleInterface
{
    FDelegateHandle InitHandle,MapHandle,PIEHandle;
    bool Ready=false;
    void Initialize()
    {
        if (Ready||!GEditor||IsRunningCommandlet()) return; Ready=true;
        GuLiMapEditor::RegisterViewport();
        MapHandle=FEditorDelegates::MapChange.AddLambda([](uint32){GuLiMapEditor::EndInteraction();});
        PIEHandle=FEditorDelegates::PreBeginPIE.AddLambda([](bool){GuLiMapEditor::EndInteraction();});
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("GuLiMapAuthoring"),FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
        { return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SGuLiMapPanel)]; })).SetDisplayName(FText::FromString(TEXT("地图标注")));
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FGuLiMapAuthoringEditorModule::RegisterMenus));
    }
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        auto* Tools=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        auto& Section=Tools->FindOrAddSection(TEXT("GuLi"));
        Section.AddSubMenu(TEXT("GuLiMapTools"),FText::FromString(TEXT("GuLi")),FText::FromString(TEXT("GuLi 编辑器工具")),
            FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
            {
                Menu->AddSection(TEXT("Authoring")).AddMenuEntry(TEXT("GuLiMapAuthoring"),FText::FromString(TEXT("地图标注")),FText::FromString(TEXT("配置地图点位、区域与导出数据")),FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([]{FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("GuLiMapAuthoring")));})));
            }));
    }
    void StartupModule() override
    {
        InitHandle=FCoreDelegates::OnPostEngineInit.AddRaw(this,&FGuLiMapAuthoringEditorModule::Initialize);
        if (GEditor) Initialize();
    }
    void ShutdownModule() override
    {
        FCoreDelegates::OnPostEngineInit.Remove(InitHandle); FEditorDelegates::MapChange.Remove(MapHandle); FEditorDelegates::PreBeginPIE.Remove(PIEHandle);
        UToolMenus::UnRegisterStartupCallback(this); UToolMenus::UnregisterOwner(this);
        if (Ready)
        {
            if (auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(FName(TEXT("GuLiMapAuthoring")))) Tab->RequestCloseTab();
            FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("GuLiMapAuthoring")); GuLiMapEditor::UnregisterViewport();
        }
    }
};
IMPLEMENT_MODULE(FGuLiMapAuthoringEditorModule, GuLiMapAuthoringEditor)
