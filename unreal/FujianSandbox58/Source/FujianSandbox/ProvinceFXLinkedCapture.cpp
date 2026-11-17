// 显式Linked离屏取证：复用真实HUD/Survey状态和原生A截图，不创建旧B或模拟场址。
// 固定步长只验证轨迹与输入路由；独立同步目录隔离现场，不代表实时FPS、物理触屏或B画面。-codex，2026-10-30
#include "ProvinceFXCapture.h"
#include "FujianMap.h"
#include "ProvinceEffects.h"
#include "XinluoSurveyControl.h"
#include "ProceduralMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "InputKeyEventArgs.h"

namespace
{
bool SamePose(const FPondCameraPose& A,const FPondCameraPose& B)
{
    return A.Focus.Equals(B.Focus,.05)&&FMath::IsNearlyEqual(A.Distance,B.Distance,.05f)&&FMath::IsNearlyEqual(A.Pitch,B.Pitch,.05f)&&FMath::IsNearlyEqual(A.Yaw,B.Yaw,.05f);
}
}

bool FProvinceFXCapture::InitializeLinked()
{
    auto* C=Owner.Get();
    if(!C||!C->IsSurveyLinked()||Atlas||Reel||!FSlateApplication::Get().IsRenderingOffScreen())
    {UE_LOG(LogTemp,Error,TEXT("Linked capture requires XinluoLinked + RenderOffscreen and excludes Atlas/Reel"));return false;}
    // Capture初始化早于SurveyControl。先拒绝现场路径，Survey稍后才获准在全新隔离目录拿锁。-codex，2026-10-30
    if(!FParse::Value(FCommandLine::Get(),TEXT("PondSyncDir="),LinkedSyncDirectory))return false;
    LinkedSyncDirectory=FPaths::ConvertRelativePathToFull(LinkedSyncDirectory);
    FPaths::NormalizeDirectoryName(LinkedSyncDirectory);FPaths::CollapseRelativeDirectories(LinkedSyncDirectory);
    FString Expected=Directory/TEXT("linked-sync");FPaths::NormalizeDirectoryName(Expected);FPaths::CollapseRelativeDirectories(Expected);
    if(!FPaths::IsSamePath(LinkedSyncDirectory,Expected)||IFileManager::Get().DirectoryExists(*LinkedSyncDirectory))
    {UE_LOG(LogTemp,Error,TEXT("Linked capture requires a NEW PondSyncDir exactly <capture-dir>/linked-sync"));return false;}
    LinkedOverviewOnly=FParse::Param(FCommandLine::Get(),TEXT("SurveyCaptureOverview"));
    LinkedAll=FParse::Param(FCommandLine::Get(),TEXT("SurveyCaptureAll"));
    LinkedMovie=FParse::Param(FCommandLine::Get(),TEXT("SurveyCaptureMovie"));
    LinkedEdges=FParse::Param(FCommandLine::Get(),TEXT("SurveyCaptureEdges"));
    FParse::Value(FCommandLine::Get(),TEXT("SurveyCaptureRestore="),LinkedRestoreId);
    if(!LinkedRestoreId.IsEmpty())
    {
        // 仅在全新测试目录预置合法历史导航；数据仍来自用户原JSON，正常初始化负责读取并恢复。-codex，2026-10-30
        Xinluo::FDataset Data;if(!Data.Load(Xinluo::DataPath())||Data.Find(LinkedRestoreId)<0)return false;
        Xinluo::FState Seed;Seed.Dataset=Data.Version;Seed.Session=FGuid::NewGuid().ToString();Seed.Revision=1;Seed.Heartbeat=1;
        Seed.Nav.SelectCity(Xinluo::CityCode);Seed.Nav.SelectSubject(Data,LinkedRestoreId);
        if(FParse::Param(FCommandLine::Get(),TEXT("SurveyCaptureRestoreOverview")))Seed.Nav.Overview();
        if(!IFileManager::Get().MakeDirectory(*LinkedSyncDirectory,true)||!FFileHelper::SaveStringToFile(Seed.Serialize(),*(LinkedSyncDirectory/TEXT("state.json"))))return false;
    }
    CSV=TEXT("frame,simulation_time,step,id,view,revision,focus_x,focus_y,focus_z,camera_x,camera_y,camera_z,distance,pitch,yaw,motion,ground_found,ground_clearance,los_blocked,screen_x,screen_y,inside,wall_seconds,phase,beacon_age,water_x,water_y,water_z,water_span_px,arrival_age,beam_height,beam_radius\n");
    EndFrameHandle=UGameViewportClient::OnScreenshotCaptured().AddRaw(this,&FProvinceFXCapture::CaptureFrame);
    return true;
}

