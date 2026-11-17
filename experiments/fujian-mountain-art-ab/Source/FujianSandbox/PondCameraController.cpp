// 复用现有地形、侧壁与边框的世界顶点取景，避免空包围盒角把镜头拉远；不改变塘级局部上下文。
// 先定位再有限环绕，重入与手动均从实际当前姿态继续。-codex，2026-10-21
#include "FujianMap.h"
#include "Camera/CameraActor.h"
#include "ProceduralMeshComponent.h"
#include "HAL/PlatformTime.h"

FPondCameraPose AFujianController::CurrentPose() const {return {Focus,Distance,Yaw,Pitch};}
void AFujianController::ApplyPose(const FPondCameraPose& P){Focus=WantedFocus=P.Focus;Distance=WantedDistance=P.Distance;Yaw=P.Yaw;Pitch=P.Pitch;}
void AFujianController::CancelCamera(const TCHAR* Reason)
{
    if(!bShowcase)return;
    if(Motion.IsActive())ApplyPose(Motion.Cancel());
    WantedFocus=Focus;WantedDistance=Distance;
    UE_LOG(LogTemp,Display,TEXT("PondCamera cancel=%s yaw=%.2f distance=%.2f"),Reason,Yaw,Distance);
}
void AFujianController::Back()
{
    if(!Map)return;
    CancelCamera(TEXT("back"));
    if(Map->Cities.IsValidIndex(Map->SelectedCity))
    {
        if(Map->SelectedSite!=INDEX_NONE)Map->ShowCity(Map->SelectedCity);else Map->ShowProvince();
    }
    FocusSelection(false,false);
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
    if(!bPreservePitch&&!bOrbit)Target.Yaw=135.f;
    // 窗口尺寸变化只重拟合距离，不把用户手动视角或俯视强制拉回默认角度。-codex，2026-10-21
    Target.Pitch=bPreservePitch?Pitch:(bTop?80.f:(Site?46.f:40.f));
    const float Arc=bOrbit&&!bTop?24.f:0.f;
    const float TanH=FMath::Tan(FMath::DegreesToRadians(20.f));const float TanV=TanH/(static_cast<float>(W)/H);
    Target.Distance=600;
    // 拟合整段环绕角，而非只拟合终点，防止长条市域转到中途被裁切。-codex，2026-10-21
    for(int32 A=0;A<=8;A++)
    {
        const float Angle=Target.Yaw+Arc*A/8.f;
        const float R=FMath::DegreesToRadians(Angle),P=FMath::DegreesToRadians(Target.Pitch);
        const FVector Offset(FMath::Cos(P)*FMath::Cos(R),FMath::Cos(P)*FMath::Sin(R),FMath::Sin(P));
        const FVector Forward=-Offset,Right=FVector::CrossProduct(FVector::UpVector,Forward).GetSafeNormal(),Up=FVector::CrossProduct(Forward,Right);
        for(const FVector& Point:FitPoints)
        {
            const FVector D=Point-Center;
            Target.Distance=FMath::Max(Target.Distance,static_cast<float>(FMath::Max(FMath::Abs(D|Right)/(TanH*.87f)-(D|Forward),FMath::Abs(D|Up)/(TanV*.72f)-(D|Forward))));
        }
    }
    Target.Distance*=1.05f;Motion.Start(CurrentPose(),Target,Arc);
    UE_LOG(LogTemp,Display,TEXT("PondCamera fit source=%s points=%d viewport=%dx%d yaw=%.2f pitch=%.2f distance=%.2f ms=%.3f"),Site?TEXT("site-box"):(bCompleteGeometry?TEXT("geometry"):TEXT("bounds-fallback")),FitPoints.Num(),W,H,Target.Yaw,Target.Pitch,Target.Distance,(FPlatformTime::Seconds()-FitStarted)*1000.0);
    UE_LOG(LogTemp,Display,TEXT("PondCamera begin city=%d site=%d target=%s distance=%.2f arc=%.1f"),FramedCity,FramedSite,*Center.ToString(),Target.Distance,Arc);
}
