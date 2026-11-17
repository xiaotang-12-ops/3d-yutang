// 复用现有地形、侧壁与边框的世界顶点取景，避免空包围盒角把镜头拉远；不改变塘级局部上下文。
// 先定位再有限环绕，重入与手动均从实际当前姿态继续。-codex，2026-10-21
#include "FujianMap.h"
#include "Camera/CameraActor.h"
#include "ProceduralMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "ProvinceEffects.h"
#include "XinluoSurveyControl.h"

namespace
{
struct FWaterSightSamples {FVector Points[9];};
FWaterSightSamples WaterSamples(const FVector& Anchor,const FVector2D& Half)
{
    // 中心、四角与四边中点共同验遮挡，防止只查对角线漏掉前沿山脊。-codex，2026-11-02
    const double X=Half.X*.94,Y=Half.Y*.94;
    return {{Anchor,Anchor+FVector(X,Y,0),Anchor-FVector(X,Y,0),Anchor+FVector(X,-Y,0),Anchor+FVector(-X,Y,0),
        Anchor+FVector(X,0,0),Anchor-FVector(X,0,0),Anchor+FVector(0,Y,0),Anchor-FVector(0,Y,0)}};
}
}

FPondCameraPose AFujianController::CurrentPose() const {return {Focus,Distance,Yaw,Pitch};}
void AFujianController::ApplyPose(const FPondCameraPose& P){Focus=WantedFocus=P.Focus;Distance=WantedDistance=P.Distance;Yaw=P.Yaw;Pitch=P.Pitch;}
void AFujianController::CancelCamera(const TCHAR* Reason)
{
    if(!bShowcase)return;
    bSurveyFocusMotion=false;
    SurveyFlight.Cancel();SurveyFocusSubject.Empty();
    if(ProvinceEffects.IsValid())ProvinceEffects->Cancel();
    if(Motion.IsActive())ApplyPose(Motion.Cancel());
    WantedFocus=Focus;WantedDistance=Distance;
    UE_LOG(LogTemp,Display,TEXT("PondCamera cancel=%s yaw=%.2f distance=%.2f"),Reason,Yaw,Distance);
}
// 调查主体只借用同一镜头轨迹；不把实际场址写进旧模拟塘列表。-codex，2026-10-29
void AFujianController::FocusSurveyPoint(const FString& SubjectId,const FVector& WorldPoint)
{
    if(!SurveyControl||!Map||SurveyControl->Navigation().City!=Xinluo::CityCode||SurveyControl->Navigation().Subject!=SubjectId||WorldPoint.ContainsNaN())return;
    const auto& Data=SurveyControl->Dataset();const int32 Index=Data.Find(SubjectId);
    if(!Data.Subjects.IsValidIndex(Index)||!Data.Subjects[Index].HasCoordinate)return;
    CancelCamera(TEXT("survey-target"));
    FString DisplayError;
    if(!Map->SetSurveyFarmSelection(SubjectId,DisplayError)){RejectSurveyFocus(SubjectId,*DisplayError);return;}
    FVector FocusPoint=WorldPoint,FarmCenter,Water;float FarmRadius=0;FVector2D HalfExtent;
    if(!Map->GetSurveyFarmFocus(SubjectId,FarmCenter,FarmRadius)||FarmRadius<=0||!Map->GetSurveyFarmWaterAnchor(SubjectId,Water,HalfExtent))
    {RejectSurveyFocus(SubjectId,TEXT("farm geometry unavailable"));return;}
    FocusPoint.Z=FarmCenter.Z+FMath::Min(FarmRadius*.03f,.08f);
    // 用户保留点选但要求主体更小：不低于1000的周边视野，避免水面/卫星贴图占满近景。-codex，2026-11-03
    const float ContextDistance=FMath::Max(1000.f,FarmRadius*40.f);
    FPondCameraPose Target=CurrentPose();Target.Focus=FocusPoint;Target.Pitch=54.f;
    float Highest=FocusPoint.Z;
    for(const auto& City:Map->Cities)if(City.Terrain.IsValid())Highest=FMath::Max(Highest,float(City.Terrain->Bounds.Origin.Z+City.Terrain->Bounds.BoxExtent.Z));
    auto ClearSegment=[&](const FVector& A,const FVector& B)
    {
        for(const auto& City:Map->Cities)if(City.Terrain.IsValid())
        {
            FHitResult Hit;
            if(City.Terrain->LineTraceComponent(Hit,A,B,FCollisionQueryParams(SCENE_QUERY_STAT(SurveyFlightPath),true))&&Hit.Distance<FVector::Distance(A,B)-.005)return false;
        }
        FHitResult LocalHit;
        if(Map->TraceSurveyFarmTerrain(LocalHit,A,B)&&LocalHit.Distance<FVector::Distance(A,B)-.005)return false;
        return true;
    };
    auto WaterClear=[&](const FVector& Eye)
    {for(const FVector& P:WaterSamples(Water,HalfExtent).Points)if(!ClearSegment(Eye,P))return false;return true;};
    auto AboveGround=[&](const FVector& Eye)
    {
        for(const auto& City:Map->Cities)if(City.Terrain.IsValid())
        {
            FHitResult Hit;
            if(City.Terrain->LineTraceComponent(Hit,FVector(Eye.X,Eye.Y,Highest+1000.f),FVector(Eye.X,Eye.Y,-100.f),FCollisionQueryParams(SCENE_QUERY_STAT(SurveyFocusGround),true))&&Eye.Z-Hit.ImpactPoint.Z<1.f)return false;
        }
        FHitResult LocalHit;
        if(Map->TraceSurveyFarmTerrain(LocalHit,FVector(Eye.X,Eye.Y,Highest+1000.f),FVector(Eye.X,Eye.Y,-100.f))&&Eye.Z-LocalHit.ImpactPoint.Z<1.f)return false;
        return true;
    };
    
    const float OriginalPitch=Target.Pitch;
    for(;;)
    {
        Target.Distance=FMath::Max(ContextDistance,(Highest+120.f-float(FocusPoint.Z))/FMath::Sin(FMath::DegreesToRadians(Target.Pitch)));
        const FVector FinalEye=FSurveyCameraFlight::Eye(Target);
        if(WaterClear(FinalEye)&&AboveGround(FinalEye))break;
        if(Target.Pitch>=80.f){RejectSurveyFocus(SubjectId,TEXT("no safe readable final pose"));return;}
        Target.Pitch=FMath::Min(80.f,Target.Pitch+5.f);
    }
    auto ClearArc=[&](const FVector& From,const FPondCameraPose& To)
    {
        FVector Previous=From;
        for(int32 I=1;I<=48;++I)
        {
            const FVector Next=FSurveyCameraFlight::Eye(FSurveyCameraFlight::Travel(From,Water,To,I/48.f));
            if(!ClearSegment(Previous,Next))return false;Previous=Next;
        }
        return true;
    };
    // 先建立有周边山地的远景，再亮灯；固定远近比例避免从全省镜头标定出覆盖整屏的巨环。-codex，2026-11-02
    const FVector StartEye=FSurveyCameraFlight::Eye(CurrentPose());
    FPondCameraPose Establish=Target;Establish.Focus=Water;Establish.Distance=FMath::Max(1550.f,Target.Distance*2.3f);
    FVector TurnEye=FSurveyCameraFlight::Eye(Establish);
    if(!WaterClear(TurnEye)||!ClearArc(TurnEye,Target))
    {
        TurnEye.Z=FMath::Max(double(Highest+120.f),StartEye.Z);
        for(int32 I=0;I<8&&(!WaterClear(TurnEye)||!ClearArc(TurnEye,Target));++I)TurnEye.Z+=FMath::Max(120.,FVector::DistXY(TurnEye,Water)*.25);
    }
    if(!ClearSegment(StartEye,TurnEye)||!ClearArc(TurnEye,Target))
    {RejectSurveyFocus(SubjectId,TEXT("no safe readable flight path"));return;}
    SurveyWaterAnchor=Water;SurveyWaterHalfExtent=HalfExtent;SurveyFocusSubject=SubjectId;
    // 只在点选时标定世界高度，后续镜头运动不再改尺寸；约400逻辑像素，匹配准稿远景可读性。-codex，2026-11-02
    const auto FarPose=FSurveyCameraFlight::Look(TurnEye,Water,Target.Yaw);
    const float A=FMath::DegreesToRadians(FarPose.Pitch),Fov=PlayerCameraManager?PlayerCameraManager->GetFOVAngle():40.f;
    const float Focal=960.f/FMath::Tan(FMath::DegreesToRadians(Fov*.5f));
    SurveyBeamHeight=FMath::Max(14.f,400.f*FarPose.Distance/FMath::Max(1.f,Focal*FMath::Cos(A)+400.f*FMath::Sin(A)));
    SurveyBeamHeight=FMath::Min(SurveyBeamHeight,Target.Distance*.52f);
    SurveyBeamRadius=FMath::Clamp(175.f*FarPose.Distance/Focal,FarmRadius*1.8f,Target.Distance*.14f);SurveyBeamYaw=FarPose.Yaw;
    SurveyFlight.Start(CurrentPose(),Target,Water,TurnEye);bSurveyFocusMotion=true;LastActivity=GetWorld()->GetTimeSeconds();
    UE_LOG(LogTemp,Display,TEXT("SurveyFocus id=%s world=%s distance=%.2f radius=%.3f beacon_distance=%.2f original_pitch=%.2f final_pitch=%.2f"),*SubjectId,*FocusPoint.ToString(),Target.Distance,FarmRadius,float(FVector::Distance(TurnEye,Water)),OriginalPitch,Target.Pitch);
}
void AFujianController::RejectSurveyFocus(const FString& SubjectId,const TCHAR* Reason)
{
    // 拒绝和运行时超时共用Failed显示状态，仍保留当前眼位；改选/手动/返回统一清除。-codex，2026-11-02
    SurveyFlight.Reject(CurrentPose());SurveyFocusSubject=SubjectId;bSurveyFocusMotion=false;
    UE_LOG(LogTemp,Warning,TEXT("SurveyFocus rejected id=%s reason=%s; retaining current camera"),*SubjectId,Reason);
}
bool AFujianController::IsSurveyWaterVisible(bool RequireReadable)
{
    auto Reject=[&](const TCHAR* Reason){SurveyVisibilityFailure=Reason;return false;};
    if(!Map||!SurveyControl||SurveyControl->Navigation().View!=TEXT("subject")||SurveyControl->Navigation().Subject!=SurveyFocusSubject)return Reject(TEXT("navigation changed"));
    int32 W=0,H=0;GetViewportSize(W,H);if(W<=0||H<=0)return Reject(TEXT("viewport unavailable"));
    FVector Eye;FRotator Rotation;GetPlayerViewPoint(Eye,Rotation);
    // 投影与截图使用同一已提交相机。等待状态保持眼位，拒绝点击当帧的旧相机缓存。-codex，2026-11-02
    if(!Eye.Equals(FSurveyCameraFlight::Eye(CurrentPose()),.01))return Reject(TEXT("camera not submitted"));
    const auto* HUD=Cast<AFujianHUD>(GetHUD());if(!HUD)return Reject(TEXT("HUD unavailable"));
    const float Scale=FMath::Min(W/1920.f,H/1080.f);const FVector2D Offset((W-1920*Scale)*.5f,(H-1080*Scale)*.5f);
    TArray<FVector2D> ProjectedWater;
    for(const FVector& P:WaterSamples(SurveyWaterAnchor,SurveyWaterHalfExtent).Points)
    {
        FVector2D Screen;if(!ProjectWorldLocationToScreen(P,Screen))return Reject(TEXT("projection behind camera"));
        const FVector2D Q=(Screen-Offset)/Scale;ProjectedWater.Add(Q);
        if(Q.X<40||Q.X>1880||Q.Y<120||Q.Y>995)return Reject(TEXT("outside usable viewport"));
        for(const FBox2D* B:{&HUD->SurveySelector,&HUD->SurveyToggle,&HUD->SurveyPanel})if(B->bIsValid&&B->ExpandBy(8*Scale).IsInsideOrOn(Screen))return Reject(TEXT("covered by controls"));
        for(const auto& City:Map->Cities)if(City.Terrain.IsValid())
        {
            FHitResult Hit;
            if(City.Terrain->LineTraceComponent(Hit,Eye,P,FCollisionQueryParams(SCENE_QUERY_STAT(SurveyVisibleWater),true))&&Hit.Distance<FVector::Distance(Eye,P)-.005)return Reject(TEXT("water occluded by terrain"));
        }
        FHitResult LocalHit;
        if(Map->TraceSurveyFarmTerrain(LocalHit,Eye,P)&&LocalHit.Distance<FVector::Distance(Eye,P)-.005)return Reject(TEXT("water occluded by local farm terrain"));
    }
    SurveyWaterSpan=ProjectedWater.Num()>=3?float(FVector2D::Distance(ProjectedWater[1],ProjectedWater[2])):0.f;
    if(RequireReadable&&SurveyWaterSpan<6.f)return Reject(TEXT("water too small to identify"));
    SurveyVisibilityFailure.Empty();return true;
}
void AFujianController::Back()
{
    if(SurveyControl){if(bSurveyFocusMotion)CancelCamera(TEXT("survey-back"));SurveyControl->Back();return;}
    if(!Map)return;
    if(IsProvinceFX()){Home();return;}
    CancelCamera(TEXT("back"));
    if(Map->Cities.IsValidIndex(Map->SelectedCity))
    {
        if(Map->SelectedSite!=INDEX_NONE)Map->ShowCity(Map->SelectedCity);else Map->ShowProvince();
    }
    FocusSelection(false,false);
}
// 同城返回只恢复既有市域构图，不再选城或改动B导航。-codex，2026-10-30
void AFujianController::ReframeSurveyCity()
{
    CancelCamera(TEXT("survey-city"));
    if(Map){FString DisplayError;if(!Map->SetSurveyFarmSelection(TEXT(""),DisplayError))UE_LOG(LogTemp,Warning,TEXT("Survey display restore failed: %s"),*DisplayError);}
    if(ProvinceEffects.IsValid())ProvinceEffects->ReframeCity();
}
void AFujianController::FocusSelection(bool bTop,bool bOrbit,bool bPreservePitch)
{
    if(!Map||Map->Cities.IsEmpty())return;
    int32 W=0,H=0;GetViewportSize(W,H);if(W<=0||H<=0)return;
    const double FitStarted=FPlatformTime::Seconds();
    CancelCamera(TEXT("new-target"));FittedViewport=FIntPoint(W,H);
    FramedCity=Map->SelectedCity;FramedSite=Map->SelectedSite;
    FBox Box=Map->VisibleBounds();FVector Center=Box.GetCenter();
    const bool Site=Map->Cities.IsValidIndex(FramedCity)&&Map->Cities[FramedCity].Sites.IsValidIndex(FramedSite);
    if(Site)
    {
        Center=Map->Cities[FramedCity].Sites[FramedSite].Position;
        // 以实际塘中心作焦点，同时留出一片市域和相邻点，不捏造独立塘三维。-codex，2026-10-21
        const float Radius=FMath::Max(400.f,static_cast<float>(Box.GetSize().GetMax())*.22f);
        Box=FBox(Center-FVector(Radius,Radius,150),Center+FVector(Radius,Radius,200));
    }
    TArray<FVector> FitPoints;
    bool bCompleteGeometry=!Site;
    auto AppendMesh=[&FitPoints](UProceduralMeshComponent* Mesh)
    {
        const FProcMeshSection* Section=Mesh?Mesh->GetProcMeshSection(0):nullptr;
        if(!Section||Section->ProcVertexBuffer.IsEmpty())return false;
        const FTransform Transform=Mesh->GetComponentTransform();
        for(const FProcMeshVertex& Vertex:Section->ProcVertexBuffer)
        {
            const FVector World=Transform.TransformPosition(Vertex.Position);
            if(World.ContainsNaN())return false;
            FitPoints.Add(World);
        }
        return true;
    };
    // 顶点仅在选择/重拟合时收集一次，九段投影复用；漏任一组件时整层退回旧盒法，不能以残缺轮廓靠近。-codex，2026-10-21
    if(!Site)
    {
        if(Map->Cities.IsValidIndex(FramedCity))
        {
            const FDemoCity& City=Map->Cities[FramedCity];
            bCompleteGeometry=AppendMesh(City.Terrain.Get())&&AppendMesh(City.Skirt.Get());
        }
        else
        {
            for(const FDemoCity& City:Map->Cities)
                if(!AppendMesh(City.Terrain.Get())){bCompleteGeometry=false;break;}
            TArray<UProceduralMeshComponent*> DecorMeshes;
            if(Map->ProvinceDecor.IsValid())Map->ProvinceDecor->GetComponents(DecorMeshes);
            // TerrainScene 的省装饰固定包含墙和描边两组件，独立 Ground 舞台不参与地图取景。-codex，2026-10-21
            if(DecorMeshes.Num()!=2)bCompleteGeometry=false;
            if(bCompleteGeometry)for(UProceduralMeshComponent* Mesh:DecorMeshes)
                if(!AppendMesh(Mesh)){bCompleteGeometry=false;break;}
        }
        if(bCompleteGeometry)
        {
            // 标签仍由 HUD 屏幕空间排布；这里只覆盖其真实锚点并留少量世界空间余量，不承诺消除文字遮挡。-codex，2026-10-21
            auto AppendAnchor=[&FitPoints](const FVector& Anchor)
            {
                for(int32 I=0;I<8;I++)FitPoints.Add(Anchor+FVector(I&1?40:-40,I&2?40:-40,I&4?40:-40));
            };
            if(Map->Cities.IsValidIndex(FramedCity))
                for(const FDemoSite& Item:Map->Cities[FramedCity].Sites)AppendAnchor(Item.Position+FVector(0,0,40));
            else
                for(const FDemoCity& City:Map->Cities)AppendAnchor(City.Center+FVector(0,0,70));
        }
    }
    if(!bCompleteGeometry)
    {
        FitPoints.Reset();
        for(int32 I=0;I<8;I++)FitPoints.Add(FVector(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,I&4?Box.Max.Z:Box.Min.Z));
    }
    FPondCameraPose Target=CurrentPose();Target.Focus=Center;
    // 复位/返回使用固定初始朝向；自动选塘仍沿当前朝向衔接，窗口重拟合保留手动方向。-codex，2026-10-22
    if(!bPreservePitch&&!bOrbit)Target.Yaw=IsProvinceOnly()?180.f:135.f;
    // 窗口尺寸变化只重拟合距离，不把用户手动视角或俯视强制拉回默认角度。-codex，2026-10-21
    Target.Pitch=bPreservePitch?Pitch:(bTop?80.f:(IsProvinceOnly()?55.f:(Site?46.f:40.f)));
    const float Arc=bOrbit&&!bTop&&!IsProvinceOnly()?24.f:0.f;
    const float TanH=FMath::Tan(FMath::DegreesToRadians(20.f));const float TanV=TanH/(static_cast<float>(W)/H);
    Target.Distance=600;
    // 拟合整段环绕角，而非只拟合终点，防止长条市域转到中途被裁切。-codex，2026-10-21
    for(int32 A=0;!IsProvinceOnly()&&A<=8;A++)
    {
        const float Angle=Target.Yaw+Arc*A/8.f;
        const float R=FMath::DegreesToRadians(Angle),P=FMath::DegreesToRadians(Target.Pitch);
        const FVector Offset(FMath::Cos(P)*FMath::Cos(R),FMath::Cos(P)*FMath::Sin(R),FMath::Sin(P));
        const FVector Forward=-Offset,Right=FVector::CrossProduct(FVector::UpVector,Forward).GetSafeNormal(),Up=FVector::CrossProduct(Forward,Right);
        for(const FVector& Point:FitPoints)
        {
            const FVector D=Point-Center;
            const float UsableHeight=.72f;
            Target.Distance=FMath::Max(Target.Distance,static_cast<float>(FMath::Max(FMath::Abs(D|Right)/(TanH*.87f)-(D|Forward),FMath::Abs(D|Up)/(TanV*UsableHeight)-(D|Forward))));
        }
    }
    if(IsProvinceOnly())
    {
        // 透视图的地理包围盒中心不等于画面中心。联合求解上下余量和视距，
        // 让整省占高约84%、上下均衡，不靠放大后裁掉南端。仅省览使用。-codex，2026-10-27
        const float R=FMath::DegreesToRadians(Target.Yaw),P=FMath::DegreesToRadians(Target.Pitch);
        const FVector Forward=-FVector(FMath::Cos(P)*FMath::Cos(R),FMath::Cos(P)*FMath::Sin(R),FMath::Sin(P));
        const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward).GetSafeNormal(),Up=FVector::CrossProduct(Forward,Right);
        const float VerticalLimit=TanV*.86f;
        float Upper=-TNumericLimits<float>::Max(),Lower=TNumericLimits<float>::Max();
        Target.Distance=600.f;
        for(const FVector& Point:FitPoints)
        {
            const FVector D=Point-Center;
            const float Depth=D|Forward,Height=D|Up;
            Upper=FMath::Max(Upper,Height-VerticalLimit*Depth);
            Lower=FMath::Min(Lower,Height+VerticalLimit*Depth);
            Target.Distance=FMath::Max(Target.Distance,static_cast<float>(FMath::Abs(D|Right)/(TanH*.87f)-Depth));
        }
        Target.Distance=FMath::Max(Target.Distance,(Upper-Lower)/(2.f*VerticalLimit));
        Target.Focus=Center+Up*((Upper+Lower)*.5f);
    }
    Target.Distance*=IsProvinceOnly()?1.02f:1.05f;Motion.Start(CurrentPose(),Target,Arc);
    UE_LOG(LogTemp,Display,TEXT("PondCamera fit source=%s points=%d viewport=%dx%d yaw=%.2f pitch=%.2f distance=%.2f ms=%.3f"),Site?TEXT("site-box"):(bCompleteGeometry?TEXT("geometry"):TEXT("bounds-fallback")),FitPoints.Num(),W,H,Target.Yaw,Target.Pitch,Target.Distance,(FPlatformTime::Seconds()-FitStarted)*1000.0);
    UE_LOG(LogTemp,Display,TEXT("PondCamera begin city=%d site=%d target=%s distance=%.2f arc=%.1f"),FramedCity,FramedSite,*Target.Focus.ToString(),Target.Distance,Arc);
}
