// R15省览独立光效与有限飞行；复用真实地形和逐市材质实例，不改变地理坐标。
// 所有选择仍属于Map，关闭ProvinceFX即回到静态省览。-codex，2026-10-27
#pragma once
#include "CoreMinimal.h"
#include "PondCameraMotion.h"
class AFujianController;
class UCanvas;
class FProvinceFXCapture;

class FProvinceEffects
{
public:
    explicit FProvinceEffects(AFujianController* Controller);
    ~FProvinceEffects();
    bool Initialize();
    void Tick(float Delta);
    void Draw(UCanvas* Canvas);
    void Select(int32 City);
    void ReframeCity();
    void Cancel();
    void Resize();
    float Clock() const {return Time;}
    bool IsFlying() const {return Flying;}
    void PreviewPointer(FVector2D Position);
    int32 PreviewCity() const {return HoverCity;}
    float PreviewStrength() const {return HoverAlpha;}
private:
    // 显式离屏取证直接核对现有取景目标，避免“导航变了但相机未到位”的假通过。-codex，2026-10-30
    friend class FProvinceFXCapture;
    struct FRing {TArray<FVector> Points;TArray<float> Arc;float Length=0;int32 City=INDEX_NONE;};
    struct FSurfacePatch {FVector A,B,C;int32 City=INDEX_NONE;};
    void BuildSurface();
    TWeakObjectPtr<AFujianController> Owner;
    TArray<FRing> Rings;
    TArray<FSurfacePatch> Surface;
    FBox ProvinceBounds=FBox(ForceInit);
    TUniquePtr<FProvinceFXCapture> Capture;
    FPondCameraPose Home,Start,Sky,Dive,Target,CancelledPose;
    float Time=0,FlightTime=0,SelectionTime=-100,MaxTerrainZ=254;
    int32 RetiringCity=INDEX_NONE;
    float RetireTime=-100,RetireAge=0;
    int32 HoverCity=INDEX_NONE,HoverTarget=INDEX_NONE,RelayCity=INDEX_NONE;
    float HoverAlpha=0;
    float NearFocus=0;
    float HoverTime=0,RepeatTime=-100;
    float ShellStart=-1;
    FVector HoverOrigin=FVector::ZeroVector;
    FBox2D HoverLabel=FBox2D(ForceInit);
    bool Flying=false,Returning=false,CanResume=false;
    FPondCameraPose CityPose(int32 Index) const;
    void BeginFlight(const FPondCameraPose& To,bool Return);
};
