#pragma once
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "Widgets/Layout/Anchors.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDData.h"
#include "GuLiGroundHUDWidget.generated.h"
class UCanvasPanel;
class UBorder;
class UTextBlock;
class UProgressBar;
class UVerticalBox;
class UHorizontalBox;
class UUniformGridPanel;
class UScrollBox;
class USizeBox;
class UImage;
class UTexture2D;
class UDataTable;
class UGuLiIconCatalog;
class UMaterialInstanceDynamic;
class UGuLiGroundMapWidget;
class UGuLiGroundHUDChrome;
class UGuLiGroundHUDPanel;
class SGuLiGroundReticle;
class AGuLiCommanderPlayerController;
struct FGuLiGroundHUDClip
{
    TWeakObjectPtr<UImage> Image;
    TWeakObjectPtr<UMaterialInstanceDynamic> Material;
    int32 Frames=1;
    float LastFrame=-1,LastOpacity=-1;
};
struct FGuLiGroundHUDMotionStyle
{
    float Intro=.45f,Open=.18f,Close=.12f,Cast=.18f,Ready=.30f,ReadyFade=.10f,Aim=.12f,Slide=18;
    float Shot=.18f,Kick=4,MaxKick=12,Jitter=1.5f,Hit=.16f,Kill=.28f;
};

/** Passive combat HUD + one local modal. No Blueprint bindings or per-control ticks. */
UCLASS()
class GULISTRIKE_API UGuLiGroundHUDWidget final:public UUserWidget
{
    GENERATED_BODY()
public:
    void ToggleMap();
    void CloseModal(bool Immediate=false);
    void SetRogueHidden(bool Hidden);
    void RefreshPawnBinding();
    bool HasModal() const {return ModalMode!=0;}
    /** Structured read-only diagnostics; does not advance animation or gameplay. */
    FString GetMotionDiagnostics() const;
#if WITH_EDITOR
    /** Builds transient widgets only; never edits the player's HUD or acquisition state. */
    UFUNCTION(BlueprintCallable,Category="Ground|Editor")
    static FString ValidateIconLayouts();
#endif
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry&,float) override;
    virtual FReply NativeOnKeyDown(const FGeometry&,const FKeyEvent&) override;
