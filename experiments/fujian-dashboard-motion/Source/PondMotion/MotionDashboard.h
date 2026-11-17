// 展示屏完整目标与可见状态分离；同一可见状态同时驱动文本、组和Stage ID。-codex，2026-10-28
#pragma once
#include "Widgets/SLeafWidget.h"
#include "MotionData.h"
#include "XinluoScreenLink.h"
#include "PondDashboardDraw.h"
#include "MotionCarousel.h"
struct FMotionCaptureState;
class AMotionStage;
class FMotionFlickerProbe;
class FMotionPondSprites;
class SMotionDashboard:public SLeafWidget
{
public:
 SMotionDashboard();
 SLATE_BEGIN_ARGS(SMotionDashboard){} SLATE_ARGUMENT(TWeakObjectPtr<AMotionStage>,Stage) SLATE_END_ARGS()
 void Construct(const FArguments&);
 virtual ~SMotionDashboard() override;
 virtual FVector2D ComputeDesiredSize(float) const override{return {1920,1080};}
 virtual bool SupportsKeyboardFocus()const override{return true;}
 virtual void Tick(const FGeometry&,double,float)override;
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
 virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&)override;
 virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent&)override;
 virtual void OnMouseLeave(const FPointerEvent&)override;
 virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&)override;
private:
 TWeakObjectPtr<AMotionStage> Stage;FMotionData Data;FMotionSnapshot Current,Overview;
 TUniquePtr<FMotionPondSprites> PondSprites;
 Xinluo::FTransport Link;Xinluo::FReceiver Receiver;Xinluo::FState TargetState,VisualState;
 FString ReceiverSession,LinkError;bool bLinked=false,bLinkReady=false,bHasTarget=false;
 double NextPoll=0,NextAck=0;int64 AckHeartbeat=0;
 mutable int64 PaintedRevision=0;mutable FString PaintedSession;
 bool ApplyTarget(const Xinluo::FState& State);void Preview(const Xinluo::FNavigation& Nav);
 void Poll(double Now);FString LinkStatus(double Now)const;
 void Back();void SetDataPage(bool Open);
 bool bDataPage=false,bPageTarget=false,bPageSwitching=false;
 double PageEntered=0;float PageVeil=0;bool bHandoffScreenshot=false;
 int32 Page=0,Revision=0;FVector2D Pointer=FVector2D(-100,-100);
 double Started=0,LastFrame=0,NextMetrics=0;TArray<float> Samples;
 MotionCarousel::FFrame CarouselFrame;double CarouselPosition=0;
 Xinluo::FNavigation CarouselNavigation;FString CarouselSession;bool bCarouselActive=false;
 uint64 CarouselSerial=0;int32 LastCarouselFocus=INDEX_NONE;
 TSharedPtr<FMotionFlickerProbe> FlickerProbe;mutable uint64 ProbePaintCount=0;
 bool bRecording=false,bScreenshot=false,bCapturing=false;double NextCapture=0,RecordStart=0;int32 FrameIndex=0,ShotIndex=0;FString CaptureRows;void Capture(double Now);
 TSharedPtr<FMotionCaptureState,ESPMode::ThreadSafe> CaptureState;FDelegateHandle CaptureHandle;void InitializeCapture();FString CaptureDirectory,RecordDirectory;int32 TakeIndex=0;
 FVector2D Logical(const FGeometry&,FVector2D)const;bool Hit(float,float,float,float)const;
 void DrawCity(PondDashboard::FCanvas C,const Xinluo::FNavigation& Nav)const;
 void DrawSubject(PondDashboard::FCanvas C,const Xinluo::FNavigation& Nav)const;
 void DrawPond(PondDashboard::FCanvas C,int32 Index,const FBox2D& Bounds)const;
 FBox2D PondImageRect(int32 Index,const FBox2D& Bounds)const;
 void DrawPageControl(PondDashboard::FCanvas C,double Now)const;
};
