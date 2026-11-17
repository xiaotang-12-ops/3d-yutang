// 使用有终点的平滑曲线，避免追近插值无限趋近和松手后旧轨迹重新接管。-codex，2026-10-21
#include "PondCameraMotion.h"

namespace { float Smooth(float T) { T=FMath::Clamp(T,0.f,1.f); return T*T*(3.f-2.f*T); } }

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
    Pose.Yaw=FMath::Lerp(StartPose.Yaw,Target.Yaw,T)+Orbit*Smooth((Elapsed-ApproachSeconds)/OrbitSeconds);
    const float Duration=ApproachSeconds+(FMath::IsNearlyZero(Orbit)?0.f:OrbitSeconds);
    if(Elapsed>=Duration){Pose=Target;Pose.Yaw+=Orbit;bActive=false;}
    return Pose;
}
FPondCameraPose FPondCameraMotion::Cancel(){bActive=false;return Pose;}
void FPondCameraMotion::SetPose(const FPondCameraPose& Value){Pose=Value;bActive=false;}
const TCHAR* FPondCameraMotion::Phase() const {return !bActive?TEXT("hold"):(Elapsed<ApproachSeconds?TEXT("approach"):TEXT("orbit"));}
