// 原生 UE5 运行时：共享数据网格、省市状态、相机拾取和简易 HUD。
// 目标 UE 5.6；运行和打包验证状态以项目 README 为准。-codex，2026-10-16
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "PondCameraMotion.h"
#include "FujianMap.generated.h"

class ACameraActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class FPondPresentation;

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
private:
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
    virtual bool InputTouch(FInputDeviceId DeviceId,uint32 Handle,ETouchType::Type Type,const FVector2D& TouchLocation,float Force,uint32 TouchpadIndex,uint64 Timestamp) override;
    virtual void FlushPressedKeys() override;
    virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override { return bShowcase||Super::ShouldFlushKeysWhenViewportFocusChanges(); }
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void FitMap(bool bTop = false);
    void Home();
    void Back();
    void CancelCamera(const TCHAR* Reason);
    bool IsShowcase() const { return bShowcase; }
    const TCHAR* CameraPhase() const { return Motion.Phase(); }
    void OpenData();
    FString DataWindowStatus() const;
    UPROPERTY() TObjectPtr<AFujianMap> Map;
private:
    void FocusSelection(bool bTop = false, bool bOrbit = true, bool bPreservePitch = false);
    FPondCameraPose CurrentPose() const;
    void ApplyPose(const FPondCameraPose& Pose);
    FPondCameraMotion Motion;
    TSharedPtr<FPondPresentation> Presentation;
    bool bShowcase = false;
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
    uint32 ActiveTouchHandle = MAX_uint32;
    FIntPoint FittedViewport = FIntPoint::ZeroValue;
};

UCLASS()
class FUJIANSANDBOX_API AFujianHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    bool ClickAt(FVector2D Position);
private:
    void DrawShowcase();
    bool ClickShowcase(FVector2D Position);
    TArray<FBox2D> CityButtons;
    TArray<FBox2D> SiteButtons;
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
