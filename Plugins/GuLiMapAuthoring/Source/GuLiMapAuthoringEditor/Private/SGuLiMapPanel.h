#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "EditorUndoClient.h"
#include "GuLiMapTypes.h"

class IDetailsView;
class AGuLiMapMarker;
class AGuLiMapDensityMap;
class UGuLiMapAuthoringSubsystem;
template<typename T> class SListView;
template<typename T> class SComboBox;
struct FGuLiMarkerListItem { TWeakObjectPtr<AGuLiMapMarker> Actor; };

class SGuLiMapPanel : public SCompoundWidget, public FEditorUndoClient
{
public:
    SLATE_BEGIN_ARGS(SGuLiMapPanel) {} SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    ~SGuLiMapPanel() override;
    void PostUndo(bool Success) override { QueueRefresh(); }
    void PostRedo(bool Success) override { QueueRefresh(); }
    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    using FItem=TSharedPtr<FGuLiMarkerListItem>;
    TSharedPtr<SListView<FItem>> List;
    TSharedPtr<SListView<TSharedPtr<FGuLiMapIssue>>> IssueList;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SComboBox<TWeakObjectPtr<UGuLiMapTypeDefinition>>> TypeCombo;
    TArray<FItem> Items;
    TArray<TSharedPtr<FGuLiMapIssue>> Issues;
    TArray<TWeakObjectPtr<UGuLiMapTypeDefinition>> Types;
    TWeakObjectPtr<UGuLiMapTypeDefinition> SelectedType;
    TArray<TSharedPtr<FName>> Shapes;
    TSharedPtr<FName> Shape;
    FString Search,NewTypeId,Status;
    FName DensityLayer=TEXT("BlueOre");
    double BrushRadiusCm=20000.0,BrushStrength01=0.25,BrushFalloff01=0.5,DensityCellSizeCm=2500.0;
    bool OnlyType=false,OnlyEnabled=false,Syncing=false,RefreshQueued=false,EditingType=false,PaintView=false,EraseDensity=false,BlueVisible=true,RedVisible=true;
    FDelegateHandle SelectionHandle,PropertyHandle,MapHandle,AssetAddedHandle,AssetRemovedHandle,AssetUpdatedHandle;
    UGuLiMapAuthoringSubsystem* Service() const;
    AGuLiMapDensityMap* DensityMap() const;
    void UpdateDensityBrush();
    void NotifyDensityChanged(AGuLiMapDensityMap* Actor,bool InvalidateSurface=false);
    void Refresh();
    void QueueRefresh();
    void SelectionChanged(UObject* Object);
    void PropertyChanged(UObject* Object,FPropertyChangedEvent& Event);
    void MapChanged(uint32 Flags);
    void AssetChanged(const struct FAssetData& Asset);
    void SelectList(FItem Item,ESelectInfo::Type How);
    void Focus(FItem Item);
    void ShowResult(const FGuLiMapResult& Result);
    FReply Run(const FString& Action);
};
