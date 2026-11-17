// 原生 UE5 运行时：共享数据网格、省市状态、相机拾取和简易 HUD。
// 当前副本目标 UE 5.8；运行和打包验证以A专项计划与主控实际日志为准。-codex，2026-10-22
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "PondCameraMotion.h"
#include "SurveyCameraFlight.h"
#include "FujianMap.generated.h"

class ACameraActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class FPondPresentation;
class FProvinceEffects;
class FProvinceFXCapture;
class FXinluoSurveyControl;
namespace Xinluo { struct FDataset; }
class FSurveyFarmScene;

struct FDemoSite
{
    FString Id;
    FString Name;
    FString Kind;
    FVector Position = FVector::ZeroVector;
    int32 AreaMu = 0;
};

struct FDemoCity
{
    int32 Code = 0;
    FString Name;
    FString English;
    FVector Center = FVector::ZeroVector;
    FBox Bounds = FBox(ForceInit);
    TArray<FDemoSite> Sites;
    TWeakObjectPtr<AActor> Region;
    TWeakObjectPtr<UProceduralMeshComponent> Terrain;
    TWeakObjectPtr<UProceduralMeshComponent> Skirt;
};

UCLASS()
class FUJIANSANDBOX_API AFujianMap : public AActor
{
    GENERATED_BODY()
public:
    AFujianMap();
    virtual void BeginPlay() override;
    void ShowCity(int32 Index);
    void ShowProvince();
    void Pick(AActor* Actor);
    void SelectSite(int32 Index);
    FBox VisibleBounds() const;
    TArray<FDemoCity> Cities;
    int32 SelectedCity = INDEX_NONE;
    int32 SelectedSite = INDEX_NONE;
    FString Error;
    int32 Total = 0;
    float HeightExaggeration = 1.f;
    TWeakObjectPtr<AActor> ProvinceDecor;
    bool BuildSurveyFarms(const Xinluo::FDataset& Dataset,FString& BuildError);
    void GetSurveyFarmSites(TArray<FString>& Ids,TArray<FVector>& Centers,TArray<float>& Radii) const;
    bool GetSurveyFarmFocus(const FString& Id,FVector& Center,float& Radius) const;
    bool GetSurveyFarmWaterAnchor(const FString& Id,FVector& Anchor,FVector2D& HalfExtent) const;
    void SetSurveyFarmsVisible(bool Visible);
    bool SetSurveyFarmSelection(const FString& Id,FString& Error);
    uint32 SurveyFarmGeometryRevision() const;
    bool IsSurveyFarmDisplayed(const FString& Id) const;
    bool TraceSurveyFarmTerrain(FHitResult& Hit,const FVector& Start,const FVector& End) const;
private:
    TSharedPtr<FSurveyFarmScene> SurveyFarmScene;
    void ClearMarkers();
    TArray<TWeakObjectPtr<AActor>> Markers;
    UPROPERTY() TObjectPtr<UMaterialInterface> MarkerMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> SelectedMarkerMaterial;
};

UCLASS()
class FUJIANSANDBOX_API AFujianController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    virtual bool InputTouch(FTouchId TouchId,ETouchType::Type Type,const FVector2D& TouchLocation,float Force,uint64 Timestamp) override;
    virtual void FlushPressedKeys() override;
    virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override { return bShowcase||Super::ShouldFlushKeysWhenViewportFocusChanges(); }
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void FitMap(bool bTop = false);
    void Home();
    void Back();
    void CancelCamera(const TCHAR* Reason);
    void FocusSurveyPoint(const FString& SubjectId,const FVector& WorldPoint);
    void ReframeSurveyCity();
    bool IsShowcase() const { return bShowcase; }
    // 省览是当前默认A画面；旧下钻/双屏仅由显式启动参数保留。-codex，2026-10-27
    bool IsProvinceOnly() const { return bShowcase && !bFullControls; }
    bool IsProvinceFX() const {return bProvinceFX;}
    void SelectProvinceCity(int32 Index);
    void DrawProvinceFX(UCanvas* Canvas);
    const TCHAR* CameraPhase() const { return SurveyFocusSubject.IsEmpty()?Motion.Phase():SurveyFlight.Phase(); }
    float SurveyBeaconAge(const FString& Id) const {return SurveyFocusSubject==Id?SurveyFlight.BeaconAge():-1.f;}
    float SurveyArrivalAge(const FString& Id) const {return SurveyFocusSubject==Id?SurveyFlight.ArrivalAge():-1.f;}
    float SurveyBeaconHeight(const FString& Id) const {return SurveyFocusSubject==Id?SurveyBeamHeight:0.f;}
    float SurveyBeaconRadius(const FString& Id) const {return SurveyFocusSubject==Id?SurveyBeamRadius:0.f;}
    float SurveyBeaconYaw(const FString& Id) const {return SurveyFocusSubject==Id?SurveyBeamYaw:0.f;}
    FString SurveyCameraStatus() const {return SurveyFlight.GetStage()==FSurveyCameraFlight::EStage::Failed?TEXT("暂时无法安全定位，请调整视角后重选"):FString();}
    void OpenData();
    FString DataWindowStatus() const;
    // 调查控件和输入共用同一导航源，尚未批准的控件布局不在此处生成。-codex，2026-10-28
    TSharedPtr<FXinluoSurveyControl> SurveyControl;
    bool IsSurveyLinked() const { return bSurveyLinked; }
    UPROPERTY() TObjectPtr<AFujianMap> Map;
