// 复用Map选择入口与Slate真实帧缓冲，生成省/市/塘与快切的确定性运行证据。
// 默认完全不运行，仅-PondVisualProof启用；不注入操作系统输入，不改数据或摄像机算法。-codex，2026-10-22
#include "PondVisualProof.h"
#include "FujianMap.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/ScopeExit.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"

void FPondVisualProof::Capture(const FString& Name,const TSharedPtr<SWindow>& DataWindow,bool bMotionFrame)
{
    // A12仅静态04/06可显式记录/复用实际渲染POV；不改Actor、控制器或正常运动。
    // LocalPlayer读取CameraManager缓存及GetFOVAngle，故同时核对缓存和实际FOV；退出总恢复。-codex，2026-10-23
    auto* Manager=ProofOwner.IsValid()?ProofOwner->PlayerCameraManager.Get():nullptr;
    const bool LocalDetail=Name==TEXT("06-local-detail");
    const bool StaticPose=!bMotionFrame&&(Name==TEXT("04-city-settled")||Name==TEXT("06-pond-settled")||LocalDetail);
    const bool RecordPose=StaticPose&&FParse::Param(FCommandLine::Get(),TEXT("PondFixedPoseRecord"));
    FString PoseDirectory;
    const bool ReusePose=StaticPose&&FParse::Value(FCommandLine::Get(),TEXT("PondFixedPoseDir="),PoseDirectory);
    FMinimalViewInfo OriginalPOV;
    FSceneViewProjectionData CheckedProjection;
    bool ProjectionChecked=false;
    bool PoseApplied=false;
    ON_SCOPE_EXIT
    {
        if(PoseApplied&&Manager)
        {
            Manager->SetCameraCachePOV(OriginalPOV);
            const auto& Restored=Manager->GetCameraCacheView();
            const bool RestoredOK=Restored.Location.Equals(OriginalPOV.Location,1e-7)&&Restored.Rotation.Equals(OriginalPOV.Rotation,1e-7)&&FMath::IsNearlyEqual(Restored.FOV,OriginalPOV.FOV,1e-6f);
            UE_LOG(LogTemp,Display,TEXT("PondFixedPose restore=%d capture=%s"),RestoredOK,*Name);
        }
    };
    if(LocalDetail&&!RecordPose&&!ReusePose){UE_LOG(LogTemp,Error,TEXT("PondFixedPose local detail requires explicit record/reuse mode"));return;}
    if(RecordPose||ReusePose)
    {
        if(!Manager||RecordPose==ReusePose){UE_LOG(LogTemp,Error,TEXT("PondFixedPose requires camera and exactly one record/reuse mode"));return;}
        OriginalPOV=Manager->GetCameraCacheView();
        TSharedPtr<FJsonObject> Baseline;
        if(ReusePose)
        {
            FString Text;TSharedPtr<FJsonObject> Object;
            if(!FFileHelper::LoadFileToString(Text,*(PoseDirectory/(Name+TEXT("-pose.json"))))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Object)||!Object.IsValid())
            {UE_LOG(LogTemp,Error,TEXT("PondFixedPose missing/invalid baseline %s"),*Name);return;}
            const TCHAR* Fields[]={TEXT("x"),TEXT("y"),TEXT("z"),TEXT("pitch"),TEXT("yaw"),TEXT("roll"),TEXT("fov")};
            double Values[7];
            for(int32 I=0;I<7;I++)if(!Object->TryGetNumberField(Fields[I],Values[I])||!FMath::IsFinite(Values[I]))
            {UE_LOG(LogTemp,Error,TEXT("PondFixedPose invalid field %s"),Fields[I]);return;}
            if(Values[6]<=1||Values[6]>=170){UE_LOG(LogTemp,Error,TEXT("PondFixedPose invalid FOV"));return;}
            FMinimalViewInfo Fixed=OriginalPOV;Fixed.Location=FVector(Values[0],Values[1],Values[2]);Fixed.Rotation=FRotator(Values[3],Values[4],Values[5]);Fixed.FOV=Values[6];
            Baseline=Object;Manager->SetCameraCachePOV(Fixed);PoseApplied=true;
            const auto& Actual=Manager->GetCameraCacheView();
            if(!Actual.Location.Equals(Fixed.Location,1e-7)||!Actual.Rotation.Equals(Fixed.Rotation,1e-7)||!FMath::IsNearlyEqual(Manager->GetFOVAngle(),Fixed.FOV,1e-6f))
            {UE_LOG(LogTemp,Error,TEXT("PondFixedPose actual POV/FOV differs; capture refused"));return;}
        }
        else if(LocalDetail)
        {
            // 原06局部仅约98x70像素；独立静态近看保留其朝向，以固定世界目标和650距离取图。
            // 不触碰正常Focus/Distance/Motion，视口读回后恢复原缓存。-codex，2026-10-23
            FMinimalViewInfo Fixed=OriginalPOV;
            Fixed.Location=FVector(247.5,796.5,210.275)-Fixed.Rotation.Vector()*650.;
            Manager->SetCameraCachePOV(Fixed);PoseApplied=true;
        }
        const auto& Actual=Manager->GetCameraCacheView();
        if(!FMath::IsNearlyEqual(Manager->GetFOVAngle(),Actual.FOV,1e-6f))
        {UE_LOG(LogTemp,Error,TEXT("PondFixedPose locked FOV differs; capture refused"));return;}
        TSharedRef<FJsonObject> Object=MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("x"),Actual.Location.X);Object->SetNumberField(TEXT("y"),Actual.Location.Y);Object->SetNumberField(TEXT("z"),Actual.Location.Z);
        Object->SetNumberField(TEXT("pitch"),Actual.Rotation.Pitch);Object->SetNumberField(TEXT("yaw"),Actual.Rotation.Yaw);Object->SetNumberField(TEXT("roll"),Actual.Rotation.Roll);Object->SetNumberField(TEXT("fov"),Manager->GetFOVAngle());
        Object->SetStringField(TEXT("capture"),Name);Object->SetBoolField(TEXT("reuse"),ReusePose);
        auto* Local=ProofOwner->GetLocalPlayer();
        auto* Viewport=GEngine&&GEngine->GameViewport?GEngine->GameViewport->Viewport:nullptr;
        if(!Local||!Viewport||!Local->GetProjectionData(Viewport,CheckedProjection)||!CheckedProjection.ViewOrigin.Equals(Actual.Location,1e-6))
        {UE_LOG(LogTemp,Error,TEXT("PondFixedPose actual projection unavailable/mismatched"));return;}
        const auto Size=Viewport->GetSizeXY();
        Object->SetNumberField(TEXT("width"),Size.X);Object->SetNumberField(TEXT("height"),Size.Y);
        for(int32 Row=0;Row<4;Row++)for(int32 Col=0;Col<4;Col++)
        {
            const FString Key=FString::Printf(TEXT("projection_%d_%d"),Row,Col);
            const double Value=CheckedProjection.ProjectionMatrix.M[Row][Col];Object->SetNumberField(Key,Value);
            double Expected=0;
            if(ReusePose&&(!Baseline->TryGetNumberField(Key,Expected)||!FMath::IsNearlyEqual(Value,Expected,1e-7)))
            {UE_LOG(LogTemp,Error,TEXT("PondFixedPose projection differs %s"),*Key);return;}
        }
        double BaselineWidth=0,BaselineHeight=0;
        if(ReusePose&&(!Baseline->TryGetNumberField(TEXT("width"),BaselineWidth)||!Baseline->TryGetNumberField(TEXT("height"),BaselineHeight)||BaselineWidth!=Size.X||BaselineHeight!=Size.Y))
        {UE_LOG(LogTemp,Error,TEXT("PondFixedPose viewport differs"));return;}
        ProjectionChecked=true;
        FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));
        const FString PoseOutput=Directory/(Name+TEXT("-pose.json"));
        if(IFileManager::Get().FileExists(*PoseOutput)||!FFileHelper::SaveStringToFile(Text,*PoseOutput,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {UE_LOG(LogTemp,Error,TEXT("PondFixedPose diagnostic save failed"));return;}
        UE_LOG(LogTemp,Display,TEXT("PondFixedPose reuse=%d capture=%s location=%s rotation=%s fov=%.9g"),ReusePose,*Name,*Actual.Location.ToString(),*Actual.Rotation.ToString(),Manager->GetFOVAngle());
    }
    // Slate截图只重绘窗口，不保证游戏视口纹理新鲜；连续读回前显式刷新本取证进程的3D缓冲。
    // 本机可见/隐藏两组对照均曾只取到9张不同A帧，此处仅显式取证路径执行，不用于性能采样。-codex，2026-10-22
    if(GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport)GEngine->GameViewport->Viewport->Draw(false);
    if(ProjectionChecked)
    {
        FSceneViewProjectionData After;
        if(!ProofOwner->GetLocalPlayer()->GetProjectionData(GEngine->GameViewport->Viewport,After)||!After.ViewOrigin.Equals(CheckedProjection.ViewOrigin,1e-7)||!After.ViewRotationMatrix.Equals(CheckedProjection.ViewRotationMatrix,1e-7)||!After.ProjectionMatrix.Equals(CheckedProjection.ProjectionMatrix,1e-7))
        {UE_LOG(LogTemp,Error,TEXT("PondFixedPose post-draw view changed; readback refused"));return;}
        UE_LOG(LogTemp,Display,TEXT("PondFixedPose post_draw_verified=1 capture=%s"),*Name);
    }
    const auto A=GEngine&&GEngine->GameViewport?GEngine->GameViewport->GetWindow():TSharedPtr<SWindow>();
    const TArray<TPair<FString,TSharedPtr<SWindow>>> Windows={{TEXT("A"),A},{TEXT("B"),DataWindow}};
    struct FFrame
    {
        FString Screen;
        TArray<FColor> Pixels;
        FIntVector Size=FIntVector::ZeroValue;
        double Captured=0,ReadSeconds=0;
        bool bRead=false;
    };
    TArray<FFrame> Frames;
    // 先连续读回两屏，再编码/写盘；两次读回仍有时差，清单保留真实时间，不能冒充原子双屏录屏。-codex，2026-10-22
    for(const auto& Item:Windows)
    {
        FFrame& Frame=Frames.AddDefaulted_GetRef();Frame.Screen=Item.Key;
        const double Start=FPlatformTime::Seconds();
        if(Item.Value.IsValid())Frame.bRead=FSlateApplication::Get().TakeScreenshot(Item.Value->GetContent(),Frame.Pixels,Frame.Size);
        Frame.Captured=FPlatformTime::Seconds();Frame.ReadSeconds=Frame.Captured-Start;
        Frame.bRead=Frame.bRead&&Frame.Size.X>0&&Frame.Size.Y>0&&Frame.Pixels.Num()==Frame.Size.X*Frame.Size.Y;
    }
    for(auto& Frame:Frames)
    {
        const double WriteStart=FPlatformTime::Seconds();
        bool Saved=false;
        const FString Path=Directory/(Name+TEXT("-")+Frame.Screen+(bMotionFrame?TEXT(".bmp"):TEXT(".png")));
        if(Frame.bRead)
        {
            for(auto& Pixel:Frame.Pixels)Pixel.A=255;
            // 连续取证不用PNG压缩：本机实测编码一对1080p约0.21秒，会掩盖正常转场节奏。-codex，2026-10-22
            if(bMotionFrame)Saved=FImageUtils::SaveImageByExtension(*Path,FImageView(Frame.Pixels.GetData(),Frame.Size.X,Frame.Size.Y));
            else
            {
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Frame.Size.X,Frame.Size.Y,Frame.Pixels,PNG);
                Saved=PNG.Num()>0&&FFileHelper::SaveArrayToFile(PNG,*Path);
            }
        }
        if(!Saved){if(bMotionFrame)MotionFailures++;UE_LOG(LogTemp,Error,TEXT("PondProof capture failed %s"),*Path);}
        if(bMotionFrame)
        {
            MotionRows+=FString::Printf(TEXT("%d,%s,%.6f,%.6f,%.6f,%d,%d,%d\n"),MotionFrames,*Frame.Screen,Frame.Captured-MotionEpoch,Frame.ReadSeconds,FPlatformTime::Seconds()-WriteStart,Frame.Size.X,Frame.Size.Y,Saved?1:0);
        }
        else UE_LOG(LogTemp,Display,TEXT("PondProof capture=%s size=%dx%d saved=%d"),*Path,Frame.Size.X,Frame.Size.Y,Saved);
    }
}

