// 远景入画与终态塘面可辨分别验证；完整展示后飞近，停稳才淡出。-codex，2026-11-02
#include "SurveyCameraFlight.h"
namespace
{
float FlightEase(float T){T=FMath::Clamp(T,0.f,1.f);return T*T*T*(T*(T*6-15)+10);}
}
FVector FSurveyCameraFlight::Eye(const FPondCameraPose& P)
{
    const float R=FMath::DegreesToRadians(P.Yaw),A=FMath::DegreesToRadians(P.Pitch);
    return P.Focus+P.Distance*FVector(FMath::Cos(A)*FMath::Cos(R),FMath::Cos(A)*FMath::Sin(R),FMath::Sin(A));
}
FPondCameraPose FSurveyCameraFlight::Look(const FVector& Eye,const FVector& Focus,float FallbackYaw)
{
    const FVector V=Eye-Focus;const float Range=FMath::Max(float(V.Size()),.001f);
    return {Focus,Range,V.SizeSquared2D()>1.e-10?float(FMath::RadiansToDegrees(FMath::Atan2(V.Y,V.X))):FallbackYaw,float(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(V.Z/Range,-1.,1.))))};
}
FPondCameraPose FSurveyCameraFlight::Travel(const FVector& FromEye,const FVector& FromFocus,const FPondCameraPose& Destination,float T)
{
    // 半径/最短方位/高度连续，避免眼位直线穿过目标竖轴；控制器用同一函数预检真实路径。-codex，2026-11-02
    const auto From=Look(FromEye,FromFocus,Destination.Yaw);const FVector EndEye=Eye(Destination);
    const FVector Focus=FMath::Lerp(FromFocus,Destination.Focus,T);
    const float Yaw=From.Yaw+FMath::FindDeltaAngleDegrees(From.Yaw,Destination.Yaw)*T,R=FMath::DegreesToRadians(Yaw);
    const double Radius=FMath::Lerp(FVector::DistXY(FromEye,FromFocus),FVector::DistXY(EndEye,Destination.Focus),double(T));
    return Look(FVector(Focus.X+FMath::Cos(R)*Radius,Focus.Y+FMath::Sin(R)*Radius,FMath::Lerp(FromEye.Z,EndEye.Z,double(T))),Focus,Yaw);
}
void FSurveyCameraFlight::Start(const FPondCameraPose& Current,const FPondCameraPose& Destination,const FVector& Water,const FVector& TurnEye)
{
    StartPose=Pose=Current;Target=Destination;WaterPoint=Water;StartEye=Eye(Current);Turn=TurnEye;
    StartRotation=(Current.Focus-StartEye).Rotation().Quaternion();TurnRotation=(Water-Turn).Rotation().Quaternion();
    Elapsed=0;LightAge=SettledAge=-1;Stage=EStage::Orient;
}
void FSurveyCameraFlight::Cancel(){Stage=EStage::Idle;Elapsed=0;LightAge=SettledAge=-1;}
void FSurveyCameraFlight::Reject(const FPondCameraPose& Current){Pose=Current;Stage=EStage::Failed;Elapsed=0;LightAge=SettledAge=-1;}
FPondCameraPose FSurveyCameraFlight::Advance(float Delta,bool Visible)
{
    if(!FMath::IsFinite(Delta)||Delta<=0)return Pose;
    if(LightAge>=0)LightAge+=Delta;
    if(SettledAge>=0)SettledAge+=Delta;
    Elapsed+=Delta;
    if(Stage==EStage::Orient)
    {
        const float T=FlightEase(Elapsed/OrientDuration);const FVector Camera=FMath::Lerp(StartEye,Turn,T);
        const FVector Forward=FQuat::Slerp(StartRotation,TurnRotation,T).GetForwardVector();
        const float Range=FMath::Lerp(StartPose.Distance,float(FVector::Distance(Turn,WaterPoint)),T);
        Pose=Look(Camera,Camera+Forward*Range,StartPose.Yaw);
        if(Elapsed>=OrientDuration){Pose=Look(Turn,WaterPoint,StartPose.Yaw);Stage=EStage::AwaitVisible;Elapsed=0;}
    }
    else if(Stage==EStage::AwaitVisible)
    {
        // 此时只要求远方目标落在画面且无遮挡；微小塘不能因为像素不足先飞近才亮灯。-codex，2026-11-02
        if(Visible&&Elapsed>=.05f){Stage=EStage::Beacon;Elapsed=0;LightAge=0;}
        else if(Elapsed>=2.f){Stage=EStage::Failed;LightAge=-1;}
    }
    else if(Stage==EStage::Beacon)
    {
        if(Elapsed>=BeaconDuration){Stage=EStage::Approach;Elapsed=0;}
    }
    else if(Stage==EStage::Approach)
    {
        // 同一安全空间路径按对数距离分配时间，避免远近跨度大时最后几帧突然放大。-codex，2026-11-02
        const float Ease=FlightEase(Elapsed/ApproachDuration);const double From=FVector::Distance(Turn,WaterPoint),To=Target.Distance;
        const float PathT=From>.001&&To>.001&&FMath::Abs(From-To)>.001?float((From-FMath::Exp(FMath::Lerp(FMath::Loge(From),FMath::Loge(To),double(Ease))))/(From-To)):Ease;
        Pose=Travel(Turn,WaterPoint,Target,FMath::Clamp(PathT,0.f,1.f));
        if(Elapsed>=ApproachDuration){Pose=Target;Stage=EStage::Complete;SettledAge=0;}
    }
    return Pose;
}
const TCHAR* FSurveyCameraFlight::Phase() const
{
    switch(Stage)
    {
        case EStage::Orient:return TEXT("survey-orient");case EStage::AwaitVisible:return TEXT("survey-visible");
        case EStage::Beacon:return TEXT("survey-beacon");case EStage::Approach:return TEXT("survey-approach");
        case EStage::Complete:return TEXT("survey-hold");case EStage::Failed:return TEXT("survey-failed");default:return TEXT("idle");
    }
}