private:
    friend class FProvinceEffects;
    friend class FProvinceFXCapture;
    TSharedPtr<FProvinceEffects> ProvinceEffects;
    bool bProvinceFX=false;
    void FocusSelection(bool bTop = false, bool bOrbit = true, bool bPreservePitch = false);
    FPondCameraPose CurrentPose() const;
    void ApplyPose(const FPondCameraPose& Pose);
    FPondCameraMotion Motion;
    bool bSurveyFocusMotion=false;
    FSurveyCameraFlight SurveyFlight;
    FString SurveyFocusSubject;
    FVector SurveyWaterAnchor=FVector::ZeroVector;
    FVector2D SurveyWaterHalfExtent=FVector2D::ZeroVector;
    FString SurveyVisibilityFailure;
    float SurveyBeamRadius=0,SurveyBeamYaw=0;
    float SurveyBeamHeight=0; // 本次点选固定世界高度，不按相机距离逐帧重算。-codex，2026-11-02
    float SurveyWaterSpan=0; // 逻辑1920画布上的真实水面跨度，不能只用屏内点冒充可辨。-codex，2026-11-02
    bool IsSurveyWaterVisible(bool RequireReadable=true);
    void RejectSurveyFocus(const FString& SubjectId,const TCHAR* Reason);
    TSharedPtr<FPondPresentation> Presentation;
    bool bShowcase = false;
    bool bFullControls = false;
    bool bSurveyLinked = false;
    FString ProvinceCapturePath;
    bool bProvinceCaptureRequested = false;
    float ProvinceCaptureTime = 0.f;
    int32 FramedCity = INDEX_NONE;
    int32 FramedSite = INDEX_NONE;
    double NextMotionLog = 0;
    void UpdatePointerDrag(FVector2D Position);
    void PickAt(FVector2D Position);
    UPROPERTY() TObjectPtr<ACameraActor> MapCamera;
    FVector Focus = FVector::ZeroVector;
    FVector WantedFocus = FVector::ZeroVector;
    float Distance = 6500.f;
    float WantedDistance = 6500.f;
    float Yaw = 180.f;
    float Pitch = 55.f;
    FVector2D PressPosition = FVector2D::ZeroVector;
    FVector2D PreviousPosition = FVector2D::ZeroVector;
    float DragDistance = 0.f;
    double LastActivity = 0;
    bool bPointerDown = false;
    bool bTouchDown = false;
    bool bSurveyPointer = false;
    // UE5.8 触点身份包含设备与手指，跨屏同序号触点不能接管当前单触点操作。-codex，2026-10-22
    FTouchId ActiveTouchId;
    FIntPoint FittedViewport = FIntPoint::ZeroValue;
};

UCLASS()
class FUJIANSANDBOX_API AFujianHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    bool ClickAt(FVector2D Position);
    bool SurveyHit(FVector2D Position) const;
    bool SurveyBlocksWheel(FVector2D Position) const;
    bool SurveyListAction(FVector2D Position) const;
    bool ScrollSurveyList(float WheelDelta,FVector2D Position);
    bool ClickSurvey(FVector2D Position);
private:
    friend class AFujianController;
    friend class FProvinceFXCapture;
    friend class FProvinceEffects;
    void DrawShowcase();
    void DrawProvinceOnly();
    void DrawSurvey();
    FBox2D SurveyPanel,SurveyBack,SurveyOverview;
    TArray<FBox2D> SurveyRows;
    TArray<FString> SurveyIds;
    // 仅缓存地表锚点与每帧命中派生值，业务选择仍由SurveyControl唯一维护。-codex，2026-10-29
    TMap<FString,FVector> SurveyWorldPoints;
    FString SurveyPointVersion,SurveyHovered;
    TArray<FBox2D> SurveyPointHits;
    TArray<FBox2D> SurveyLabelHits;
    TMap<FString,float> SurveyLabelAlpha;
    TSet<FString> SurveyNearLabels;
    // 仅为波纹代表点及单次点击特效的显示缓存，业务选择仍读取SurveyControl。-codex，2026-10-31
    TSet<FString> SurveyRippleIds;
    FBox2D SurveySelector,SurveyToggle;
    bool bSurveyListExpanded=true;
    static constexpr int32 SurveyVisibleRows=6;
    int32 SurveyFirstRow=0;
    bool ClickShowcase(FVector2D Position);
    TArray<FBox2D> CityButtons;
    TArray<FBox2D> SiteButtons;
    // 仅记录本帧实际投影点的触摸范围；选择仍直接调用Map，不创建另一套塘位状态。-codex，2026-10-22
    TArray<FBox2D> SiteMarkerButtons;
    // 快捷入口与地图标签分别保存实际绘制框，避免索引面板覆盖地图命中区域。-codex，2026-10-22
    TArray<FBox2D> QuickSiteButtons;
    FBox2D ProvinceButton;
    FBox2D HomeButton;
    FBox2D ResetButton;
    FBox2D TopButton;
    FBox2D DataButton;
};

UCLASS()
class FUJIANSANDBOX_API AFujianSandboxGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AFujianSandboxGameMode();
};
