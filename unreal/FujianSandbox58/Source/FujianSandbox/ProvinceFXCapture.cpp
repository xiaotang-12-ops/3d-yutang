// 只在ProvinceFXCaptureDir显式传入且RenderOffscreen启用时运行；不覆盖已有目录。
// A时间为截图回调收到像素、B时间为纹理读回完成；非原子采集，固定步长影片不冒充实时性能。-codex，2026-10-27
#include "ProvinceFXCapture.h"
#include "FujianMap.h"
#include "ProvinceEffects.h"
#include "PondPresentation.h"
#include "PondDashboard.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "UnrealClient.h"

FProvinceFXCapture::FProvinceFXCapture(AFujianController* C):Owner(C){}
FProvinceFXCapture::~FProvinceFXCapture()
{
    if(EndFrameHandle.IsValid())UGameViewportClient::OnScreenshotCaptured().Remove(EndFrameHandle);
    if(WidgetRenderer){FlushRenderingCommands();BeginCleanup(WidgetRenderer);WidgetRenderer=nullptr;}
    Dashboard.Reset();DataTarget.Reset();
    if(Enabled){FApp::SetUseFixedTimeStep(OldFixed);FApp::SetFixedDeltaTime(OldDelta);}
}
bool FProvinceFXCapture::Initialize()
{
    Linked=FParse::Param(FCommandLine::Get(),TEXT("ProvinceFXLinkedCapture"));
    if(!FParse::Value(FCommandLine::Get(),TEXT("ProvinceFXCaptureDir="),Directory))return !Linked;
    Directory=FPaths::ConvertRelativePathToFull(Directory);
    if(!FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen"))||IFileManager::Get().DirectoryExists(*Directory)||!IFileManager::Get().MakeDirectory(*Directory,true))
    {UE_LOG(LogTemp,Error,TEXT("ProvinceFX capture requires RenderOffscreen and a new output directory"));return false;}
    OldFixed=FApp::UseFixedTimeStep();OldDelta=FApp::GetFixedDeltaTime();Enabled=true;
    FApp::SetUseFixedTimeStep(true);FApp::SetFixedDeltaTime(1./24.);
    Atlas=FParse::Param(FCommandLine::Get(),TEXT("ProvinceFXAtlas"));
    Reel=FParse::Param(FCommandLine::Get(),TEXT("ProvinceFXReel"))&&!Atlas;
    if(Linked)return InitializeLinked();
    // NullApplication没有显示器清单，旧Presentation::Show会直接返回。
    // 取证独立创建无OS窗口的既有B控件，仍绑定同一Presentation，不复制数据/绘制逻辑。-codex，2026-10-27
    auto* C=Owner.Get();
    if(!C||!C->Presentation.IsValid()||!FSlateApplication::Get().IsRenderingOffScreen())return false;
    Dashboard=SNew(SPondDashboard).Presentation(C->Presentation).CaptureWorld(C->GetWorld());
    WidgetRenderer=new FWidgetRenderer(true,true);
    DataTarget.Reset(FWidgetRenderer::CreateTargetFor(FVector2D(1920,1080),TF_Bilinear,true));
    if(!DataTarget.IsValid())return false;
    CSV=TEXT("frame,simulation_time,selected_city,b_city,camera_z,pitch,yaw,distance,a_wall_seconds,b_wall_seconds,hover_city,hover_alpha\n");
    EndFrameHandle=UGameViewportClient::OnScreenshotCaptured().AddRaw(this,&FProvinceFXCapture::CaptureFrame);
    return true;
}
void FProvinceFXCapture::Tick(float Time)
{
    Clock=Time;if(!Enabled||Failed)return;auto* C=Owner.Get();if(!C||!C->Map)return;
    if(Linked){TickLinked();return;}
    // 显式九市图集只录每市落稳帧，复用同一截图/快照链，不改正常交互或20秒预演。-codex，2026-10-27
    if(Atlas)
    {
        if(Clock<6.f)return;
        if(Stage==0){C->SelectProvinceCity(Frame);AtlasStart=Clock;Stage=1;}
        if(Stage==1&&Clock-AtlasStart>=4.25f)FScreenshotRequest::RequestScreenshot(TEXT("ProvinceFXAtlasFrame"),false,false);
        return;
    }
    if(Reel)
    {
        // 三组交互预演复用真实标签命中；只在显式离屏录制参数下驱动，不写系统鼠标。-codex，2026-10-27
        auto* HUD=Cast<AFujianHUD>(C->GetHUD());if(!HUD)return;
        auto Preview=[&](int32 City){if(HUD->CityButtons.IsValidIndex(City)&&HUD->CityButtons[City].bIsValid)C->ProvinceEffects->PreviewPointer(HUD->CityButtons[City].GetCenter());};
        auto Pick=[&](int32 City)
        {
            if(!HUD->CityButtons.IsValidIndex(City)||!HUD->CityButtons[City].bIsValid){Fail(TEXT("Reel label not visible"));return;}
            C->PickAt(HUD->CityButtons[City].GetCenter());
        };
        C->ProvinceEffects->PreviewPointer(FVector2D(-1,-1));
        if(Clock>=10&&Clock<12)Preview(0);
        if(Stage==0&&Clock>=12){Pick(0);Stage=1;}
        if(Clock>=17&&Clock<18)Preview(4);
        if(Stage==1&&Clock>=18){Pick(4);Stage=2;}
        if(Stage==2&&Clock>=24){Pick(1);Stage=3;}
        if(Stage==3&&Clock>=30){C->Home();Stage=4;}
        if(Clock>=6)FScreenshotRequest::RequestScreenshot(TEXT("ProvinceFXFrame"),false,false);
        return;
    }
    // 6秒预热后录20秒：3秒待机、4秒入城、7秒展示、3秒返回、3秒省览。-codex，2026-10-27
    if(Stage==0&&Clock>=9.f)
    {
        // 仅改变显式离屏验证的目标，正常选城、飞行与特效不变；城市码避免索引误指厦门。-codex，2026-10-28
        int32 CaptureCityCode=350100;
        FParse::Value(FCommandLine::Get(),TEXT("ProvinceFXCity="),CaptureCityCode);
        const int32 Index=C->Map->Cities.IndexOfByPredicate([CaptureCityCode](const FDemoCity& D){return D.Code==CaptureCityCode;});
        if(Index==INDEX_NONE){Fail(TEXT("Capture city missing from source dataset"));return;}
        C->SelectProvinceCity(Index);Stage=1;
    }
    if(Stage==1&&Clock>=20.f){C->Home();Stage=2;}
    if(Clock>=6.f)FScreenshotRequest::RequestScreenshot(TEXT("ProvinceFXFrame"),false,false);
}
bool FProvinceFXCapture::Save(const FString& File,const TArray<FColor>& Pixels,FIntPoint Size)
{
    if(Size.X<=0||Size.Y<=0||Pixels.Num()!=Size.X*Size.Y||IFileManager::Get().FileExists(*File))return false;
    TArray64<uint8> JPEG;if(!FImageUtils::CompressImage(JPEG,TEXT("jpg"),FImageView(Pixels.GetData(),Size.X,Size.Y),88))return false;
    return FFileHelper::SaveArrayToFile(JPEG,*File);
}
void FProvinceFXCapture::Fail(const TCHAR* Reason)
{
    if(Linked&&!Directory.IsEmpty())FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Failed=true;UE_LOG(LogTemp,Error,TEXT("ProvinceFX capture failed: %s"),Reason);FPlatformMisc::RequestExitWithStatus(false,1);
}
bool FProvinceFXCapture::CheckInteraction()
{
    // 仅显式取证结束后执行，验证真实HUD点击、九市共享选择、重入、手动中断和返回；不冒充物理触屏测试。
    auto* C=Owner.Get();auto* H=Cast<AFujianHUD>(C->GetHUD());if(!H)return false;
    const FPondCameraPose Home=C->CurrentPose();
    auto Same=[](const FPondCameraPose& A,const FPondCameraPose& B){return A.Focus.Equals(B.Focus,.01)&&FMath::IsNearlyEqual(A.Distance,B.Distance,.01f)&&FMath::IsNearlyEqual(A.Yaw,B.Yaw,.01f)&&FMath::IsNearlyEqual(A.Pitch,B.Pitch,.01f);};
    // 悬停和触摸按下走同一预亮入口：不得改选择/B，也不得在拖动抬起后误选。-codex，2026-10-27
    if(!H->CityButtons.IsValidIndex(0)||!H->CityButtons[0].bIsValid)return false;
    const FVector2D Pointer=H->CityButtons[0].GetCenter();
    C->ProvinceEffects->PreviewPointer(Pointer);C->ProvinceEffects->Tick(.16f);
    if(C->ProvinceEffects->PreviewCity()!=0||C->Map->SelectedCity!=INDEX_NONE||!Same(Home,C->CurrentPose()))return false;
    C->ProvinceEffects->PreviewPointer(FVector2D(-1,-1));C->ProvinceEffects->Tick(.26f);
    if(C->ProvinceEffects->PreviewCity()!=INDEX_NONE)return false;
    const FTouchId TestTouch;
    C->InputTouch(TestTouch,ETouchType::Began,Pointer,1.f,0);C->ProvinceEffects->PreviewPointer(Pointer);C->ProvinceEffects->Tick(.16f);
    if(C->ProvinceEffects->PreviewCity()!=0)return false;
    C->InputTouch(TestTouch,ETouchType::Moved,Pointer+FVector2D(20,0),1.f,1);C->ProvinceEffects->PreviewPointer(Pointer+FVector2D(20,0));C->ProvinceEffects->Tick(.26f);
    C->InputTouch(TestTouch,ETouchType::Ended,Pointer+FVector2D(20,0),0.f,2);
    if(C->ProvinceEffects->PreviewCity()!=INDEX_NONE||C->Map->SelectedCity!=INDEX_NONE)return false;
    C->ApplyPose(Home);
    C->SelectProvinceCity(0);C->ProvinceEffects->Tick(1.5f);
    C->InputTouch(TestTouch,ETouchType::Began,Pointer,1.f,3);
    if(C->ProvinceEffects->IsFlying())return false;
    C->InputTouch(TestTouch,ETouchType::Ended,Pointer,0.f,4);
    if(!C->ProvinceEffects->IsFlying())return false;
    C->ProvinceEffects->Tick(2.6f);if(C->ProvinceEffects->IsFlying())return false;
    C->Home();C->ProvinceEffects->Tick(3.1f);C->Presentation->Tick(.01f);
    if(!Same(Home,C->CurrentPose()))return false;
    for(int32 I=0;I<C->Map->Cities.Num();I++)
    {
        if(!H->CityButtons.IsValidIndex(I)||!H->CityButtons[I].bIsValid)return false;
        C->PickAt(H->CityButtons[I].GetCenter());C->Presentation->Tick(.01f);
        if(C->Map->SelectedCity!=I||C->Presentation->Snapshot().City!=I||!Same(Home,C->CurrentPose()))return false;
    }
    C->ProvinceEffects->Tick(.5f);C->CancelCamera(TEXT("province-fx-selftest"));
    const FPondCameraPose Stopped=C->CurrentPose();C->ProvinceEffects->Tick(.5f);
    if(!Same(Stopped,C->CurrentPose()))return false;
    C->SelectProvinceCity(0);if(!Same(Stopped,C->CurrentPose()))return false;
    C->ProvinceEffects->Resize();C->ProvinceEffects->Tick(4.1f);
    if(C->ProvinceEffects->IsFlying())return false;
    const FPondCameraPose Settled=C->CurrentPose();C->SelectProvinceCity(0);
    if(C->ProvinceEffects->IsFlying()||!Same(Settled,C->CurrentPose()))return false;
    C->Home();C->ProvinceEffects->Tick(3.1f);C->Presentation->Tick(.01f);
    if(!Same(Home,C->CurrentPose())||C->Presentation->Snapshot().City!=INDEX_NONE)return false;
    UE_LOG(LogTemp,Display,TEXT("ProvinceFX interaction checks PASS: hover without selection, hover exit, touch drag cancel, midflight same-city touch resume, nine HUD picks, shared B selection, rapid retarget, cancel, repeated city, resize/refit, Home"));return true;
}
void FProvinceFXCapture::CaptureFrame(int32 Width,int32 Height,const TArray<FColor>& A)
{
    if(!Enabled||Failed||Clock<6.f)return;auto* C=Owner.Get();
    if(Linked){CaptureLinkedFrame(Width,Height,A);return;}
    if(!C||!C->Map||!C->Presentation.IsValid()||!GEngine||!GEngine->GameViewport||!GEngine->GameViewport->Viewport){Fail(TEXT("A/B unavailable"));return;}
    const int32 FrameLimit=Reel?720:480;
    if(!Atlas&&Frame>=FrameLimit)
    {
        Failed=true;
        if(!CheckInteraction()){Fail(TEXT("Interaction checks failed"));return;}
        if(!FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Fail(TEXT("CSV save failed"));return;}
        UE_LOG(LogTemp,Display,TEXT("ProvinceFX captured frames=%d duration=%d fps=24 output=%s"),Frame,FrameLimit/24,*Directory);Failed=true;FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    const int32 ACity=C->Map->SelectedCity,BCity=C->Presentation->Snapshot().City;
    if(ACity!=BCity){Fail(TEXT("A/B selected city mismatch"));return;}
    // 首帧取证时再记录实际驻留纹理；启动早期可能仍显示32px占位，不能拿导入尺寸冒充运行尺寸。-codex，2026-10-28
    if(Frame==0||(!Atlas&&Frame==240))GEngine->Exec(C->GetWorld(),TEXT("ListTextures"));
    TArray<FColor> B;const FIntPoint AS(Width,Height);
    const double AT=FPlatformTime::Seconds();
    WidgetRenderer->DrawWidget(DataTarget.Get(),Dashboard.ToSharedRef(),FVector2D(1920,1080),1.f/24.f);
    FlushRenderingCommands();
    if(!DataTarget->GameThread_GetRenderTargetResource()->ReadPixels(B)){Fail(TEXT("B render target readback failed"));return;}
    const double BT=FPlatformTime::Seconds();
    const FIntPoint BS(DataTarget->SizeX,DataTarget->SizeY);
    if(AS!=FIntPoint(1920,1080)||BS!=FIntPoint(1920,1080)){Fail(TEXT("Unexpected A/B viewport size"));return;}
    if(!Save(Directory/FString::Printf(TEXT("A-%04d.jpg"),Frame),A,AS)||!Save(Directory/FString::Printf(TEXT("B-%04d.jpg"),Frame),B,BS)){Fail(TEXT("Frame save failed"));return;}
    const FVector Location=C->PlayerCameraManager->GetCameraLocation();
    CSV+=FString::Printf(TEXT("%d,%.6f,%d,%d,%.3f,%.3f,%.3f,%.3f,%.6f,%.6f,%d,%.3f\n"),Frame,Clock-6,ACity,BCity,Location.Z,C->Pitch,C->Yaw,C->Distance,AT,BT,C->ProvinceEffects->PreviewCity(),C->ProvinceEffects->PreviewStrength());
    if(Frame%48==0)UE_LOG(LogTemp,Display,TEXT("ProvinceFX frame=%d city=%d z=%.2f pitch=%.1f"),Frame,ACity,Location.Z,C->Pitch);
    Frame++;
    if(Atlas)
    {
        UE_LOG(LogTemp,Display,TEXT("ProvinceFX atlas city=%d code=%d z=%.2f"),ACity,C->Map->Cities[ACity].Code,Location.Z);Stage=0;
        if(Frame>=C->Map->Cities.Num())
        {
            if(!FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Fail(TEXT("Atlas CSV save failed"));return;}
            UE_LOG(LogTemp,Display,TEXT("ProvinceFX atlas captured cities=%d"),Frame);Failed=true;FPlatformMisc::RequestExitWithStatus(false,0);
        }
    }
}