void FProvinceFXCapture::TickLinked()
{
    auto* C=Owner.Get();auto* H=C?Cast<AFujianHUD>(C->GetHUD()):nullptr;
    if(Clock>480.f){Fail(TEXT("Linked capture timeout"));return;}
    if(Clock<6.f||!C||!H||!C->SurveyControl||C->SurveyControl->Dataset().Subjects.IsEmpty())return;
    if(!LinkedStepsReady)
    {
        LinkedStepsReady=true;LinkedStart=Clock;
        auto Add=[this](ELinkedAction Action,const FString& Id,const TCHAR* Label,float Duration){LinkedSteps.Add({Action,Id,Label,Duration});};
        if(LinkedOverviewOnly)
        {
            // 总览验收走真实城市和HUD入口；旧subject/overview存档由正常初始化归一。-codex，2026-11-03
            if(!LinkedRestoreId.IsEmpty())Add(ELinkedAction::CityHold,TEXT(""),TEXT("restored-city"),1.f);
            Add(ELinkedAction::Home,TEXT(""),TEXT("overview-home"),3.5f);
            const int32 CityIndex=C->Map->Cities.IndexOfByPredicate([](const FDemoCity& City){return City.Code==Xinluo::CityCode;});
            for(int32 Cycle=0;Cycle<3;++Cycle)
            {
                Add(ELinkedAction::Hover,FString::FromInt(CityIndex),TEXT("overview-hover"),1.f);
                Add(ELinkedAction::City,TEXT(""),TEXT("city-entry"),3.5f);
                Add(ELinkedAction::CityHold,TEXT(""),TEXT("overview-stable"),2.f);
                Add(ELinkedAction::ReadOnlyOverview,TEXT(""),TEXT("overview-readonly"),1.2f);
                Add(ELinkedAction::RepeatCity,TEXT(""),TEXT("overview-repeat"),1.2f);
                Add(ELinkedAction::Toggle,TEXT(""),TEXT("overview-collapse"),.2f);
                Add(ELinkedAction::Toggle,TEXT(""),TEXT("overview-expand"),.2f);
                Add(ELinkedAction::ScrollDown,TEXT(""),TEXT("overview-scroll-down"),.2f);
                Add(ELinkedAction::ScrollUp,TEXT(""),TEXT("overview-scroll-up"),.2f);
                Add(ELinkedAction::Home,TEXT(""),TEXT("overview-return"),3.5f);
            }
        }
        else
        {
        if(!LinkedRestoreId.IsEmpty())Add(ELinkedAction::Hold,LinkedRestoreId,TEXT("restored-subject"),2.f);
        else
        {
            if(LinkedAll)for(int32 I=0;I<C->Map->Cities.Num();++I)Add(ELinkedAction::Hover,FString::FromInt(I),TEXT("city-hover"),.8f);
            Add(ELinkedAction::City,TEXT(""),TEXT("city-entry"),4.5f);
            if(LinkedMovie)Add(ELinkedAction::CityHold,TEXT(""),TEXT("city-fixed"),16.f);
            // 远景展示0.7秒、飞近2.8秒，停稳后0.85秒淡出；显式诊断保留完整退场过程。-codex，2026-11-02
            Add(ELinkedAction::Subject,TEXT("xinluo-001"),TEXT("first-closeup"),6.8f);
            if(LinkedMovie)Add(ELinkedAction::Hold,TEXT("xinluo-001"),TEXT("closeup-fixed"),16.f);
        }
        if(LinkedAll)
        {
            Add(ELinkedAction::Back,TEXT(""),TEXT("back-city"),4.5f);
            Add(ELinkedAction::Subject,TEXT("xinluo-001"),TEXT("list-start"),.2f);
            Add(ELinkedAction::Toggle,TEXT(""),TEXT("list-collapse"),.2f);
            Add(ELinkedAction::Toggle,TEXT(""),TEXT("list-expand"),.2f);
            Add(ELinkedAction::ScrollDown,TEXT(""),TEXT("list-scroll-down"),.2f);
            Add(ELinkedAction::ScrollUp,TEXT(""),TEXT("list-scroll-up"),.2f);
            Add(ELinkedAction::Hold,TEXT("xinluo-001"),TEXT("list-closeup-settled"),6.8f);
            Add(ELinkedAction::Subject,TEXT("xinluo-002"),TEXT("rapid-two"),.1f);
            Add(ELinkedAction::Subject,TEXT("xinluo-003"),TEXT("rapid-three"),.1f);
            Add(ELinkedAction::Subject,TEXT("xinluo-004"),TEXT("rapid-final"),6.8f);
            Add(ELinkedAction::Subject,TEXT("xinluo-001"),TEXT("before-drag"),.2f);
            Add(ELinkedAction::Drag,TEXT(""),TEXT("manual-drag"),.5f);
            Add(ELinkedAction::Subject,TEXT("xinluo-002"),TEXT("before-midflight-back"),.2f);
            Add(ELinkedAction::Back,TEXT(""),TEXT("midflight-back"),4.5f);
            for(const auto& S:C->SurveyControl->Dataset().Subjects)
                if(S.HasCoordinate)Add(ELinkedAction::Subject,S.Id,TEXT("all-points"),6.8f);
            Add(ELinkedAction::Subject,TEXT("xinluo-023"),TEXT("before-no-coordinate"),.1f);
            Add(ELinkedAction::Subject,TEXT("xinluo-024"),TEXT("no-coordinate"),1.8f);
            Add(ELinkedAction::Back,TEXT(""),TEXT("no-coordinate-back"),4.5f);
        }
        if(LinkedEdges)
        {
            // 只走真实F2输入分支；逐阶段等待后取消，并验证返回详情不复活旧轨迹。-codex，2026-11-02
            for(const TCHAR* Phase:{TEXT("survey-orient"),TEXT("survey-visible"),TEXT("survey-beacon"),TEXT("survey-approach"),TEXT("survey-hold")})
            {
                Add(ELinkedAction::Subject,TEXT("xinluo-001"),TEXT("before-keyboard-overview"),.05f);
                Add(ELinkedAction::Overview,Phase,TEXT("keyboard-overview"),.3f);
                Add(ELinkedAction::OverviewReturn,TEXT("xinluo-001"),TEXT("keyboard-overview-return"),.3f);
            }
            Add(ELinkedAction::Reject,TEXT("xinluo-001"),TEXT("preflight-rejection-state"),.5f);
        }
        Add(ELinkedAction::Home,TEXT(""),TEXT("province-return"),3.5f);
        }
    }
    if(!LinkedSteps.IsValidIndex(Stage))return;
    if(LinkedSteps[Stage].Action==ELinkedAction::Hover)
    {
        const int32 I=FCString::Atoi(*LinkedSteps[Stage].Id);
        if(!H->CityButtons.IsValidIndex(I)||!H->CityButtons[I].bIsValid){Fail(TEXT("Hover label missing"));return;}
        C->ProvinceEffects->PreviewPointer(H->CityButtons[I].GetCenter());
    }
    if(!LinkedStepStarted)
    {
        const auto& S=LinkedSteps[Stage];
        if(S.Action==ELinkedAction::Overview&&S.Id!=C->CameraPhase())return;
        FVector2D Hit;bool HasHit=false;
        if(S.Action==ELinkedAction::City)
        {
            const int32 Index=C->Map->Cities.IndexOfByPredicate([](const FDemoCity& City){return City.Code==Xinluo::CityCode;});
            if(!H->CityButtons.IsValidIndex(Index)||!H->CityButtons[Index].bIsValid)return;
            C->PickAt(H->CityButtons[Index].GetCenter());
        }
        else if(S.Action==ELinkedAction::RepeatCity)
        {
            const int32 I=C->Map->SelectedCity;if(!H->CityButtons.IsValidIndex(I)||!H->CityButtons[I].bIsValid)return;
            const FVector2D P=H->CityButtons[I].GetCenter();const FTouchId Touch;
            for(int32 Count=0;Count<3;++Count){C->InputTouch(Touch,ETouchType::Began,P,1,0);C->InputTouch(Touch,ETouchType::Ended,P,0,1);}
        }
        else if(S.Action==ELinkedAction::Subject)
        {
            const int32 I=H->SurveyIds.Find(S.Id);
            if(I<0)return;
            if(!H->bSurveyListExpanded){H->ClickSurvey(H->SurveyToggle.GetCenter());return;}
            if(I<H->SurveyFirstRow||I>=H->SurveyFirstRow+H->SurveyVisibleRows)
            {
                if(H->SurveySelector.bIsValid)H->ScrollSurveyList(I<H->SurveyFirstRow?1.f:-1.f,H->SurveySelector.GetCenter());return;
            }
            if(!H->SurveyRows.IsValidIndex(I)||!H->SurveyRows[I].bIsValid)return;
            Hit=H->SurveyRows[I].GetCenter();HasHit=true;
        }
        else if(S.Action==ELinkedAction::Back||S.Action==ELinkedAction::OverviewReturn){if(!H->SurveyBack.bIsValid)return;Hit=H->SurveyBack.GetCenter();HasHit=true;}
        else if(S.Action==ELinkedAction::Home){if(!H->HomeButton.bIsValid)return;Hit=H->HomeButton.GetCenter();HasHit=true;}
        else if(S.Action==ELinkedAction::Toggle){Hit=H->SurveyToggle.GetCenter();HasHit=true;}
        LinkedBeforeNavigation=C->SurveyControl->Navigation().Fingerprint();
        const bool WasMoving=C->bSurveyFocusMotion;
        if(S.Action==ELinkedAction::ReadOnlyOverview)
        {
            const FPondCameraPose Before=C->CurrentPose();const uint32 Revision=C->Map->SurveyFarmGeometryRevision();
            if(!H->SurveySelector.bIsValid){Fail(TEXT("Overview list not visible"));return;}
            const FVector2D P=H->SurveySelector.Min+FVector2D(80,180);const FTouchId Touch;
            C->InputTouch(Touch,ETouchType::Began,P,1,0);C->InputTouch(Touch,ETouchType::Ended,P,0,1);
            for(const auto& Subject:C->SurveyControl->Dataset().Subjects)
            {
                C->SurveyControl->SelectSubject(Subject.Id);
                if(const FVector* World=H->SurveyWorldPoints.Find(Subject.Id))C->FocusSurveyPoint(Subject.Id,*World);
            }
            if(!SamePose(Before,C->CurrentPose())||Revision!=C->Map->SurveyFarmGeometryRevision()||LinkedBeforeNavigation!=C->SurveyControl->Navigation().Fingerprint())
            {Fail(TEXT("Read-only overview changed camera/model/navigation"));return;}
        }
        if(S.Action==ELinkedAction::Overview)C->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F2,IE_Pressed,1.f));
        if(S.Action==ELinkedAction::Reject)C->RejectSurveyFocus(S.Id,TEXT("explicit diagnostic rejection-state probe"));
        if(S.Action==ELinkedAction::ScrollDown||S.Action==ELinkedAction::ScrollUp)
            H->ScrollSurveyList(S.Action==ELinkedAction::ScrollUp?1.f:-1.f,H->SurveySelector.GetCenter());
        if(HasHit)
        {
            // 引擎内触点事件覆盖按下取消逻辑；不写系统鼠标、不冒充实体触控。-codex，2026-10-30
            const FTouchId Touch;C->InputTouch(Touch,ETouchType::Began,Hit,1,0);C->InputTouch(Touch,ETouchType::Ended,Hit,0,1);
        }
        if(S.Action==ELinkedAction::Toggle||S.Action==ELinkedAction::ScrollDown||S.Action==ELinkedAction::ScrollUp)
        {
            if(LinkedBeforeNavigation!=C->SurveyControl->Navigation().Fingerprint()||(WasMoving&&!C->bSurveyFocusMotion))
            {Fail(TEXT("List operation changed navigation or cancelled closeup"));return;}
        }
        if(S.Action==ELinkedAction::Drag)
        {
            if(!WasMoving){Fail(TEXT("Manual drag test requires an unfinished closeup"));return;}
            const FPondCameraPose BeforeDrag=C->CurrentPose();
            bool Found=false;
            for(int32 Y=300;Y<900&&!Found;Y+=100)for(int32 X=400;X<1300&&!Found;X+=100)
                if(!H->SurveyHit(FVector2D(X,Y))&&!H->SurveyHit(FVector2D(X+25,Y+15))){Hit=FVector2D(X,Y);Found=true;}
            if(!Found){Fail(TEXT("No free map drag area"));return;}
            const FTouchId Touch;C->InputTouch(Touch,ETouchType::Began,Hit,1,0);C->InputTouch(Touch,ETouchType::Moved,Hit+FVector2D(25,15),1,1);C->InputTouch(Touch,ETouchType::Ended,Hit+FVector2D(25,15),0,2);
            if(C->bSurveyFocusMotion||SamePose(BeforeDrag,C->CurrentPose())){Fail(TEXT("Manual drag did not cancel and change the camera"));return;}
        }
        LinkedBeforePose=C->CurrentPose();LinkedStepStart=Clock;LinkedStepStarted=true;
        UE_LOG(LogTemp,Display,TEXT("LinkedCapture step=%d label=%s id=%s"),Stage,*S.Label,*S.Id);
    }
    FScreenshotRequest::RequestScreenshot(TEXT("LinkedCaptureFrame"),false,false);
}

