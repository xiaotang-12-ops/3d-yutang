// 显式离屏取证：原生截图回调获取A、纹理读取原B控件，保存JPEG与时序，不捕获用户桌面。
// 固定步长用于镜头/选区，旧B背景仍使用墙钟；录像不作为实时性能基准。-codex，2026-10-27
#pragma once
#include "CoreMinimal.h"
#include "PondCameraMotion.h"
#include "UObject/StrongObjectPtr.h"
class AFujianController;
class SPondDashboard;
class FWidgetRenderer;
class UTextureRenderTarget2D;
class FProvinceFXCapture
{
public:
    explicit FProvinceFXCapture(AFujianController* Controller);
    ~FProvinceFXCapture();
    bool Initialize();
    bool IsRecording() const {return Enabled;}
    void Tick(float Time);
private:
    void CaptureFrame(int32 Width,int32 Height,const TArray<FColor>& Pixels);
    bool Save(const FString& File,const TArray<FColor>& Pixels,FIntPoint Size);
    void Fail(const TCHAR* Reason);
    bool CheckInteraction();
    bool InitializeLinked();
    void TickLinked();
    void CaptureLinkedFrame(int32 Width,int32 Height,const TArray<FColor>& Pixels);
    bool FinishLinkedStep();
    enum class ELinkedAction { City, CityHold, Subject, Back, Home, Toggle, ScrollDown, ScrollUp, Drag, Hold, Hover, Overview, OverviewReturn, Reject, ReadOnlyOverview, RepeatCity };
    struct FLinkedStep
    {
        ELinkedAction Action;
        FString Id,Label;
        float Duration;
    };
    TArray<FLinkedStep> LinkedSteps;
    FString LinkedSyncDirectory,LinkedRestoreId,LinkedBeforeNavigation;
    FPondCameraPose LinkedBeforePose;
    float LinkedStepStart=0,LinkedStart=0;
    bool Linked=false,LinkedAll=false,LinkedStepStarted=false,LinkedStepsReady=false,LinkedMovie=false,LinkedEdges=false,LinkedOverviewOnly=false;
    TWeakObjectPtr<AFujianController> Owner;
    TSharedPtr<SPondDashboard> Dashboard;
    FWidgetRenderer* WidgetRenderer=nullptr;
    TStrongObjectPtr<UTextureRenderTarget2D> DataTarget;
    FDelegateHandle EndFrameHandle;
    FString Directory,CSV;
    float Clock=0,AtlasStart=0;
    int32 Frame=0,Stage=0;
    bool Enabled=false,Failed=false,OldFixed=false,Atlas=false,Reel=false;
    double OldDelta=0;
};
