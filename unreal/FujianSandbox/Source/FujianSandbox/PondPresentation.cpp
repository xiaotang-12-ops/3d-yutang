// 同进程第二原生窗口；关闭B不退出A，世界结束主动解绑，避免回调悬挂。
// ReviewMonitor仅为本机第二屏内的临时并排验证，不改系统显示器配置。-codex，2026-10-21
#include "PondPresentation.h"
#include "PondDashboard.h"
#include "FujianMap.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FPondPresentation::FPondPresentation(AFujianController* InOwner):Owner(InOwner){}
void FPondPresentation::Tick(float DeltaSeconds)
{
    if(bShuttingDown||!Owner.IsValid()||!Owner->Map)return;
    auto* Map=Owner->Map.Get();
    if(Current.Revision==0||Current.City!=Map->SelectedCity||Current.Site!=Map->SelectedSite)Refresh(Map);
    Transition+=DeltaSeconds;
    if(!bOpened&&GEngine&&GEngine->GameViewport&&GEngine->GameViewport->GetWindow().IsValid())Show();
    if(Dashboard.IsValid())Dashboard->Invalidate(EInvalidateWidgetReason::Paint);
}
void FPondPresentation::Show()
{
    if(bShuttingDown||Window.IsValid()||!FSlateApplication::IsInitialized())return;
    FDisplayMetrics Metrics;FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    if(Metrics.MonitorInfo.IsEmpty())return;
    const bool Review=FParse::Param(FCommandLine::Get(),TEXT("PondReviewMonitor"));
    auto A=GEngine&&GEngine->GameViewport?GEngine->GameViewport->GetWindow():TSharedPtr<SWindow>();
    int32 AIndex=0;const FVector2D Center=A.IsValid()?A->GetClientRectInScreen().GetCenter():FVector2D::ZeroVector;
    for(int32 I=0;I<Metrics.MonitorInfo.Num();I++){const auto R=Metrics.MonitorInfo[I].DisplayRect;if(Center.X>=R.Left&&Center.X<R.Right&&Center.Y>=R.Top&&Center.Y<R.Bottom)AIndex=I;}
    int32 BIndex=AIndex;
    if(!Review)for(int32 I=0;I<Metrics.MonitorInfo.Num();I++)if(I!=AIndex){BIndex=I;break;}
    if(Review)for(int32 I=0;I<Metrics.MonitorInfo.Num();I++)if(!Metrics.MonitorInfo[I].bIsPrimary){BIndex=I;break;}
    const auto R=Metrics.MonitorInfo[BIndex].WorkArea;
    const float FullWidth=R.Right-R.Left,FullHeight=R.Bottom-R.Top;
    const FVector2D Size(Review?FullWidth*.49f:FullWidth-24,Review?FMath::Min(FullHeight-80,FullWidth*.49f*9.f/16.f):FullHeight-56);
    const FVector2D Position(Review?R.Left+FullWidth*.505f:R.Left+12,R.Top+60);
    if(Review&&A.IsValid()){A->MoveWindowTo(FVector2D(R.Left+4,R.Top+60));A->Resize(Size);}
    SAssignNew(Window,SWindow).Title(FText::FromString(TEXT("福建 · 山海养殖｜经营数据 B"))).AutoCenter(EAutoCenter::None).ScreenPosition(Position).ClientSize(Size).AdjustInitialSizeAndPositionForDPIScale(false).FocusWhenFirstShown(false).SupportsMaximize(true);
    SAssignNew(Dashboard,SPondDashboard).Presentation(SharedThis(this));Window->SetContent(Dashboard.ToSharedRef());
    TWeakPtr<FPondPresentation> Weak=SharedThis(this);
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([Weak](const TSharedRef<SWindow>&){if(auto Self=Weak.Pin()){Self->Window.Reset();Self->Dashboard.Reset();UE_LOG(LogTemp,Display,TEXT("PondWindow B closed; A selection retained"));}}));
    bOpened=true;FSlateApplication::Get().AddWindow(Window.ToSharedRef(),true);
    UE_LOG(LogTemp,Display,TEXT("PondWindow monitors=%d A=%d B=%d review=%d rect=%.0f,%.0f size=%.0f,%.0f"),Metrics.MonitorInfo.Num(),AIndex,BIndex,Review,Position.X,Position.Y,Size.X,Size.Y);
}
void FPondPresentation::Shutdown()
{
    bShuttingDown=true;
    if(Window.IsValid()){Window->SetOnWindowClosed(FOnWindowClosed());Window->RequestDestroyWindow();Window.Reset();}Dashboard.Reset();Owner.Reset();
}
