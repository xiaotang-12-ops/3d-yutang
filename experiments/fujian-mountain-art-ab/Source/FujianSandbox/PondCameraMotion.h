// 有限相机轨迹：定位、短幅环绕、保持；取消时冻结所有通道，新的目标覆盖旧轨迹。
// 不依赖Actor或输入设备，便于单独验证中断、最短角和完成后的稳定性。-codex，2026-10-21
#pragma once
#include "CoreMinimal.h"

struct FPondCameraPose
{
    FVector Focus = FVector::ZeroVector;
    float Distance = 6500.f;
    float Yaw = 180.f;
    float Pitch = 38.f;
};

class FPondCameraMotion
{
public:
    void Start(const FPondCameraPose& Current, const FPondCameraPose& Destination, float OrbitDegrees = 24.f);
    FPondCameraPose Advance(float DeltaSeconds);
    FPondCameraPose Cancel();
    void SetPose(const FPondCameraPose& Value);
    const FPondCameraPose& GetPose() const { return Pose; }
    bool IsActive() const { return bActive; }
    const TCHAR* Phase() const;
private:
    FPondCameraPose Pose, StartPose, Target;
    float Elapsed = 0.f;
    float Orbit = 0.f;
    bool bActive = false;
    static constexpr float ApproachSeconds = 1.5f;
    static constexpr float OrbitSeconds = 3.2f;
    // 环绕借用定位尾部.45秒，同时延长自身跨度，保持总时长及最终构图。-codex，2026-10-23
    static constexpr float OverlapSeconds = .45f;
};
