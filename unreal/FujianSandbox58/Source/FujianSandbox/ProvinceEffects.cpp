// 省待机保留轮廓流光；选城使用真实地形材质轻暖受光，沿用原三段镜头。
// HUD光效为透视投影表达，不声称物理体积光；始终保留原R15场景。-codex，2026-10-27
#include "ProvinceEffects.h"
#include "ProvinceFXCapture.h"
#include "XinluoSurveyControl.h"
#include "FujianMap.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace
{
float Ease(float X){X=FMath::Clamp(X,0.f,1.f);return X*X*X*(X*(X*6-15)+10);}
FPondCameraPose Blend(const FPondCameraPose& A,const FPondCameraPose& B,float T)
{
    T=Ease(T);return {FMath::Lerp(A.Focus,B.Focus,T),FMath::Lerp(A.Distance,B.Distance,T),A.Yaw+FMath::FindDeltaAngleDegrees(A.Yaw,B.Yaw)*T,FMath::Lerp(A.Pitch,B.Pitch,T)};
}
}
FProvinceEffects::FProvinceEffects(AFujianController* C):Owner(C){}
FProvinceEffects::~FProvinceEffects()=default;
bool FProvinceEffects::Initialize()
{
    auto* C=Owner.Get();if(!C||!C->Map)return false;Home=C->CurrentPose();
    FString Text;TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/terrain.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())return false;
    // 地形加载器已经验证同一元数据；此处再检查数组/坐标，不能把损坏轮廓连成跨屏线。-codex，2026-10-27
    auto Read=[this](const TArray<TSharedPtr<FJsonValue>>& Values,int32 City)
    {
        for(const auto& Value:Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* Points=nullptr;if(!Value->TryGetArray(Points)||Points->Num()<3)continue;
            // 市界与TerrainScene侧壁顶边使用同一完整序列和高度；省巡游保留旧采样。-codex，2026-11-02
            FRing Ring;Ring.City=City;const int32 Step=City==INDEX_NONE?FMath::Max(1,Points->Num()/650):1;
            for(int32 I=0;I<Points->Num();I+=Step)
            {
                const TArray<TSharedPtr<FJsonValue>>* P=nullptr;if(!(*Points)[I]->TryGetArray(P)||P->Num()!=3)continue;
                const FVector V((*P)[0]->AsNumber(),(*P)[1]->AsNumber(),(*P)[2]->AsNumber()+(City==INDEX_NONE?7:-5));
                if(!V.ContainsNaN())Ring.Points.Add(V);
            }
            if(Ring.Points.Num()>=3)
            {
                const FVector First=Ring.Points[0];Ring.Points.Add(First);Ring.Arc.Add(0);
                for(int32 I=1;I<Ring.Points.Num();I++){Ring.Length+=FVector::DistXY(Ring.Points[I-1],Ring.Points[I]);Ring.Arc.Add(Ring.Length);}
                if(Ring.Length>1)Rings.Add(MoveTemp(Ring));
            }
        }
    };
    const TArray<TSharedPtr<FJsonValue>>* Province=nullptr;const TArray<TSharedPtr<FJsonValue>>* Cities=nullptr;
    if(!Root->TryGetArrayField(TEXT("provinceRings"),Province)||!Root->TryGetArrayField(TEXT("cities"),Cities))return false;
    Read(*Province,INDEX_NONE);
    for(const auto& V:*Cities)
    {
        const auto O=V->AsObject();const TArray<TSharedPtr<FJsonValue>>* P=nullptr;if(!O.IsValid()||!O->TryGetArrayField(TEXT("rings"),P))return false;
        const int32 Code=O->GetIntegerField(TEXT("code"));const int32 Index=C->Map->Cities.IndexOfByPredicate([Code](const FDemoCity& D){return D.Code==Code;});
        if(Index==INDEX_NONE)return false;Read(*P,Index);
    }
    for(const auto& City:C->Map->Cities){MaxTerrainZ=FMath::Max(MaxTerrainZ,float(City.Bounds.Max.Z));ProvinceBounds+=City.Bounds;}
    BuildSurface();
    Capture=MakeUnique<FProvinceFXCapture>(C);
    return Capture->Initialize();
}
FPondCameraPose FProvinceEffects::CityPose(int32 Index)const
{
    auto* C=Owner.Get();const auto& City=C->Map->Cities[Index];FPondCameraPose P;P.Focus=City.Bounds.GetCenter();P.Yaw=180;P.Pitch=34;P.Distance=900;
    int32 W=1920,H=1080;C->GetViewportSize(W,H);const float TanH=FMath::Tan(FMath::DegreesToRadians(20.f)),TanV=TanH*FMath::Max(1,H)/FMath::Max(1,W);
    const FVector Out(-FMath::Cos(FMath::DegreesToRadians(P.Pitch)),0,FMath::Sin(FMath::DegreesToRadians(P.Pitch)));
    const FVector Forward=-Out,Right=FVector::CrossProduct(FVector::UpVector,Forward).GetSafeNormal(),Up=FVector::CrossProduct(Forward,Right);
    const auto* S=City.Terrain.IsValid()?City.Terrain->GetProcMeshSection(0):nullptr;
    if(S)for(const auto& Vertex:S->ProcVertexBuffer)
    {
        const FVector D=Vertex.Position-P.Focus;
        P.Distance=FMath::Max(P.Distance,float(FMath::Max(FMath::Abs(D|Right)/(TanH*.70f)-(D|Forward),FMath::Abs(D|Up)/(TanV*.70f)-(D|Forward))));
    }
    return P;
}
void FProvinceEffects::BeginFlight(const FPondCameraPose& To,bool Return)
{
    auto* C=Owner.Get();C->SurveyFlight.Cancel();C->SurveyFocusSubject.Empty();C->bSurveyFocusMotion=false;C->Motion.Cancel();Start=C->CurrentPose();Target=To;
    // 在起飞前确定最终安全姿态，末帧不再跳到未经约束的原Target。-codex，2026-11-03
    Target.Distance=FMath::Max(Target.Distance,(MaxTerrainZ+120.f-float(Target.Focus.Z))/FMath::Max(.1f,FMath::Sin(FMath::DegreesToRadians(Target.Pitch))));
    FlightTime=0;Flying=true;Returning=Return;CanResume=false;
    // 一次连续缓动到最终构图，取消靠近后的第二次转角。-codex，2026-11-03
}
void FProvinceEffects::Select(int32 City)
{
    auto* C=Owner.Get();if(!C||!C->Map||(City!=INDEX_NONE&&!C->Map->Cities.IsValidIndex(City)))return;
    // 同城再次点击保留选择并按下方条件续飞，不重复入城和B屏重切；亮度脉冲已由稳定视觉取代。
    // 暂保留旧计时字段赋值以减少状态链扰动，本次只更换绘制表达。-codex，2026-10-29
    if(City!=INDEX_NONE&&City==C->Map->SelectedCity)
    {
        // 输入按下先暂停镜头；轻点同城可续原进度，拖动/缩放改变姿态后不能复活旧轨迹。-codex，2026-10-27
        const FPondCameraPose Now=C->CurrentPose();
        if(CanResume&&Now.Focus.Equals(CancelledPose.Focus,.01)&&FMath::IsNearlyEqual(Now.Distance,CancelledPose.Distance,.01f)&&FMath::IsNearlyEqual(Now.Yaw,CancelledPose.Yaw,.01f)&&FMath::IsNearlyEqual(Now.Pitch,CancelledPose.Pitch,.01f))Flying=true;
        CanResume=false;RepeatTime=Time;return;
    }
    // 新入口只选择城市，不生成旧塘球，也不修改SurfaceGain/BoundaryGain。-codex，2026-10-27
    RelayCity=City!=INDEX_NONE&&City!=C->Map->SelectedCity?C->Map->SelectedCity:INDEX_NONE;
    RetiringCity=C->Map->SelectedCity;RetireTime=Time;RetireAge=Time-SelectionTime;
    HoverTarget=HoverCity=INDEX_NONE;HoverAlpha=0;
    if(City!=INDEX_NONE)ShellStart=-1;
    C->Map->SelectedCity=City;C->Map->SelectedSite=INDEX_NONE;C->FramedCity=City;C->FramedSite=INDEX_NONE;
    SelectionTime=Time;BeginFlight(City==INDEX_NONE?Home:CityPose(City),City==INDEX_NONE);
    UE_LOG(LogTemp,Display,TEXT("ProvinceFX select city=%d clock=%.3f target_distance=%.2f"),City,Time,Target.Distance);
}
// 同城返回市域只重取景，不重选、不重置视觉相位或发送B导航。-codex，2026-10-30
void FProvinceEffects::ReframeCity()
{
    auto* C=Owner.Get();if(!C||!C->Map||!C->Map->Cities.IsValidIndex(C->Map->SelectedCity))return;
    C->bSurveyFocusMotion=false;C->Motion.Cancel();
    BeginFlight(CityPose(C->Map->SelectedCity),false);
}
void FProvinceEffects::Cancel()
{
    CanResume=Flying&&!Returning;if(auto* C=Owner.Get())CancelledPose=C->CurrentPose();
    Flying=false;RelayCity=INDEX_NONE;HoverTarget=INDEX_NONE;
}
// 只读复用实际HUD命中框和地形拾取；预亮不选择、不飞行，也不刷新B。-codex，2026-10-27
void FProvinceEffects::PreviewPointer(FVector2D Position)
{
    auto* C=Owner.Get();HoverTarget=INDEX_NONE;if(!C||!C->Map||Flying||C->bSurveyFocusMotion||(C->DragDistance>=6&&(C->bPointerDown||C->bTouchDown)))return;
    int32 W=0,H=0;C->GetViewportSize(W,H);if(Position.X<0||Position.Y<0||Position.X>=W||Position.Y>=H)return;
    if(auto* HUD=Cast<AFujianHUD>(C->GetHUD()))
    {
        if(HUD->HomeButton.bIsValid&&HUD->HomeButton.IsInsideOrOn(Position))return;
        for(int32 I=0;I<HUD->CityButtons.Num();I++)if(HUD->CityButtons[I].bIsValid&&HUD->CityButtons[I].IsInsideOrOn(Position)){HoverTarget=I;HoverOrigin=C->Map->Cities[I].Center;break;}
    }
    if(HoverTarget==INDEX_NONE)
    {
        FHitResult Hit;if(C->GetHitResultAtScreenPosition(Position,ECC_Visibility,true,Hit))
            for(int32 I=0;I<C->Map->Cities.Num();I++)if(C->Map->Cities[I].Region.Get()==Hit.GetActor()){HoverTarget=I;HoverOrigin=Hit.Location;break;}
    }
    if(HoverTarget==C->Map->SelectedCity)HoverTarget=INDEX_NONE;
    if(HoverTarget!=INDEX_NONE)if(auto* HUD=Cast<AFujianHUD>(C->GetHUD()))
        HoverLabel=HUD->CityButtons.IsValidIndex(HoverTarget)?HUD->CityButtons[HoverTarget]:FBox2D(ForceInit);
}
void FProvinceEffects::Resize()
{
    auto* C=Owner.Get();if(!C)return;Cancel();C->Motion.Cancel();
    // 只让旧几何拟合求新的全省Home，不把其轨迹留在新飞行链上。-codex，2026-10-27
    const FPondCameraPose Current=C->CurrentPose();const int32 Selected=C->Map->SelectedCity;
    C->Map->SelectedCity=INDEX_NONE;C->FocusSelection(false,false);Home=C->Motion.Advance(10);C->Motion.Cancel();C->ApplyPose(Current);
    C->Map->SelectedCity=Selected;C->FramedCity=Selected;C->FramedSite=INDEX_NONE;
    BeginFlight(Selected==INDEX_NONE?Home:CityPose(Selected),Selected==INDEX_NONE);
}
void FProvinceEffects::Tick(float Delta)
{
    Time+=FMath::Max(0.f,Delta);auto* C=Owner.Get();if(!C)return;
    if(!Capture||!Capture->IsRecording())
    {
        float X=0,Y=0;
        if(C->bTouchDown)PreviewPointer(C->PreviousPosition);
        else if(C->GetMousePosition(X,Y))PreviewPointer(FVector2D(X,Y));else HoverTarget=INDEX_NONE;
    }
    if(Capture)Capture->Tick(Time);
    if(HoverTarget!=INDEX_NONE&&HoverTarget!=HoverCity){HoverCity=HoverTarget;HoverAlpha=0;HoverTime=Time;}
    HoverAlpha=FMath::FInterpConstantTo(HoverAlpha,HoverTarget!=INDEX_NONE?1.f:0.f,FMath::Max(0.f,Delta),HoverTarget!=INDEX_NONE?6.67f:4.f);
    if(HoverAlpha<=0)HoverCity=INDEX_NONE;
    // 切城时先指路，目标进入视口中部才展开短壳，避免高潮在屏外演完。-codex，2026-10-27
    if(C->Map->Cities.IsValidIndex(C->Map->SelectedCity)&&ShellStart<0&&Time-SelectionTime>=.12f)
    {
        int32 W=0,H=0;C->GetViewportSize(W,H);FVector2D Screen;
        if(C->ProjectWorldLocationToScreen(C->Map->Cities[C->Map->SelectedCity].Center,Screen)&&Screen.X>W*.15f&&Screen.X<W*.85f&&Screen.Y>H*.15f&&Screen.Y<H*.85f)ShellStart=Time;
    }
    // 业务导航只读；资料无坐标不当近景，总览保持原subject焦点语义。-codex，2026-10-30
    bool SubjectFocus=false;
    if(C->SurveyControl.IsValid())
    {
        const auto& Nav=C->SurveyControl->Navigation();const auto& Data=C->SurveyControl->Dataset();
        const int32 Subject=Data.Find(Nav.Subject);
        SubjectFocus=Data.Subjects.IsValidIndex(Subject)&&Data.Subjects[Subject].HasCoordinate&&
            (Nav.View==TEXT("subject")||(Nav.View==TEXT("overview")&&Nav.ReturnView==TEXT("subject")));
    }
    NearFocus=FMath::FInterpTo(NearFocus,SubjectFocus?1.f:0.f,FMath::Max(0.f,Delta),4.f);
    // 双层光区只调真实地形原纹理，不创建覆盖面；以实际视距缩尺并跟随近景焦点。
    // 相位复用本效果Clock，省态CitySelection=0保证原材质恒等。-codex，2026-10-30
    const FPondCameraPose Pose=C->CurrentPose();
    for(int32 I=0;I<C->Map->Cities.Num();I++)
    {
        const auto& City=C->Map->Cities[I];auto* Mesh=City.Terrain.Get();
        if(auto* Material=Mesh?Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)):nullptr)
        {
            const bool Selected=I==C->Map->SelectedCity;
            Material->SetScalarParameterValue(TEXT("CitySelection"),Selected?Ease((Time-SelectionTime)/.8f):0.f);
            if(Selected)
            {
                const FVector Size=City.Bounds.GetSize();
                const float CityExtent=FMath::Max(1.f,float((Size.X*.65+Size.Y*.76)*.5));
                const float Extent=FMath::Min(CityExtent,FMath::Max(120.f,Pose.Distance*.42f));
                const FVector Center=FMath::Lerp(City.Bounds.GetCenter(),Pose.Focus,NearFocus);
                Material->SetVectorParameterValue(TEXT("CityLightFrame"),FLinearColor(Center.X,Center.Y,Extent,0));
                Material->SetScalarParameterValue(TEXT("CityLightPhase"),Time/7.f);
            }
        }
    }
    if(!Flying)return;
    FlightTime+=FMath::Max(0.f,Delta);FPondCameraPose P;
    if(Returning)P=Blend(Start,Target,FMath::Max(0.f,FlightTime-.25f)/2.75f);
    else P=Blend(Start,Target,FlightTime/2.8f);
    // 安全约束通过视距满足，不在渲染末尾硬抬相机Z改变俯仰/焦点。-codex，2026-10-27
    P.Distance=FMath::Max(P.Distance,(MaxTerrainZ+120.f-float(P.Focus.Z))/FMath::Max(.1f,FMath::Sin(FMath::DegreesToRadians(P.Pitch))));
    C->ApplyPose(P);if(FlightTime>=(Returning?3.f:2.8f)){C->ApplyPose(Target);Flying=false;}
}
