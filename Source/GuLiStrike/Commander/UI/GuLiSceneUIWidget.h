#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GuLiSceneUIWidget.generated.h"

class AGuLiCommanderPlayerController;
class APlayerController;
class AGuLiCommanderPresentationActor;
class AGuLiCommanderHealthBarRenderer;
class AGuLiBattlePlayerState;
class ASceneCapture2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class UTexture2D;
class UTexture;
class UGuLiCommanderRouteLineComponent;
class UGuLiOutpostPresentationComponent;
class SGuLiSceneUI;
struct FGuLiSceneUIWidgetData;

/** One non-interactive, post-scene Slate batch for each local player's viewport. */
UCLASS(Config=Game, NotBlueprintable)
class GULISTRIKE_API UGuLiSceneUIWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	UGuLiSceneUIWidget(const FObjectInitializer& Initializer);
	void InitializeForController(APlayerController* Controller);
	void SetModalHidden(bool bHidden);
	void BeginHUDFrame();
	void AddScreenLine(float X1, float Y1, float X2, float Y2, FLinearColor Color, float Width=1);
	void AddScreenRect(FLinearColor Color, float X, float Y, float W, float H);
	void AddScreenDisc(FVector2D Center, float Radius, FLinearColor Color);
	void AddScreenImage(UTexture2D* Texture, FVector2D Position, FVector2D Size);
	void AddScreenText(const FString& Text, FLinearColor Color, float X, float Y);
	void AddWorldLine(const FVector& Start, const FVector& End, FLinearColor Color, float Width=1);
	bool ProjectWorldLineToScreen(const FVector& Start, const FVector& End, FVector2D& A, FVector2D& B) const;
	FVector2D ToPlayerScreen(const FVector2D& ViewportPosition) const;
	void PaintSceneUI(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual void NativeDestruct() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	UFUNCTION() void HandleIdentityChanged();
	void RefreshSources();
	void RefreshOutline(float DeltaSeconds);
	void RefreshPlacement();
	void RefreshWorldWidgets();
	UMaterialInstanceDynamic* GetSurfaceInstance(UTexture* Texture);
	void DestroyOutlineCapture();
	void DestroyPlacementCapture();
	UPROPERTY(Config) TSoftObjectPtr<UMaterialInterface> EnemyOutlineMaterial;
	UPROPERTY(Config) TSoftObjectPtr<UMaterialInterface> WidgetSurfaceMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OutlineMaterialInstance;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> OutlineTarget;
	UPROPERTY(Transient) TObjectPtr<ASceneCapture2D> OutlineCapture;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PlacementMaterialInstance;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> PlacementTarget;
	UPROPERTY(Transient) TObjectPtr<ASceneCapture2D> PlacementCapture;
	UPROPERTY(Transient) TObjectPtr<UGuLiCommanderRouteLineComponent> RouteData;
	UPROPERTY(Transient) TMap<TObjectPtr<UTexture>, TObjectPtr<UMaterialInstanceDynamic>> SurfaceInstances;
	TWeakObjectPtr<APlayerController> LocalController;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> Presentation;
	TWeakObjectPtr<AGuLiCommanderHealthBarRenderer> HealthBars;
	TWeakObjectPtr<AGuLiBattlePlayerState> BoundIdentity;
	TArray<TWeakObjectPtr<AActor>> ActorRingSources;
	TArray<TWeakObjectPtr<UGuLiOutpostPresentationComponent>> OutpostHaloSources;
	TSharedPtr<SGuLiSceneUI> SceneSlate;
	TSharedPtr<FGuLiSceneUIWidgetData> Data;
	float SourceRefreshRemaining = 0;
	float OutlineListRefreshRemaining = 0;
	bool bModalHidden = false;
};
