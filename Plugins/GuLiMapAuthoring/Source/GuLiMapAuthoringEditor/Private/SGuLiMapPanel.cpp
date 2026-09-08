#include "SGuLiMapPanel.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSettings.h"
#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapMarker.h"
#include "GuLiMapViewport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"

UGuLiMapAuthoringSubsystem* SGuLiMapPanel::Service() const { return GEditor?GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>():nullptr; }
void SGuLiMapPanel::Construct(const FArguments& Args)
{
    auto& PropertyModule=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs ViewArgs; ViewArgs.bHideSelectionTip=true; ViewArgs.bLockable=false; ViewArgs.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    Details=PropertyModule.CreateDetailView(ViewArgs);
    auto Button=[this](const TCHAR* Label,const TCHAR* Action)
    { return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([this,A=FString(Action)]{return Run(A);}); };
    for (FName N:GuLiMap::ListShapes()) Shapes.Add(MakeShared<FName>(N)); if (!Shapes.IsEmpty()) Shape=Shapes[0];
    ChildSlot
    [
        SNew(SBorder).Padding(8)
        [
            SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
            [ SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Service()&&Service()->EditorWorld()?Service()->EditorWorld()->GetOutermost()->GetName():TEXT("PIE/SIE 中不可编辑"));}) ]
            +SVerticalBox::Slot().AutoHeight()
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4))
                +SWrapBox::Slot()[Button(TEXT("校验"),TEXT("validate"))]
                +SWrapBox::Slot()[Button(TEXT("保存"),TEXT("save"))]
                +SWrapBox::Slot()[Button(TEXT("保存并导出"),TEXT("export"))]
                +SWrapBox::Slot()[Button(TEXT("刷新"),TEXT("refresh"))]
                +SWrapBox::Slot()[Button(TEXT("生成三个预设"),TEXT("presets"))]
            ]
            +SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [
                SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)
                [
                    SAssignNew(TypeCombo,SComboBox<TWeakObjectPtr<UGuLiMapTypeDefinition>>).OptionsSource(&Types)
                    .OnGenerateWidget_Lambda([](TWeakObjectPtr<UGuLiMapTypeDefinition> T){return SNew(STextBlock).Text(FText::FromString(T.IsValid()?T->TypeId.ToString()+TEXT(" · ")+T->DisplayName:TEXT("Missing")));})
                    .OnSelectionChanged_Lambda([this](TWeakObjectPtr<UGuLiMapTypeDefinition> T,ESelectInfo::Type){SelectedType=T; QueueRefresh();})
                    [ SNew(STextBlock).Text_Lambda([this]{return FText::FromString(SelectedType.IsValid()?SelectedType->TypeId.ToString()+TEXT(" · ")+SelectedType->DisplayName:TEXT("选择类型"));}) ]
                ]
                +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[Button(TEXT("编辑类型/字段"),TEXT("edittype"))]
            ]
            +SVerticalBox::Slot().AutoHeight()
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4))
                +SWrapBox::Slot()[Button(TEXT("视口放置"),TEXT("place"))]
                +SWrapBox::Slot()[Button(TEXT("退出放置"),TEXT("stop"))]
                +SWrapBox::Slot()[Button(TEXT("世界原点"),TEXT("origin"))]
                +SWrapBox::Slot()[Button(TEXT("所选 Actor 原点"),TEXT("selectedorigin"))]
                +SWrapBox::Slot()[SNew(SCheckBox).IsChecked_Lambda([]{return GetDefault<UGuLiMapAuthoringSettings>()->bSurfacePlacement?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([](ECheckBoxState S){auto* C=GetMutableDefault<UGuLiMapAuthoringSettings>(); C->bSurfacePlacement=S==ECheckBoxState::Checked; C->SaveConfig();})[SNew(STextBlock).Text(FText::FromString(TEXT("表面优先")))]]
                +SWrapBox::Slot()[SNew(STextBlock).Text(FText::FromString(TEXT("工作平面 Z (cm)")))]
                +SWrapBox::Slot()[SNew(SBox).WidthOverride(110)[SNew(SNumericEntryBox<double>).Value_Lambda([]{return TOptional<double>(GetDefault<UGuLiMapAuthoringSettings>()->WorkPlaneZ);})
                    .OnValueCommitted_Lambda([](double V,ETextCommit::Type){if (FMath::IsFinite(V)) {auto* C=GetMutableDefault<UGuLiMapAuthoringSettings>(); C->WorkPlaneZ=V; C->SaveConfig();}})]]
            ]
            +SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [
                SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox).HintText(FText::FromString(TEXT("新增 TypeId，例如 SupplyPoint"))).OnTextChanged_Lambda([this](const FText& T){NewTypeId=T.ToString();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[Button(TEXT("创建类型"),TEXT("newtype"))]
            ]
            +SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SSearchBox).HintText(FText::FromString(TEXT("搜索业务键、名称、类型"))).OnTextChanged_Lambda([this](const FText& T){Search=T.ToString(); QueueRefresh();})]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState S){OnlyType=S==ECheckBoxState::Checked; QueueRefresh();})[SNew(STextBlock).Text(FText::FromString(TEXT("当前类型")))]]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState S){OnlyEnabled=S==ECheckBoxState::Checked; QueueRefresh();})[SNew(STextBlock).Text(FText::FromString(TEXT("仅启用")))]]
            ]
            +SVerticalBox::Slot().FillHeight(1).Padding(0,5)
            [
                SNew(SSplitter)
                +SSplitter::Slot().Value(0.35f)
                [
                    SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3,3))
                        +SWrapBox::Slot()[Button(TEXT("聚焦 F"),TEXT("focus"))]
                        +SWrapBox::Slot()[Button(TEXT("复制"),TEXT("duplicate"))]
                        +SWrapBox::Slot()[Button(TEXT("删除"),TEXT("delete"))]
                        +SWrapBox::Slot()[Button(TEXT("启用"),TEXT("enable"))]
                        +SWrapBox::Slot()[Button(TEXT("禁用"),TEXT("disable"))]
                    ]
                    +SVerticalBox::Slot().FillHeight(1)
                    [
                        SAssignNew(List,SListView<FItem>).ListItemsSource(&Items).SelectionMode(ESelectionMode::Multi)
                        .OnGenerateRow_Lambda([](FItem Item,const TSharedRef<STableViewBase>& Owner)
                        {
                            auto* A=Item->Actor.Get(); const auto* T=A?A->Record.Type.LoadSynchronous():nullptr;
                            const FString Label=A?(A->Record.bEnabled?TEXT("● "):TEXT("○ "))+A->Record.DisplayName+TEXT("\n")+A->Record.MarkerKey.ToString()+TEXT("\n")+(T?T->TypeId.ToString():TEXT("Missing type")):TEXT("Missing");
                            return SNew(STableRow<FItem>,Owner).Padding(5)[SNew(STextBlock).Text(FText::FromString(Label)).AutoWrapText(true)];
                        }).OnSelectionChanged(this,&SGuLiMapPanel::SelectList).OnMouseButtonDoubleClick(this,&SGuLiMapPanel::Focus)
                    ]
                ]
                +SSplitter::Slot().Value(0.65f)
                [
                    SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().FillWidth(1)
                        [ SNew(SComboBox<TSharedPtr<FName>>).OptionsSource(&Shapes).InitiallySelectedItem(Shape)
                            .OnGenerateWidget_Lambda([](TSharedPtr<FName> N){return SNew(STextBlock).Text(FText::FromName(*N));})
                            .OnSelectionChanged_Lambda([this](TSharedPtr<FName> N,ESelectInfo::Type){Shape=N;})
                            [SNew(STextBlock).Text_Lambda([this]{return Shape?FText::FromName(*Shape):FText::GetEmpty();})] ]
                        +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("添加命名区域"),TEXT("addregion"))]
                    ]
                    +SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("坐标/尺寸 cm（100cm=1m）；Scale=1\n区域中心：移动/旋转；绿点选边 + Insert 插点；Delete 删顶点；Ctrl+R 反转。"))).AutoWrapText(true) ]
                    +SVerticalBox::Slot().FillHeight(1)[Details.ToSharedRef()]
                ]
            ]
            +SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).AutoWrapText(true)]
            +SVerticalBox::Slot().AutoHeight().MaxHeight(130)
            [
                SAssignNew(IssueList,SListView<TSharedPtr<FGuLiMapIssue>>).ListItemsSource(&Issues)
                .OnGenerateRow_Lambda([](TSharedPtr<FGuLiMapIssue> I,const TSharedRef<STableViewBase>& Owner)
                {return SNew(STableRow<TSharedPtr<FGuLiMapIssue>>,Owner)[SNew(STextBlock).Text(FText::FromString(I->Field+TEXT(" ")+I->Message)).ColorAndOpacity(FLinearColor(1,0.35f,0.15f)).AutoWrapText(true)];})
                .OnSelectionChanged_Lambda([this](TSharedPtr<FGuLiMapIssue> Issue,ESelectInfo::Type)
                { if (Issue&&Service()) for (auto* A:Service()->LoadedMarkers()) if (A->Record.MarkerId==Issue->MarkerId) {GEditor->SelectNone(false,true); GEditor->SelectActor(A,true,true); GEditor->MoveViewportCamerasToActor(*A,false);} })
            ]
        ]
    ];
    SelectionHandle=USelection::SelectionChangedEvent.AddSP(this,&SGuLiMapPanel::SelectionChanged);
    PropertyHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this,&SGuLiMapPanel::PropertyChanged);
    MapHandle=FEditorDelegates::MapChange.AddSP(this,&SGuLiMapPanel::MapChanged);
    auto& AR=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    AssetAddedHandle=AR.OnAssetAdded().AddSP(this,&SGuLiMapPanel::AssetChanged); AssetRemovedHandle=AR.OnAssetRemoved().AddSP(this,&SGuLiMapPanel::AssetChanged); AssetUpdatedHandle=AR.OnAssetUpdated().AddSP(this,&SGuLiMapPanel::AssetChanged);
    GEditor->RegisterForUndo(this); Refresh();
}
SGuLiMapPanel::~SGuLiMapPanel()
{
    GuLiMapEditor::EndInteraction();
    USelection::SelectionChangedEvent.Remove(SelectionHandle); FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyHandle); FEditorDelegates::MapChange.Remove(MapHandle);
    if (auto* M=FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
    { M->Get().OnAssetAdded().Remove(AssetAddedHandle); M->Get().OnAssetRemoved().Remove(AssetRemovedHandle); M->Get().OnAssetUpdated().Remove(AssetUpdatedHandle); }
    if (GEditor) GEditor->UnregisterForUndo(this);
}
void SGuLiMapPanel::QueueRefresh()
{
    if (RefreshQueued) return; RefreshQueued=true;
    RegisterActiveTimer(0,FWidgetActiveTimerDelegate::CreateLambda([this](double,float){RefreshQueued=false; Refresh(); return EActiveTimerReturnType::Stop;}));
}
void SGuLiMapPanel::Refresh()
{
    if (!Service()) return; TGuardValue<bool> Guard(Syncing,true);
    Types.Reset(); for (auto* T:Service()->ListTypes()) Types.Add(T);
    if (!SelectedType.IsValid()&&!Types.IsEmpty()) SelectedType=Types[0]; TypeCombo->RefreshOptions();
    Items.Reset();
    for (auto* A:Service()->LoadedMarkers())
    {
        auto* T=A->Record.Type.LoadSynchronous();
        if (OnlyType&&T!=SelectedType.Get() || OnlyEnabled&&!A->Record.bEnabled) continue;
        if (!Search.IsEmpty() && !(A->Record.MarkerKey.ToString()+A->Record.DisplayName+(T?T->TypeId.ToString():TEXT(""))).Contains(Search)) continue;
        auto Item=MakeShared<FGuLiMarkerListItem>(); Item->Actor=A; Items.Add(Item);
    }
    List->RequestListRefresh(); List->ClearSelection();
    for (const auto& Item:Items) if (Item->Actor.IsValid()&&Item->Actor->IsSelected()) List->SetItemSelection(Item,true);
    TArray<UObject*> Selected; for (FSelectionIterator It(*GEditor->GetSelectedActors());It;++It) if (auto* A=Cast<AGuLiMapMarker>(*It)) Selected.Add(A);
    if (!EditingType) Details->SetObjects(Selected);
}
void SGuLiMapPanel::SelectionChanged(UObject*) { if (!Syncing) { EditingType=false; Details->SetObject(nullptr); QueueRefresh(); } }
void SGuLiMapPanel::PropertyChanged(UObject* O,FPropertyChangedEvent&)
{
    if (O&&O->IsA<AGuLiMapMarker>()) QueueRefresh();
    if (O&&O->IsA<UGuLiMapTypeDefinition>())
    {
        if (Service()) for (auto* A:Service()->LoadedMarkers()) if (A->Record.Type.Get()==O) A->RefreshVisuals();
        Status=TEXT("类型已变更：选中实例 → Synchronize Type；冲突保留旧数据，显式升级才重置。"); QueueRefresh();
    }
}
void SGuLiMapPanel::MapChanged(uint32 Flags) { GuLiMapEditor::EndInteraction(); EditingType=false; Details->SetObject(nullptr); Issues.Reset(); IssueList->RequestListRefresh(); QueueRefresh(); }
void SGuLiMapPanel::AssetChanged(const FAssetData& A) { if (A.AssetClassPath==UGuLiMapTypeDefinition::StaticClass()->GetClassPathName()) QueueRefresh(); }
void SGuLiMapPanel::SelectList(FItem Item,ESelectInfo::Type)
{
    if (Syncing) return; TGuardValue<bool> Guard(Syncing,true);
    GuLiMapEditor::EndInteraction(); EditingType=false; GEditor->SelectNone(false,true); TArray<UObject*> Objects;
    for (const auto& I:List->GetSelectedItems()) if (I->Actor.IsValid()) { GEditor->SelectActor(I->Actor.Get(),true,false); Objects.Add(I->Actor.Get()); }
    GEditor->NoteSelectionChange(); Details->SetObjects(Objects);
}
void SGuLiMapPanel::Focus(FItem Item) { if (Item&&Item->Actor.IsValid()) GEditor->MoveViewportCamerasToActor(*Item->Actor,false); }
void SGuLiMapPanel::ShowResult(const FGuLiMapResult& R)
{
    Issues.Reset(); for (const auto& I:R.Issues) Issues.Add(MakeShared<FGuLiMapIssue>(I));
    IssueList->RequestListRefresh(); Status=R.bSuccess?TEXT("成功"):TEXT("操作未完成，请处理下面的问题。");
    if (!R.Files.IsEmpty()) Status+=TEXT("：")+FPaths::GetPath(R.Files[0]);
}
FReply SGuLiMapPanel::Run(const FString& Action)
{
    auto* S=Service(); if (!S) return FReply::Handled();
    if (Action==TEXT("validate")) ShowResult(S->ValidateMap());
    if (Action==TEXT("save")) Status=S->SaveAuthoringPackages()?TEXT("已保存"):TEXT("保存取消或失败，未导出。");
    if (Action==TEXT("export")) { if (S->SaveAuthoringPackages()) ShowResult(S->ExportMap()); else Status=TEXT("保存取消或失败，未导出。"); }
    if (Action==TEXT("refresh")) Refresh();
    if (Action==TEXT("presets")) { ShowResult(S->EnsurePresets()); Refresh(); }
    if (Action==TEXT("edittype")) { if (SelectedType.IsValid()) { TGuardValue<bool> G(Syncing,true); EditingType=true; List->ClearSelection(); Details->SetObject(SelectedType.Get()); } }
    if (Action==TEXT("newtype"))
    {
        auto* T=S->CreateType(NewTypeId); Status=T?TEXT("类型已创建，请编辑并保存。"):TEXT("创建失败：检查 TypeId 合法性、重复项及目录设置。");
        if (T) {SelectedType=T; EditingType=true; Refresh(); Details->SetObject(T);}
    }
    if (Action==TEXT("place")&&SelectedType.IsValid()) { GuLiMapEditor::BeginPlacement(SelectedType.Get()); Status=TEXT("左键放置；Alt 保留相机；Esc 退出。无表面时使用工作平面。"); }
    if (Action==TEXT("stop")) {GuLiMapEditor::EndInteraction(); Status=TEXT("已退出放置/控制柄编辑。");}
    if ((Action==TEXT("origin")||Action==TEXT("selectedorigin"))&&SelectedType.IsValid())
    {
        FVector P=FVector::ZeroVector;
        if (Action==TEXT("selectedorigin")) { auto* A=GEditor->GetSelectedActors()->GetTop<AActor>(); if (!A) {Status=TEXT("请先选中一个 Actor。"); return FReply::Handled();} P=A->GetActorLocation(); }
        if (auto* A=S->CreateMarker(SelectedType.Get(),P)) { GEditor->SelectNone(false,true); GEditor->SelectActor(A,true,true); QueueRefresh(); }
    }
    if (Action==TEXT("focus")) for (const auto& I:List->GetSelectedItems()) Focus(I);
    if (Action==TEXT("delete")||Action==TEXT("duplicate")||Action==TEXT("enable")||Action==TEXT("disable")||Action==TEXT("addregion"))
    {
        if (!S->EditorWorld()) return FReply::Handled();
        const auto Selected=List->GetSelectedItems(); if (Selected.IsEmpty()) return FReply::Handled();
        GuLiMapEditor::EndInteraction(); const FScopedTransaction Tx(FText::FromString(TEXT("地图标记：")+Action));
        if (Action==TEXT("delete")||Action==TEXT("duplicate"))
        {
            // Restrict native editing to this panel's selected marker set.
            TGuardValue<bool> Guard(Syncing,true); GEditor->SelectNone(false,true);
            for (const auto& I:Selected) if (I->Actor.IsValid()) GEditor->SelectActor(I->Actor.Get(),true,false);
            if (Action==TEXT("delete")) GEditor->edactDeleteSelected(S->EditorWorld(),true,true,true);
            else GEditor->edactDuplicateSelected(S->EditorWorld()->PersistentLevel,false);
            GEditor->NoteSelectionChange();
        }
        else for (const auto& I:Selected) if (auto* A=I->Actor.Get())
        {
            if (Action==TEXT("enable")||Action==TEXT("disable"))
            { ShowResult(S->UpdateMarker(GuLiMap::Guid(A->Record.MarkerId),FString::Printf(TEXT("{\"schema_version\":1,\"enabled\":%s}"),Action==TEXT("enable")?TEXT("true"):TEXT("false")))); }
            if (Action==TEXT("addregion")&&Shape)
            {
                int32 N=1; FName Key;
                do {Key=FName(*FString::Printf(TEXT("Area_%d"),N++));} while (A->Record.Regions.ContainsByPredicate([&](const auto& R){return R.RegionKey==Key;}));
                ShowResult(S->UpdateMarker(GuLiMap::Guid(A->Record.MarkerId),FString::Printf(TEXT("{\"schema_version\":1,\"regions\":[{\"region_key\":\"%s\",\"shape_type\":\"%s\"}]}"),*Key.ToString(),*Shape->ToString())));
            }
        }
        QueueRefresh();
    }
    return FReply::Handled();
}
FReply SGuLiMapPanel::OnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if (List&&List->HasKeyboardFocus()) { if (E.GetKey()==EKeys::F) return Run(TEXT("focus")); if (E.GetKey()==EKeys::Delete) return Run(TEXT("delete")); }
    if (E.GetKey()==EKeys::Escape) return Run(TEXT("stop"));
    return SCompoundWidget::OnKeyDown(G,E);
}