void FPondVisualProof::RecordMotion(const TSharedPtr<SWindow>& DataWindow)
{
    if(!bRecordMotion||bMotionFinished||Time<3)return;
    const double Now=FPlatformTime::Seconds();
    if(MotionFrames==0)MotionEpoch=Now;
    if(Time<30.5&&Now>=NextMotionCapture)
    {
        Capture(FString::Printf(TEXT("motion-%06d"),MotionFrames),DataWindow,true);
        MotionFrames++;
        // 12Hz是采样上限，不追赶漏帧；按每屏实际时间合成VFR，录制负荷不能当正常性能。-codex，2026-10-22
        NextMotionCapture=Now+1.0/12.0;
    }
    else if(Time>=30.5)
    {
        const bool ManifestSaved=FFileHelper::SaveStringToFile(MotionRows,*(Directory/TEXT("motion-frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        const bool Complete=ManifestSaved&&MotionFrames>0&&MotionFailures==0;
        const FString Summary=FString::Printf(TEXT("complete=%d\npairs=%d\nfailed_windows=%d\nwall_seconds=%.6f\nprogrammatic_selection_only=1\nperformance_evidence=0\n"),Complete?1:0,MotionFrames,MotionFailures,Now-MotionEpoch);
        const bool SummarySaved=FFileHelper::SaveStringToFile(Summary,*(Directory/TEXT("motion-result.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("PondMotion complete=%d pairs=%d failed_windows=%d manifest=%d summary=%d wall=%.3f"),Complete&&SummarySaved,MotionFrames,MotionFailures,ManifestSaved,SummarySaved,Now-MotionEpoch);
        bMotionFinished=true;
    }
}
void FPondVisualProof::Tick(float Delta,AFujianController* Owner,const TSharedPtr<SWindow>& B)
{
    if(!Owner||!Owner->Map||!B.IsValid())return;
    ProofOwner=Owner;
    auto* Map=Owner->Map.Get();
    if(!Started)
    {
        Started=true;
        bRecordMotion=FParse::Param(FCommandLine::Get(),TEXT("PondMotionFrames"));
        if(bRecordMotion)MotionRows=TEXT("frame,screen,seconds,read_seconds,write_seconds,width,height,saved\n");
        if(!FParse::Value(FCommandLine::Get(),TEXT("PondCaptureDir="),Directory))Directory=FPaths::ProjectSavedDir()/TEXT("PondProof");
        IFileManager::Get().MakeDirectory(*Directory,true);
        for(int32 I=0;I<Map->Cities.Num();I++)if(Map->Cities[I].Code==350100)Fuzhou=I;
        // 仅验证进程扩大自身窗口到1080p；沿现有副屏位置，不移动/激活任何其他应用。-codex，2026-10-22
        B->Resize(FVector2D(1920,1080));
        const auto A=GEngine&&GEngine->GameViewport?GEngine->GameViewport->GetWindow():TSharedPtr<SWindow>();
        if(A.IsValid()){A->Resize(FVector2D(1920,1080));B->MoveWindowTo(A->GetPositionInScreen());}
        if(Fuzhou==INDEX_NONE){UE_LOG(LogTemp,Error,TEXT("PondProof missing Fuzhou fixture"));Step=100;return;}
    }
    Time+=Delta;
    RecordMotion(B);
    auto City=[&](int32 Code){for(int32 I=0;I<Map->Cities.Num();I++)if(Map->Cities[I].Code==Code){Map->ShowCity(I);break;}};
    if(Step==0&&Time>=5){Capture(TEXT("01-province"),B);Map->ShowCity(Fuzhou);Step++;}
    else if(Step==1&&Time>=6){Capture(TEXT("02-city-enter"),B);Step++;}
    else if(Step==2&&Time>=7.4){Capture(TEXT("03-city-wings"),B);Step++;}
    // 相机定位1.5秒+环绕3.2秒，留额外帧余量；B停稳不能代替A停稳。-codex，2026-10-22
    else if(Step==3&&Time>=11){Capture(TEXT("04-city-settled"),B);Step++;}
    else if(Step==4&&Time>=12)
    {
        for(int32 I=0;I<Map->Cities[Fuzhou].Sites.Num();I++)if(Map->Cities[Fuzhou].Sites[I].Id.EndsWith(TEXT("-08"))){Map->SelectSite(I);break;}Step++;
    }
    else if(Step==5&&Time>=13.4){Capture(TEXT("05-pond-enter"),B);Step++;}
    else if(Step==6&&Time>=18){Capture(TEXT("06-pond-settled"),B);if(FParse::Param(FCommandLine::Get(),TEXT("PondLocalDetail")))Capture(TEXT("06-local-detail"),B);City(350500);Step++;}
    else if(Step==7&&Time>=18.2){City(350200);Step++;}
    else if(Step==8&&Time>=24){Capture(TEXT("07-latest-city"),B);Owner->Home();Step++;}
    else if(Step==9&&Time>=30){Capture(TEXT("08-return-province"),B);Step++;UE_LOG(LogTemp,Display,TEXT("PondProof sequence complete; programmatic selection only"));}
    else if(Step==10&&Time>=31&&FParse::Param(FCommandLine::Get(),TEXT("PondProofExit")))FPlatformMisc::RequestExit(false);
}
