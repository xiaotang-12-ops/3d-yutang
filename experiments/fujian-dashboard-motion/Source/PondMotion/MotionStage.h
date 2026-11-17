// 第二版隔离塘体舞台：只管理示意几何、受光和相机，不拥有经营数据或Slate文字。-codex，2026-10-23
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RenderCommandFence.h"
#include "MotionCarousel.h"
#include "ProceduralMeshComponent.h"
#include "MotionStage.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPointLightComponent;
class USkyLightComponent;
class UTextureCube;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMediaPlayer;
class UMediaTexture;
class UImgMediaSource;

enum class EMotionStageView : uint8 { Hidden, City, Pond, DataOverview };

// 六档仅为主体示意样式；CPU节缓存不创建额外场景组件，不代表真实塘界。-codex，2026-10-31
struct FMotionCityShapeCache
{
    TArray<FProcMeshSection> Sections;
    double Width=1630., Near=0., Far=0.;
};

UCLASS()
class AMotionStage : public AActor
{
    GENERATED_BODY()
public:
    AMotionStage();
    virtual void Tick(float DeltaSeconds) override;
    void SetPresentation(EMotionStageView View, float EntryProgress, int32 SiteSeed, const TArray<FString>& CityIds);
    void SetCityCarouselFrame(const MotionCarousel::FFrame& Frame);
    void SetSpritePondsEnabled(bool Enabled){bSpritePondsEnabled=Enabled;}

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void BuildGeometry();
    void BuildCityGeometry(bool bCarousel = false);
    void BuildCarouselGeometryCache();
    void BindCarouselStyle(int32 Slot, int32 SubjectIndex);
    void UpdatePose();
    void UpdateCityPose();
    void UpdateCarouselPose();
    void UpdateEnvironmentPose();
    void UpdateWaterlight();
    void InitializeBackgroundVideo();
    void UpdateBackgroundVideo();
    bool IsBackgroundFullyCached() const;
    void FailBackgroundVideo(const TCHAR* Reason);
    UMaterialInterface* LoadStageMaterial(const TCHAR* Name);

    UPROPERTY() TObjectPtr<USceneComponent> StageRoot;
    UPROPERTY() TObjectPtr<USceneComponent> PondRoot;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Bank;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Water;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Bed;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Cut;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Plants;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Floor;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Environment;
    UPROPERTY() TObjectPtr<UMaterialInterface> PondEnvironmentMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> DataEnvironmentMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PondEnvironmentDynamic;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DataEnvironmentDynamic;
    UPROPERTY() TObjectPtr<UMediaPlayer> BackgroundPlayer;
    UPROPERTY() TObjectPtr<UMediaTexture> BackgroundVideoTexture;
    UPROPERTY() TObjectPtr<UImgMediaSource> BackgroundSequenceSource;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> KeyLight;
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> FillLight;
    UPROPERTY() TObjectPtr<UPointLightComponent> RimLight;
    UPROPERTY() TObjectPtr<USkyLightComponent> AmbientLight;
    UPROPERTY() TObjectPtr<UTextureCube> AmbientCubemap;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> LoadedMaterials;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> CityRoots;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> CityMeshes;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> DataWaterlightMaterials;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> CarouselMaterials;

    EMotionStageView CurrentView = EMotionStageView::Hidden;
    MotionCarousel::FFrame CarouselFrame;
    bool bCarouselGeometry = false;
    bool bSpritePondsEnabled = false;
    bool bCarouselMaterialsReady = false;
    bool bCarouselFrameApplied = false;
    uint64 AppliedCarouselSerial = 0;
    float AppliedCarouselEntry = -1.f;
    TArray<FMotionCityShapeCache> CarouselShapes;
    int32 CarouselSlotStyles[MotionCarousel::PoolSize] = {-1,-1,-1,-1,-1,-1,-1};
    int32 CarouselLayerLogs = 0, CarouselLoggedFocus = INDEX_NONE;
    TArray<FString> CurrentCityIds;
    double CityAges[MotionCarousel::PoolSize] = {};
    float CityPhases[MotionCarousel::PoolSize] = {};
    float Entry = 0.f;
    double MotionTime = 0.0;
    float SitePhase = 0.f;
    int32 CurrentSiteSeed = INDEX_NONE;
    enum class EBackgroundState : uint8 { Loading, Starting, Playing, Failed };
    EBackgroundState BackgroundState = EBackgroundState::Loading;
    FRenderCommandFence BackgroundFrameFence;
    bool bBackgroundFencePending = false;
    bool bBackgroundMetadataChecked = false;
    bool bBackgroundCacheRechecked = false;
    double BackgroundLoadStarted = 0.;
    double BackgroundPlayStarted = 0.;
    double BackgroundLastTime = -1.;
    int32 BackgroundLoopLogs = 0;
    FString BackgroundTimingRows;
    double BackgroundTimingLastFlush = 0.;
    int32 BackgroundTimingSamples = 0;
};
