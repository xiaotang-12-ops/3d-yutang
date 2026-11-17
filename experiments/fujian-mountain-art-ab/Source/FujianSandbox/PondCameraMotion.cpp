// 使用有终点的平滑曲线，避免追近插值无限趋近和松手后旧轨迹重新接管。-codex，2026-10-21
#include "PondCameraMotion.h"

namespace
{
    float Smooth(float T) { T=FMath::Clamp(T,0.f,1.f); return T*T*(3.f-2.f*T); }
    // 环绕提前进入时不突加角加速度；定位保持旧曲线，避免顺带改变推近节奏。-codex，2026-10-23
    float SmoothOrbit(float T)
    {
        const double U=FMath::Clamp(static_cast<double>(T),0.,1.);
        return static_cast<float>(U*U*U*(U*(U*6.-15.)+10.));
    }
}

void FPondCameraMotion::Start(const FPondCameraPose& Current,const FPondCameraPose& Destination,float OrbitDegrees)
{
    StartPose=Pose=Current;Target=Destination;
    Target.Yaw=Current.Yaw+FMath::FindDeltaAngleDegrees(Current.Yaw,Destination.Yaw);
    Orbit=FMath::Clamp(OrbitDegrees,-30.f,30.f);Elapsed=0.f;bActive=true;
}
FPondCameraPose FPondCameraMotion::Advance(float DeltaSeconds)
{
    if(!bActive)return Pose;
    Elapsed+=FMath::Max(0.f,DeltaSeconds);
    const float T=Smooth(Elapsed/ApproachSeconds);
    Pose.Focus=FMath::Lerp(StartPose.Focus,Target.Focus,T);
    Pose.Distance=FMath::Lerp(StartPose.Distance,Target.Distance,T);
    Pose.Pitch=FMath::Lerp(StartPose.Pitch,Target.Pitch,T);
    // 重叠只提前环绕起点，终点仍为原4.7秒；不增加转角或持续追转。-codex，2026-10-23
    const float OrbitStart=ApproachSeconds-OverlapSeconds;
    const float OrbitSpan=OrbitSeconds+OverlapSeconds;
    Pose.Yaw=FMath::Lerp(StartPose.Yaw,Target.Yaw,T)+Orbit*SmoothOrbit((Elapsed-OrbitStart)/OrbitSpan);
    const float Duration=ApproachSeconds+(FMath::IsNearlyZero(Orbit)?0.f:OrbitSeconds);
    if(Elapsed>=Duration){Pose=Target;Pose.Yaw+=Orbit;bActive=false;}
    return Pose;
}
FPondCameraPose FPondCameraMotion::Cancel(){bActive=false;return Pose;}
void FPondCameraMotion::SetPose(const FPondCameraPose& Value){Pose=Value;bActive=false;}
const TCHAR* FPondCameraMotion::Phase() const {return !bActive?TEXT("hold"):(Elapsed<ApproachSeconds?TEXT("approach"):TEXT("orbit"));}
