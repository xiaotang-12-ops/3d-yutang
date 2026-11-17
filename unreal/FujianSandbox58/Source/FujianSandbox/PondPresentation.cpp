// 同进程第二原生窗口；关闭B不退出A，世界结束主动解绑，避免回调悬挂。
// ReviewMonitor仅为本机第二屏内的临时并排验证，不改系统显示器配置。-codex，2026-10-21
#include "PondPresentation.h"
#include "PondDashboard.h"
#include "PondVisualProof.h"
#include "FujianMap.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FPondPresentation::FPondPresentation(AFujianController* InOwner):Owner(InOwner)
{if(FParse::Param(FCommandLine::Get(),TEXT("PondVisualProof")))VisualProof=MakeShared<FPondVisualProof>();}
void FPondPresentation::Tick(float DeltaSeconds)
{
    if(bShuttingDown||!Owner.IsValid()||!Owner->Map)return;
    auto* Map=Owner->Map.Get();
    if(Current.Revision==0||Current.City!=Map->SelectedCity||Current.Site!=Map->SelectedSite)
    {
        // 收拢期间再次点击时保留屏上真正可见的旧对象；展开期间保存当前阶段，避免瞬间补全旧图再退出。
        // 旧/新快照各自连同身份绘制，选择仍只有Map一份；最新点击覆盖，不排队。-codex，2026-10-22
        if(Transition>=.38f||Previous.Revision==0)
        {Previous=Current;PreviousTransition=Transition;}
        Refresh(Map);
    }
    Transition=FMath::Min(Transition+FMath::Max(0.f,DeltaSeconds),4.f);
    if(!bOpened&&GEngine&&GEngine->GameViewport&&GEngine->GameViewport->GetWindow().IsValid())Show();
    if(Dashboard.IsValid())Dashboard->Invalidate(EInvalidateWidgetReason::Paint);
    if(VisualProof.IsValid())VisualProof->Tick(DeltaSeconds,Owner.Get(),Window);
}
void FPondPresentation::Show()
{
    // 即使旧调用方意外持有Presentation，联动模式也不允许打开第三个模拟窗口。-codex，2026-10-28
    if(FParse::Param(FCommandLine::Get(),TEXT("XinluoLinked")))return;
    if(bShuttingDown||Window.IsValid()||!FSlateApplication::IsInitialized())return;
    FDisplayMetrics Metrics;FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    if(Metrics.MonitorInfo.IsEmpty())return;
    const bool Review=FParse::Param(FCommandLine::Get(),TEXT("PondReviewMonitor"));
    // 仅性能取证时让双1080p窗口叠放在副屏；不启用Proof额外绘制，也不占用用户第一屏。-codex，2026-10-22
    const bool ReviewFullHD=Review&&FParse::Param(FCommandLine::Get(),TEXT("PondReviewFullHD"));
    auto A=GEngine&&GEngine->GameViewport?GEngine->GameViewport->GetWindow():TSharedPtr<SWindow>();
    int32 AIndex=0;const FVector2D Center=A.IsValid()?A->GetClientRectInScreen().GetCenter():FVector2D::ZeroVector;
    for(int32 I=0;I<Metrics.MonitorInfo.Num();I++){const auto R=Metrics.MonitorInfo[I].DisplayRect;if(Center.X>=R.Left&&Center.X<R.Right&&Center.Y>=R.Top&&Center.Y<R.Bottom)AIndex=I;}
    int32 BIndex=AIndex;
    if(!Review)for(int32 I=0;I<Metrics.MonitorInfo.Num();I++)if(I!=AIndex){BIndex=I;break;}
    if(Review)for(int32 I=0;I<Metrics.MonitorInfo.Num();I++)if(!Metrics.MonitorInfo[I].bIsPrimary){BIndex=I;break;}
    const auto R=Metrics.MonitorInfo[BIndex].WorkArea;
    const float FullWidth=R.Right-R.Left,FullHeight=R.Bottom-R.Top;
    const FVector2D Size=ReviewFullHD?FVector2D(1920,1080):FVector2D(Review?FullWidth*.49f:FullWidth-24,Review?FMath::Min(FullHeight-80,FullWidth*.49f*9.f/16.f):FullHeight-56);
    const FVector2D Position(ReviewFullHD?R.Left+8:Review?R.Left+FullWidth*.505f:R.Left+12,R.Top+60);
    if(Review&&A.IsValid()){A->MoveWindowTo(FVector2D(R.Left+4,R.Top+60));A->Resize(Size);}
    SAssignNew(Window,SWindow).Title(FText::FromString(TEXT("福建 · 山海养殖｜经营数据 B"))).AutoCenter(EAutoCenter::None).ScreenPosition(Position).ClientSize(Size).AdjustInitialSizeAndPositionForDPIScale(false).FocusWhenFirstShown(false).SupportsMaximize(true);
    SAssignNew(Dashboard,SPondDashboard).Presentation(SharedThis(this)).CaptureWorld(Owner.IsValid()?Owner->GetWorld():nullptr);Window->SetContent(Dashboard.ToSharedRef());
    TWeakPtr<FPondPresentation> Weak=SharedThis(this);
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([Weak](const TSharedRef<SWindow>&){if(auto Self=Weak.Pin()){Self->Window.Reset();Self->Dashboard.Reset();UE_LOG(LogTemp,Display,TEXT("PondWindow B closed; A selection retained"));}}));
    bOpened=true;FSlateApplication::Get().AddWindow(Window.ToSharedRef(),true);
    UE_LOG(LogTemp,Display,TEXT("PondWindow monitors=%d A=%d B=%d review=%d rect=%.0f,%.0f size=%.0f,%.0f"),Metrics.MonitorInfo.Num(),AIndex,BIndex,Review,Position.X,Position.Y,Size.X,Size.Y);
    if(ReviewFullHD)
    {
        // 初建窗口会被桌面工作区限高；创建后仅测试参数再设客户区，和已有取证窗口一致。-codex，2026-10-22
        Window->Resize(Size);
        const FVector2D ActualA=A.IsValid()?FVector2D(A->GetClientSizeInScreen()):FVector2D::ZeroVector;
        const FVector2D ActualB=Window->GetClientSizeInScreen();
        UE_LOG(LogTemp,Display,TEXT("PondReviewFullHD actualClientA=%.0fx%.0f actualClientB=%.0fx%.0f (overlapped secondary-monitor windows; no proof draw)"),ActualA.X,ActualA.Y,ActualB.X,ActualB.Y);
    }
}
void FPondPresentation::Shutdown()
{
    bShuttingDown=true;
    if(Window.IsValid()){Window->SetOnWindowClosed(FOnWindowClosed());Window->RequestDestroyWindow();Window.Reset();}Dashboard.Reset();Owner.Reset();
}
