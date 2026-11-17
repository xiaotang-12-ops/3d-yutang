// 调查镜头先在远方转向并展示光效，再靠近；抵达时钟独立供HUD淡出，预检复用同一轨迹。-codex，2026-11-02
#pragma once
#include "PondCameraMotion.h"
class FSurveyCameraFlight
{
public:
    // 节奏统一供状态、HUD与验收使用；先远景亮起，约一秒完成退场。-codex，2026-11-02
    static constexpr float OrientDuration=1.25f;
    static constexpr float BeaconDuration=.70f; // 远处完整亮起后再靠近。-codex，2026-11-03
    static constexpr float ApproachDuration=2.8f;
    static constexpr float FadeDuration=.85f;
    enum class EStage { Idle, Orient, AwaitVisible, Beacon, Approach, Complete, Failed };
    void Start(const FPondCameraPose& Current,const FPondCameraPose& Destination,const FVector& Water,const FVector& TurnEye);
    FPondCameraPose Advance(float DeltaSeconds,bool TargetVisible);
    void Cancel();
    void Reject(const FPondCameraPose& Current);
    bool IsActive() const {return Stage>=EStage::Orient&&Stage<=EStage::Approach;}
    EStage GetStage() const {return Stage;}
    const TCHAR* Phase() const;
    float BeaconAge() const {return LightAge;}
    float ArrivalAge() const {return SettledAge;}
    const FPondCameraPose& GetPose() const {return Pose;}
    static FVector Eye(const FPondCameraPose& P);
    static FPondCameraPose Look(const FVector& Eye,const FVector& Focus,float FallbackYaw);
    static FPondCameraPose Travel(const FVector& FromEye,const FVector& FromFocus,const FPondCameraPose& Destination,float T);
private:
    EStage Stage=EStage::Idle;
    FPondCameraPose Pose,StartPose,Target;
    FVector WaterPoint,StartEye,Turn;
    FQuat StartRotation,TurnRotation;
    float Elapsed=0,LightAge=-1,SettledAge=-1;
};