private:
    friend class SGuLiGroundReticle;
    void BuildLayout();
    void Refresh();
    void RefreshPeriodic();
    void Apply(EGuLiGroundHUDGroup Groups);
    void UpdateRogueCards(const TArray<FGuLiGroundHUDCard>& Cards);
    void Hit(bool Kill);
    void BuildMotionLayout(UCanvasPanel* Design,UWidget* Header);
    void BuildSkillMotion(UCanvasPanel* Canvas,int32 SkillIndex);
    void BuildAmbientLoop(UCanvasPanel* Canvas,int32 Index,FVector2D Position);
    void UpdateAmbient(bool ForcePause=false);
    void LoadMotionStyle();
    FGuLiGroundHUDClip MakeClip(const TCHAR* Name,const TCHAR* Material=TEXT("MotionAtlasMaterial"));
    void ShowClip(FGuLiGroundHUDClip& Clip,float Frame,float Opacity);
    void Motion(const FGuLiGroundHUDMotion& Event);
    void TickMotion(double Now,float Delta);
    void ClearMotion();
    void StartEntry();
    void FinishCloseModal();
    float ShotExpansion(double Now) const;
    void OpenModal(uint8 Mode);
    void UpdateReticle();
    void SelectMarker(const FGuLiGroundHUDMarker* Marker);
    UFUNCTION() void RefreshKeyLabels();
    UTextBlock* Text(const FString& Key,int32 Size=18);
    UImage* Icon(FName Name,FVector2D Size);
    void PrepareIcons();
    UImage* DataIcon(int32 Id,FVector2D Size);
    void SetDataIconTexture(UImage* Image,UTexture2D* Texture);
    int32 StyledIconId(FName Name) const;
    void UpdateSkillIcons(const FGuLiGroundHUDSnapshot& View);
    UWidget* Glyph(FName Name);
    FLinearColor Color(FName Name) const;
    float Number(FName Name,float Default) const;
    AGuLiCommanderPlayerController* Controller() const;
    UFUNCTION() void CloseClicked();
    UFUNCTION() void CenterClicked();
    UFUNCTION() void MapClicked();
    UFUNCTION() void RogueClicked();
    UPROPERTY(Transient) TObjectPtr<UGuLiGroundHUDData> Data;
    UPROPERTY(Transient) TObjectPtr<UDataTable> Style;
    UPROPERTY(Transient) TObjectPtr<UGuLiIconCatalog> IconCatalog;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SkillIcons;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UImage>> RogueImages;
    TMap<int32,int32> BuffIconIds;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Art;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Root;
    UPROPERTY(Transient) TObjectPtr<USizeBox> DesignSize;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Battle;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Modal;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MapContent;
    UPROPERTY(Transient) TObjectPtr<UGuLiGroundHUDChrome> Chrome;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HealthText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HealthMaximum;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ShieldText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FuelText;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StatValues;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MapDistance;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MapCoordinates;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MapAffiliation;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MapInfo;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MapTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MapHelp;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MiniMapStatus;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY(Transient) TObjectPtr<UUniformGridPanel> RogueList;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> RogueScroll;
    UPROPERTY(Transient) TObjectPtr<UBorder> RoguePanel;
    UPROPERTY(Transient) TObjectPtr<UBorder> RogueOffer;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UTextBlock>> RogueLevels;
    TArray<FString> RogueIds;
    bool bRogueGridInitialized=false;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> BuffList;
    UPROPERTY(Transient) TObjectPtr<UGuLiGroundMapWidget> MiniMap;
    UPROPERTY(Transient) TObjectPtr<UGuLiGroundMapWidget> FullMap;
    UPROPERTY(Transient) TObjectPtr<UGuLiGroundMapWidget> MapPreview;
    UPROPERTY(Transient) TObjectPtr<UBorder> MapSide;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> FuelSegments;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SkillStates;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> KeyLabels;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> SkillMasks;
    UPROPERTY(Transient) TArray<TObjectPtr<UGuLiGroundHUDPanel>> SkillPanels;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BuffTimes;
    TWeakObjectPtr<APawn> BoundPawn;
    FTimerHandle RefreshTimer;
    FDelegateHandle UrgentHandle,HitHandle,MotionHandle;
    TSharedPtr<SGuLiGroundReticle> Reticle;
    FVector2D MuzzlePoint=FVector2D::ZeroVector;
    bool bMuzzle=false,bKill=false,bRogueHidden=false,bReticleVisible=false;
    float TargetHealth=0,TargetMasks[4]{};bool bBarsInitialized=false;
    double HitUntil=0,HitStarted=0;uint8 ModalMode=0;
    FString LastBuffText;
    FGuLiGroundHUDMotionStyle MotionStyle;
    FGuLiGroundHUDClip EntryClip,ModalClip,CastClips[4],ReadyClips[4],CooldownClip;
    FGuLiGroundHUDClip AmbientClips[2];
    double AmbientPhase=0,AmbientStarted=0;
    bool bAmbientRunning=false;
    uint32 AmbientTransitions=0;
    TArray<TWeakObjectPtr<UWidget>> EntryPanels;
    TArray<FVector2D> EntryDirections;
    double EntryStarted=-1,ModalStarted=-1,CastStarted[4]{-1,-1,-1,-1},ReadyStarted[4]{-1,-1,-1,-1};
    double ShotStarted=-1;float ShotKick=0,HeldAlpha[4]{},ModalAlpha=0,ModalFrom=0;
    bool bModalClosing=false,bObservedHealth=false,bWasDead=false,bHeldAim=false,bHeldJet=false,bCooling=false;
    bool bReticleMotionActive=false;
    double BlinkEndsAt=0;float BlinkDuration=0,LastCooldownProgress=-1;
    uint64 ObservedGeneration=0;
    uint32 EntryPlays=0,ShotPulses=0,CastPlays[4]{},ReadyPlays[4]{},HitPlays=0,KillPlays=0,RegularRefreshes=0,MotionFrames=0;
};