bool FProvinceFXCapture::FinishLinkedStep()
{
    auto* C=Owner.Get();auto* H=Cast<AFujianHUD>(C->GetHUD());const auto& S=LinkedSteps[Stage];const auto& Nav=C->SurveyControl->Navigation();
    if(LinkedOverviewOnly)
    {
        if(!Nav.Subject.IsEmpty()||Nav.ReturnView!=TEXT("city")||C->bSurveyFocusMotion||!C->SurveyFocusSubject.IsEmpty()||C->SurveyFlight.BeaconAge()>=0)return false;
        if(H->SurveyPanel.bIsValid||!H->SurveyPointHits.IsEmpty()||!H->SurveyLabelHits.IsEmpty()||!H->SurveyRows.IsEmpty())return false;
        if(Nav.City==Xinluo::CityCode)
        {
            TArray<FString> Ids;TArray<FVector> Centers;TArray<float> Radii;C->Map->GetSurveyFarmSites(Ids,Centers,Radii);
            if(Ids.Num()!=25||H->SurveyIds.Num()!=26||H->SurveyRippleIds.Num()!=7)return false;
            for(int32 I=0;I<Ids.Num();++I)if(!C->Map->IsSurveyFarmDisplayed(Ids[I])||Radii[I]>8.001f)return false;
        }
        if((S.Action==ELinkedAction::ReadOnlyOverview||S.Action==ELinkedAction::RepeatCity)&&
            (!SamePose(LinkedBeforePose,C->CurrentPose())||LinkedBeforeNavigation!=Nav.Fingerprint()))return false;
    }
    if(S.Action==ELinkedAction::Hover&&(C->ProvinceEffects->PreviewCity()!=FCString::Atoi(*S.Id)||C->ProvinceEffects->PreviewStrength()<.95f))return false;
    if(S.Action==ELinkedAction::Overview||S.Action==ELinkedAction::OverviewReturn)
    {
        if(C->bSurveyFocusMotion||!C->SurveyFocusSubject.IsEmpty()||C->SurveyFlight.BeaconAge()>=0||!SamePose(LinkedBeforePose,C->CurrentPose()))return false;
        if(Nav.View!=(S.Action==ELinkedAction::Overview?TEXT("overview"):TEXT("subject")))return false;
    }
    if(S.Action==ELinkedAction::Reject)
        if(C->SurveyCameraStatus().IsEmpty()||C->bSurveyFocusMotion||C->SurveyFlight.BeaconAge()>=0||!SamePose(LinkedBeforePose,C->CurrentPose())||LinkedBeforeNavigation!=Nav.Fingerprint())return false;
    if(S.Action==ELinkedAction::Subject||S.Action==ELinkedAction::Hold)
    {
        if(Nav.Subject!=S.Id||Nav.View!=TEXT("subject"))return false;
        if(const FVector* Anchor=H->SurveyWorldPoints.Find(S.Id))
        {
            if((S.Duration>=1.5f||S.Action==ELinkedAction::Hold)&&(C->bSurveyFocusMotion||!C->Focus.Equals(*Anchor,.1)))return false;
        }
        else if(C->bSurveyFocusMotion||!SamePose(LinkedBeforePose,C->CurrentPose()))return false;
    }
    if(S.Action==ELinkedAction::City||S.Action==ELinkedAction::CityHold||S.Action==ELinkedAction::Back)
        if(Nav.View!=TEXT("city")||Nav.City!=Xinluo::CityCode||C->bSurveyFocusMotion||C->ProvinceEffects->IsFlying()||!C->Map->Cities.IsValidIndex(C->Map->SelectedCity)||!SamePose(C->CurrentPose(),C->ProvinceEffects->CityPose(C->Map->SelectedCity)))return false;
    if(S.Action==ELinkedAction::CityHold||S.Label==TEXT("closeup-fixed"))
        if(!SamePose(LinkedBeforePose,C->CurrentPose()))return false;
    if(S.Action==ELinkedAction::Home)
        if(Nav.View!=TEXT("province")||Nav.City!=0||C->bSurveyFocusMotion||!C->SurveyCameraStatus().IsEmpty()||C->ProvinceEffects->IsFlying()||!SamePose(C->CurrentPose(),C->ProvinceEffects->Home))return false;
    if(S.Action==ELinkedAction::Drag)
        if(C->bSurveyFocusMotion||!SamePose(LinkedBeforePose,C->CurrentPose())||LinkedBeforeNavigation!=Nav.Fingerprint())return false;
    FString Published,Error;Xinluo::FState State;
    if(!FFileHelper::LoadFileToString(Published,*(LinkedSyncDirectory/TEXT("state.json")))||!Xinluo::FState::Read(Published,C->SurveyControl->Dataset(),State,Error))return false;
    if(State.Session!=C->SurveyControl->Snapshot().Session||State.Revision!=C->SurveyControl->Snapshot().Revision||State.Nav!=Nav)return false;
    return true;
}

void FProvinceFXCapture::CaptureLinkedFrame(int32 W,int32 H,const TArray<FColor>& Pixels)
{
    auto* C=Owner.Get();auto* HUD=C?Cast<AFujianHUD>(C->GetHUD()):nullptr;
    if(!C||!HUD||!C->SurveyControl||!LinkedStepStarted||!LinkedSteps.IsValidIndex(Stage))return;
    if(W!=1920||H!=1080){Fail(TEXT("Linked capture viewport must be 1920x1080"));return;}
    const auto& Step=LinkedSteps[Stage];const auto& State=C->SurveyControl->Snapshot();
    const FVector Eye=C->PlayerCameraManager->GetCameraLocation();const FVector* Anchor=HUD->SurveyWorldPoints.Find(State.Nav.Subject);
    bool GroundFound=false,Blocked=false,Inside=false;float Clearance=0;FVector2D Screen(-1,-1);
    for(const auto& City:C->Map->Cities)if(City.Terrain.IsValid())
    {
        FHitResult Ground;
        if(City.Terrain->LineTraceComponent(Ground,FVector(Eye.X,Eye.Y,5000),FVector(Eye.X,Eye.Y,-100),FCollisionQueryParams(SCENE_QUERY_STAT(LinkedCaptureGround),true)))
        {const float ThisClearance=Eye.Z-Ground.ImpactPoint.Z;if(!GroundFound||ThisClearance<Clearance)Clearance=ThisClearance;GroundFound=true;}
        if(Anchor)
        {
            FHitResult Occlusion;
            // 锚点已由真实地面+8产生；只容许末端1单位浮点/三角边误差，不把山体遮挡列为忽略。-codex，2026-10-30
            if(City.Terrain->LineTraceComponent(Occlusion,Eye,*Anchor,FCollisionQueryParams(SCENE_QUERY_STAT(LinkedCaptureSight),true))&&Occlusion.Distance<FVector::Distance(Eye,*Anchor)-1.f)Blocked=true;
        }
    }
    FHitResult LocalGround;
    if(C->Map->TraceSurveyFarmTerrain(LocalGround,FVector(Eye.X,Eye.Y,5000),FVector(Eye.X,Eye.Y,-100)))
    {const float ThisClearance=Eye.Z-LocalGround.ImpactPoint.Z;if(!GroundFound||ThisClearance<Clearance)Clearance=ThisClearance;GroundFound=true;}
    FHitResult LocalOcclusion;
    if(Anchor&&C->Map->TraceSurveyFarmTerrain(LocalOcclusion,Eye,*Anchor)&&LocalOcclusion.Distance<FVector::Distance(Eye,*Anchor)-1.f)Blocked=true;
    if(Anchor&&C->ProjectWorldLocationToScreen(*Anchor,Screen))
        Inside=Screen.X>20&&Screen.X<W-20&&Screen.Y>100&&Screen.Y<H-65&&!(HUD->SurveySelector.bIsValid&&HUD->SurveySelector.IsInsideOrOn(Screen));
    FVector Water=FVector::ZeroVector;FVector2D WaterHalf;C->Map->GetSurveyFarmWaterAnchor(State.Nav.Subject,Water,WaterHalf);
    FVector2D WaterA,WaterB;float ActualWaterSpan=0;
    if(C->ProjectWorldLocationToScreen(Water+FVector(WaterHalf.X*.65,WaterHalf.Y*.65,0),WaterA)&&C->ProjectWorldLocationToScreen(Water-FVector(WaterHalf.X*.65,WaterHalf.Y*.65,0),WaterB))ActualWaterSpan=float(FVector2D::Distance(WaterA,WaterB))/FMath::Min(W/1920.f,H/1080.f);
    CSV+=FString::Printf(TEXT("%d,%.6f,%s,%s,%s,%lld,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%.3f,%d,%.2f,%.2f,%d,%.6f,%s,%.3f,%.6f,%.6f,%.6f,%.3f,%.3f,%.6f,%.6f\n"),Frame,Clock-LinkedStart,*Step.Label,*State.Nav.Subject,*State.Nav.View,State.Revision,C->Focus.X,C->Focus.Y,C->Focus.Z,Eye.X,Eye.Y,Eye.Z,C->Distance,C->Pitch,C->Yaw,C->bSurveyFocusMotion,GroundFound,Clearance,Blocked,Screen.X,Screen.Y,Inside,FPlatformTime::Seconds(),C->CameraPhase(),C->SurveyBeaconAge(State.Nav.Subject),Water.X,Water.Y,Water.Z,ActualWaterSpan,C->SurveyArrivalAge(State.Nav.Subject),C->SurveyBeaconHeight(State.Nav.Subject),C->SurveyBeaconRadius(State.Nav.Subject));
    const bool Complete=Clock-LinkedStepStart>=Step.Duration;
    // 显式诊断保留城市波纹及首次点选全过程，落稳单帧无法证明扩散/上升/慢推近。-codex，2026-10-31
    const bool Movie=(LinkedOverviewOnly&&Step.Action==ELinkedAction::Hover)||(Step.Label==TEXT("all-points")&&Step.Id==TEXT("xinluo-006"))||Step.Label==TEXT("city-entry")||Step.Label==TEXT("first-closeup")||(LinkedMovie&&(Step.Label==TEXT("city-fixed")||Step.Label==TEXT("closeup-fixed")));
    if(Complete||Movie)
    {
        const FString File=Directory/FString::Printf(TEXT("A-%04d-%02d-%s.jpg"),Frame,Stage,*Step.Label);
        if(!Save(File,Pixels,FIntPoint(W,H))){Fail(TEXT("Linked A frame save failed"));return;}
        if(Complete&&!FFileHelper::SaveStringToFile(State.Serialize(),*(Directory/FString::Printf(TEXT("state-%02d.json"),Stage)))){Fail(TEXT("Linked state save failed"));return;}
    }
    ++Frame;
    if(GroundFound&&Clearance<1.f){Fail(TEXT("Camera intersects terrain during path"));return;}
    if(C->SurveyFlight.GetStage()==FSurveyCameraFlight::EStage::Beacon&&!C->IsSurveyWaterVisible(false))
    {Fail(TEXT("Beacon started outside visible unobstructed water"));return;}
    const bool SettledPoint=Complete&&Anchor&&(Step.Action==ELinkedAction::Hold||(Step.Action==ELinkedAction::Subject&&Step.Duration>=1.5f));
    if(SettledPoint&&!C->IsSurveyWaterVisible()){Fail(TEXT("Settled water not identifiable or blocked"));return;}
    if(SettledPoint&&(Blocked||!Inside)){Fail(TEXT("Settled subject occluded or outside usable viewport"));return;}
    if(!Complete)return;
    if(!FinishLinkedStep()){Fail(TEXT("Linked interaction/state assertion failed"));return;}
    UE_LOG(LogTemp,Display,TEXT("LinkedCapture PASS step=%d label=%s id=%s distance=%.2f inside=%d blocked=%d"),Stage,*Step.Label,*State.Nav.Subject,C->Distance,Inside,Blocked);
    ++Stage;LinkedStepStarted=false;
    if(Stage>=LinkedSteps.Num())
    {
        if(!FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Fail(TEXT("Linked CSV save failed"));return;}
        UE_LOG(LogTemp,Display,TEXT("LinkedCapture complete steps=%d frames=%d A_only=1 physical_touch=0 realtime_fps=0 B_visual=0 output=%s"),Stage,Frame,*Directory);
        Failed=true;FPlatformMisc::RequestExitWithStatus(false,0);
    }
}
