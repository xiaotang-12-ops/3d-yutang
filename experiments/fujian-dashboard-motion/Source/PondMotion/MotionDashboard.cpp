// 真实双屏展示：完整目标可打断，画面交接只使用冻结的可见状态；采集仍走原backbuffer链。-codex，2026-10-28
#include "MotionDashboard.h"
#include "MotionStage.h"
#include "MotionDataPage.h"
#include "MotionFlickerProbe.h"
#include "MotionPondSprites.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/SlateViewportProvider.h"
#include "RHICommandList.h"
#include "Misc/ScopeLock.h"
struct FMotionCaptureState
{
 FCriticalSection Mutex;bool Requested=false,Ready=false;uint32 Generation=0;TArray<FColor> Pixels;FIntPoint Size;double Time=0,ReadMs=0;FString RequestedTargetId;
};

using namespace PondDashboard;
SMotionDashboard::SMotionDashboard()=default;
void SMotionDashboard::Construct(const FArguments& Args)
{
 Stage=Args._Stage;Data.Load();Started=LastFrame=PageEntered=FPlatformTime::Seconds();NextMetrics=Started+10;
 PondSprites=MakeUnique<FMotionPondSprites>();
 if(Stage.IsValid())Stage->SetSpritePondsEnabled(true);
 // Stage构造期附件尚未注册，未用槽的初始父级隐藏未必传播到子网格。
 // Actor已由StartPlay生成并注册；首帧前走公开视图接口强制传播一次，再进入真实目标，不改Stage资产/实现。
 if(Stage.IsValid()){Stage->SetPresentation(EMotionStageView::City,0,INDEX_NONE,{});Stage->SetPresentation(EMotionStageView::Hidden,0,INDEX_NONE,{});}
 CaptureDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../../artifacts/fujian-dashboard-motion/native")/FDateTime::Now().ToString(TEXT("session-%Y%m%d-%H%M%S")));
 bLinked=Xinluo::Linked();ReceiverSession=FGuid::NewGuid().ToString();
 if(bLinked){bLinkReady=Link.Open(TEXT("display"));if(!bLinkReady)LinkError=Link.Error;}
 else if(Data.Error.IsEmpty())
 {
  Xinluo::FState Initial;Initial.Dataset=Data.Survey.Version;Initial.Session=ReceiverSession;Initial.Revision=Initial.Heartbeat=1;Initial.Nav.SelectCity(Xinluo::CityCode);
  if(FMotionFlickerProbe::Enabled())Initial.Nav.Overview();
  ApplyTarget(Initial);
 }
}
SMotionDashboard::~SMotionDashboard()
{
 if(CaptureHandle.IsValid()&&FSlateApplication::IsInitialized())FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(CaptureHandle);
 FString CSV=TEXT("wall_frame_ms\n");for(float V:Samples)CSV+=FString::Printf(TEXT("%.4f\n"),V);
 FFileHelper::SaveStringToFile(CSV,*(FPaths::ProjectSavedDir()/TEXT("motion-frame-times.csv")));
}
FVector2D SMotionDashboard::Logical(const FGeometry& G,FVector2D P)const
{const float S=FMath::Min(G.GetLocalSize().X/1920.f,G.GetLocalSize().Y/1080.f);return S>0?(G.AbsoluteToLocal(P)-(G.GetLocalSize()-FVector2D(1920,1080)*S)*.5)/S:FVector2D(-100,-100);}
bool SMotionDashboard::Hit(float X,float Y,float W,float H)const{return Pointer.X>=X&&Pointer.X<X+W&&Pointer.Y>=Y&&Pointer.Y<Y+H;}
bool SMotionDashboard::ApplyTarget(const Xinluo::FState& S)
{
 if(!Data.Error.IsEmpty()||S.Dataset!=Data.Survey.Version||!S.Nav.Validate(Data.Survey))return false;
 // 旧画面从不借用目标对象；总览已打开也接收subject/group/return变化，无提前return。
 TargetState=S;bHasTarget=true;bPageTarget=S.Nav.View==TEXT("overview");Revision++;
 bPageSwitching=VisualState.Nav!=TargetState.Nav||PageVeil>0;
 UE_LOG(LogTemp,Display,TEXT("XinluoAccepted session=%s revision=%lld city=%d group=%d id=%s view=%s return=%s"),*S.Session,S.Revision,S.Nav.City,S.Nav.Group,*S.Nav.Subject,*S.Nav.View,*S.Nav.ReturnView);
 return true;
}
void SMotionDashboard::Preview(const Xinluo::FNavigation& Nav)
{
 if(bLinked||!Nav.Validate(Data.Survey)||Nav==TargetState.Nav)return;
 auto S=TargetState;S.Nav=Nav;++S.Revision;++S.Heartbeat;ApplyTarget(S);
}
void SMotionDashboard::SetDataPage(bool Open)
{auto N=TargetState.Nav;if(Open)N.Overview();else if(N.View==TEXT("overview"))N.Back();Preview(N);}
void SMotionDashboard::Back(){auto N=TargetState.Nav;N.Back();Preview(N);}
FString SMotionDashboard::LinkStatus(double Now)const
{
 if(!Data.Error.IsEmpty())return Data.Error;
 if(!bLinked)return TEXT("独立预览 · 公开演示数据");
 if(!LinkError.IsEmpty())return LinkError;
 return Receiver.Health(Now);
}
void SMotionDashboard::Poll(double Now)
{
 if(!bLinked||!bLinkReady)return;
 if(Now>=NextPoll)
 {
  NextPoll=Now+.1;FString Text;
  if(Link.Read(TEXT("state.json"),Text))
  {
   Xinluo::FState S;FString Error;
   if(Xinluo::FState::Read(Text,Data.Survey,S,Error))Receiver.Observe(S,Now);else Receiver.Error=Error;
  }
  if(Receiver.HasPulse&&Receiver.HasPending&&ApplyTarget(Receiver.Pending))Receiver.Commit();
 }
 if(Now>=NextAck)
 {
  Xinluo::FAck A;A.Session=Receiver.Accepted.Session;A.Dataset=Data.Survey.Version;A.ReceiverSession=ReceiverSession;A.Accepted=Receiver.Accepted.Revision;
  A.Visible=PaintedSession==A.Session?FMath::Min(PaintedRevision,A.Accepted):0;A.Heartbeat=++AckHeartbeat;A.Health=Receiver.Health(Now);
  const bool Written=Link.Write(TEXT("ack.json"),A.Serialize());NextAck=Now+(Written?1.:.25);
  if(Written)LinkError.Empty();else LinkError=Link.Error;
 }
}
FReply SMotionDashboard::OnMouseMove(const FGeometry& G,const FPointerEvent& E)
{Pointer=bLinked?FVector2D(-100,-100):Logical(G,E.GetScreenSpacePosition());return FReply::Handled();}
void SMotionDashboard::OnMouseLeave(const FPointerEvent& E){SLeafWidget::OnMouseLeave(E);Pointer={-100,-100};}
FReply SMotionDashboard::OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
 if(bLinked)return FReply::Handled().SetUserFocus(AsShared());
 Pointer=Logical(G,E.GetScreenSpacePosition());if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
 UE_LOG(LogTemp,Display,TEXT("MotionPointer x=%.1f y=%.1f button=%s"),Pointer.X,Pointer.Y,*E.GetEffectingButton().ToString());
 if(bPageSwitching||!bHasTarget||!Data.Error.IsEmpty())return FReply::Handled();
 auto N=TargetState.Nav;
 if(N.City!=Xinluo::CityCode)
 {
  if(Hit(700,620,520,64)){N.SelectCity(Xinluo::CityCode);Preview(N);}
  return FReply::Handled();
 }
 if(Hit(1680,28,210,54)){SetDataPage(!bPageTarget);return FReply::Handled();}
 if(N.View!=TEXT("overview")&&Hit(1450,28,210,54)){Back();return FReply::Handled();}
 if(N.View==TEXT("overview")||N.View==TEXT("subject"))return FReply::Handled();
 // 独立预览按当前可见模型从近到远命中，联动B仍在函数入口保持只读。-codex，2026-10-31
 if(MotionCarousel::HeroOpacity(CarouselFrame)>.15f&&MotionCarousel::HeroCard().IsInsideOrOn(Pointer)&&Data.Survey.Subjects.IsValidIndex(CarouselFrame.FocusIndex))
 {
  N.SelectSubject(Data.Survey,Data.Survey.Subjects[CarouselFrame.FocusIndex].Id);Preview(N);return FReply::Handled();
 }
 for(int I=CarouselFrame.Items.Num()-1;I>=0;--I)
 {
  const auto& Item=CarouselFrame.Items[I];
  if(MotionCarousel::CanSelectPond(Item,Pointer)&&Data.Survey.Subjects.IsValidIndex(Item.Index)
     &&PondImageRect(Item.Index,MotionCarousel::PondHit(Item)).IsInsideOrOn(Pointer))
  {N.SelectSubject(Data.Survey,Data.Survey.Subjects[Item.Index].Id);Preview(N);break;}
 }
 return FReply::Handled().SetUserFocus(AsShared());
}
FReply SMotionDashboard::OnKeyDown(const FGeometry&,const FKeyEvent& E)
{
 if(E.GetKey()==EKeys::F9){if(E.IsShiftDown())bHandoffScreenshot=true;else bScreenshot=true;return FReply::Handled();}
 if(E.GetKey()==EKeys::F10)
 {
  if(CaptureState){FScopeLock Lock(&CaptureState->Mutex);CaptureState->Generation++;CaptureState->Requested=false;CaptureState->Ready=false;}
  bRecording=!bRecording;if(bRecording){RecordDirectory=CaptureDirectory/FString::Printf(TEXT("take-%02d"),++TakeIndex);RecordStart=FPlatformTime::Seconds();NextCapture=RecordStart;FrameIndex=0;CaptureRows=TEXT("frame,time_seconds,read_encode_ms,request_target_id\n");}
  UE_LOG(LogTemp,Display,TEXT("MotionRecording enabled=%d"),bRecording);return FReply::Handled();
 }
 // 联动B不解释Escape/Home/Enter及任何旧月份热点，只有A能改变业务状态。
 if(bLinked)return FReply::Handled();
 if(E.GetKey()==EKeys::Escape){Back();return FReply::Handled();}
 if(E.GetKey()==EKeys::Home){auto N=TargetState.Nav;N.SelectCity(0);Preview(N);return FReply::Handled();}
 return FReply::Unhandled();
}
void SMotionDashboard::InitializeCapture()
{
 if(CaptureHandle.IsValid()||!GEngine||!GEngine->GameViewport)return;
 const auto Window=GEngine->GameViewport->GetWindow();if(!Window)return;
 CaptureState=MakeShared<FMotionCaptureState,ESPMode::ThreadSafe>();const auto State=CaptureState;const SWindow* Target=Window.Get();
 // 只读取本窗口已经绘制的backbuffer；不调用会再次绘制Slate的TakeScreenshot。
 // 回调只持有独立共享状态，不捕获Widget；纹理与provider仅在当前渲染回调内使用。
 CaptureHandle=FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddLambda([State,Target](SWindow& W,ISlateViewportProvider& Provider)
 {
  if(&W!=Target)return;
  uint32 Generation;{FScopeLock Lock(&State->Mutex);if(!State->Requested||State->Ready)return;Generation=State->Generation;}
  auto* Texture=Provider.GetBackBufferResource();if(!Texture)return;
  const auto Size=Texture->GetSizeXY();TArray<FColor> Pixels;const double Start=FPlatformTime::Seconds();
  FRHICommandListExecutor::GetImmediateCommandList().ReadSurfaceData(Texture,FIntRect(0,0,Size.X,Size.Y),Pixels,FReadSurfaceDataFlags(RCM_UNorm));
  FScopeLock Lock(&State->Mutex);if(Generation!=State->Generation)return;State->Pixels=MoveTemp(Pixels);State->Size=Size;State->Time=Start;State->ReadMs=(FPlatformTime::Seconds()-Start)*1000;State->Ready=true;State->Requested=false;
 });
}
void SMotionDashboard::Capture(double Now)
{
 InitializeCapture();if(!CaptureState)return;
 TArray<FColor> Pixels;FIntPoint Size;double FrameTime=0,ReadMs=0;FString RequestedTargetId;
 {
  FScopeLock Lock(&CaptureState->Mutex);
  // 此ID仅记录采集请求时的目标，不作为回读图像的显示身份；转场身份必须从原帧核验。
  if(!CaptureState->Ready){if(!CaptureState->Requested){CaptureState->RequestedTargetId=VisualState.Nav.Subject;CaptureState->Requested=true;}return;}
  Pixels=MoveTemp(CaptureState->Pixels);Size=CaptureState->Size;FrameTime=CaptureState->Time;ReadMs=CaptureState->ReadMs;RequestedTargetId=CaptureState->RequestedTargetId;CaptureState->Ready=false;
 }
 if(Pixels.Num()!=Size.X*Size.Y){bScreenshot=false;bRecording=false;UE_LOG(LogTemp,Error,TEXT("MotionCapture empty backbuffer"));return;}
 const FString Directory=bScreenshot?CaptureDirectory:RecordDirectory;IFileManager::Get().MakeDirectory(*Directory,true);
 const double Start=FPlatformTime::Seconds();
 auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Wrapper=Module.CreateImageWrapper(EImageFormat::JPEG);
 Wrapper->SetRaw(Pixels.GetData(),Pixels.Num()*sizeof(FColor),Size.X,Size.Y,ERGBFormat::BGRA,8);const auto& JPEG=Wrapper->GetCompressed(92);
 const FString Name=bScreenshot?FString::Printf(TEXT("shot-%03d.jpg"),ShotIndex++):FString::Printf(TEXT("frame-%05d.jpg"),FrameIndex);
 if(!FFileHelper::SaveArrayToFile(JPEG,*(Directory/Name))){UE_LOG(LogTemp,Error,TEXT("MotionCapture cannot write %s"),*(Directory/Name));bRecording=false;bScreenshot=false;return;}
 if(bScreenshot)UE_LOG(LogTemp,Display,TEXT("MotionStill view=%s switching=%d session=%s revision=%lld city=%d site=%s group=%d request_site=%s pixels=%dx%d path=%s"),*VisualState.Nav.View,bPageSwitching,*VisualState.Session,VisualState.Revision,VisualState.Nav.City,*VisualState.Nav.Subject,Page,*RequestedTargetId,Size.X,Size.Y,*(Directory/Name));
 if(bRecording&&!bScreenshot)
 {
  CaptureRows+=FString::Printf(TEXT("%d,%.6f,%.3f,%s\n"),FrameIndex++,FrameTime-RecordStart,ReadMs+(FPlatformTime::Seconds()-Start)*1000,*RequestedTargetId);
  if(!FFileHelper::SaveStringToFile(CaptureRows,*(Directory/TEXT("capture-times.csv")))){UE_LOG(LogTemp,Error,TEXT("MotionCapture cannot write timestamp index"));bRecording=false;}
 }
 bScreenshot=false;NextCapture=FrameTime+1.0/30.0;
}

void SMotionDashboard::Tick(const FGeometry& G,double Time,float Delta)
{
 SLeafWidget::Tick(G,Time,Delta);const double Now=FPlatformTime::Seconds();Poll(Now);
 if(FMotionFlickerProbe::Enabled()){if(!FlickerProbe)FlickerProbe=MakeShared<FMotionFlickerProbe>();FlickerProbe->Tick(ProbePaintCount,bPageSwitching,PageVeil,G.GetLocalSize(),IsVolatile());}
 if(bHasTarget)
 {
  const float Step=FMath::Clamp(Delta,0.f,.1f)*4.f;
  if(VisualState.Nav!=TargetState.Nav)
  {
   PageVeil=FMath::Min(1.f,PageVeil+Step);
   if(PageVeil>=1.f)
   {
    VisualState=TargetState;PageEntered=Now;
    if(bHandoffScreenshot){bHandoffScreenshot=false;bScreenshot=true;}
    UE_LOG(LogTemp,Display,TEXT("XinluoHandoff session=%s revision=%lld group=%d id=%s view=%s"),*VisualState.Session,VisualState.Revision,VisualState.Nav.Group,*VisualState.Nav.Subject,*VisualState.Nav.View);
   }
  }
  else {VisualState=TargetState;PageVeil=FMath::Max(0.f,PageVeil-Step);}
  bDataPage=VisualState.Nav.View==TEXT("overview");Page=VisualState.Nav.Group;
  Current=Data.Snapshot(VisualState.Nav.City==Xinluo::CityCode?0:-1,-1);Overview=Current;
  bPageSwitching=VisualState.Nav!=TargetState.Nav||PageVeil>0;
 }
 // 模型、文字、详情只消费这份帧；自动轮转不调用Preview，不改变联动回执的业务身份。-codex，2026-10-31
 const bool bCityCarousel=bHasTarget&&VisualState.Nav.City==Xinluo::CityCode&&VisualState.Nav.View==TEXT("city");
 if(bCityCarousel)
 {
  if(!bCarouselActive||CarouselNavigation!=VisualState.Nav||CarouselSession!=VisualState.Session)
  {
   const int32 Selected=Data.Survey.Find(VisualState.Nav.Subject);
   CarouselPosition=Selected>=0?Selected:0;CarouselNavigation=VisualState.Nav;CarouselSession=VisualState.Session;
   LastCarouselFocus=INDEX_NONE;
  }
  else if(!bPageSwitching)CarouselPosition+=FMath::Clamp(double(Delta),0.,.048)*MotionCarousel::RadiansPerSecond/MotionCarousel::SlotStep;
  CarouselFrame=MotionCarousel::Build(CarouselPosition,Data.Survey.Subjects.Num(),++CarouselSerial);
  if(CarouselFrame.FocusIndex!=LastCarouselFocus&&Data.Survey.Subjects.IsValidIndex(CarouselFrame.FocusIndex))
  {
   LastCarouselFocus=CarouselFrame.FocusIndex;
   UE_LOG(LogTemp,Display,TEXT("MotionCarousel front=%s slot=%d phase=%.6f serial=%llu business=%s revision=%lld"),*Data.Survey.Subjects[LastCarouselFocus].Id,CarouselFrame.FocusSlot,CarouselPosition,CarouselSerial,*VisualState.Nav.Subject,VisualState.Revision);
  }
 }
 else CarouselFrame=MotionCarousel::FFrame();
 bCarouselActive=bCityCarousel;
 if(Stage.IsValid())
 {
  TArray<FString> Ids;const auto& Nav=VisualState.Nav;int32 Seed=INDEX_NONE;
  if(bHasTarget&&Nav.City==Xinluo::CityCode)
  {
   const int32 Selected=Data.Survey.Find(Nav.Subject);
   Seed=Selected>=0?Data.Survey.Subjects[Selected].Ordinal:Data.Survey.Subjects[Nav.Group*Xinluo::PageSize].Ordinal;
   if(bDataPage||Nav.View==TEXT("subject"))Ids.Add(Selected>=0?Nav.Subject:Data.Survey.Subjects[Nav.Group*Xinluo::PageSize].Id);
   else
   {
    Ids.SetNum(MotionCarousel::PoolSize);
    for(const auto& Item:CarouselFrame.Items)if(Data.Survey.Subjects.IsValidIndex(Item.Index))Ids[Item.Slot]=Data.Survey.Subjects[Item.Index].Id;
   }
  }
  // ID/组/seed全部取VisualState，目标在另一城或总览时也不会提前污染旧舞台。
  const auto View=Ids.IsEmpty()?EMotionStageView::Hidden:bDataPage?EMotionStageView::DataOverview:Nav.View==TEXT("subject")?EMotionStageView::Pond:EMotionStageView::City;
  Stage->SetCityCarouselFrame(CarouselFrame);
  Stage->SetPresentation(View,FMath::Clamp(float(Now-PageEntered)/.8f,0.f,1.f),Seed,Ids);
 }
 if(Now-Started>8&&Samples.Num()<36000)Samples.Add(float((Now-LastFrame)*1000));LastFrame=Now;
 if(Now>NextMetrics){NextMetrics=Now+10;UE_LOG(LogTemp,Display,TEXT("MotionRuntime client=%.0fx%.0f accepted=%lld painted=%lld id=%s view=%s frameDelta=%.3fms"),G.GetLocalSize().X,G.GetLocalSize().Y,TargetState.Revision,PaintedRevision,*VisualState.Nav.Subject,*VisualState.Nav.View,Delta*1000);}
 if(bScreenshot||(bRecording&&Now>=NextCapture))Capture(Now);
 Invalidate(EInvalidateWidgetReason::Paint);
}
void SMotionDashboard::DrawPageControl(PondDashboard::FCanvas C,double Now)const
{
 C.Layer+=300;
 if(bPageSwitching){auto Veil=C;Veil.Opacity=PageVeil;Veil.Box(0,0,1920,1080,RGB(2,12,13));}
 C.Layer+=2;
 if(VisualState.Nav.City==Xinluo::CityCode)
 {
  if(!bLinked)
  {
   C.Gradient(1680,28,210,54,RGB(16,53,45),RGB(8,37,35,235));C.Line(1680,28,1890,28,RGB(144,245,197,135));
   const FString Label=bDataPage?(VisualState.Nav.ReturnView==TEXT("subject")?TEXT("← 返回主体详情"):TEXT("← 返回养殖分布")):TEXT("数据总览  →");
   C.Text(1693,42,Label,18,RGB(220,251,235),185,1);
  }
 }
}
int32 SMotionDashboard::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle&,bool)const
{
 PondDashboard::FCanvas C(G,E,L);const double Now=FPlatformTime::Seconds();
 if(FMotionFlickerProbe::Enabled()){++ProbePaintCount;FMotionFlickerProbe::PaintMarker(C);}
 if(!Data.Error.IsEmpty()||!bHasTarget||VisualState.Nav.City!=Xinluo::CityCode)
 {
  C.Gradient(0,0,1920,1080,RGB(12,22,29),RGB(5,12,18));
  C.Text(200,350,TEXT("山海渔业 · 水产调研"),42,White,1500,1);
  C.Text(200,460,!Data.Error.IsEmpty()?Data.Error:!bHasTarget?TEXT("调研资料展示准备中"):TEXT("本次调研仅覆盖龙岩市新罗区"),27,Gold,1500,1);
  if(!bLinked)C.Text(200,530,TEXT("选择下方入口查看本次调研"),19,Muted,1500,1);
  if(!bLinked&&Data.Error.IsEmpty())
  {C.Gradient(700,620,520,64,RGB(16,53,45),RGB(8,37,35));C.Text(720,636,TEXT("进入龙岩新罗调研  →"),22,White,480,1);}
 }
 else if(bDataPage)
 {
  auto Pond=C;Pond.Opacity*=Ease(FMath::Clamp(float(Now-PageEntered)/.8f,0.f,1.f));
  const int32 Selected=Data.Survey.Find(VisualState.Nav.Subject);
  DrawPond(Pond,Selected>=0?Selected:VisualState.Nav.Group*Xinluo::PageSize,FBox2D(FVector2D(310,300),FVector2D(1050,740)));
  C.Layer+=10;
  MotionDataPage::Draw(C,Overview,FMath::Clamp(float(Now-PageEntered)/1.2f,0.f,1.f),Now-Started,-1);
 }
 else if(VisualState.Nav.View==TEXT("subject"))DrawSubject(C,VisualState.Nav);
 else DrawCity(C,VisualState.Nav);
 DrawPageControl(C,Now);
 // 这是本窗口绘制提交记录，不冒充物理屏截图验收；回执下一Tick才发送。
 if(bHasTarget&&!bPageSwitching&&Now-PageEntered>=1.2&&VisualState.Session==TargetState.Session&&VisualState.Revision==TargetState.Revision&&(PaintedSession!=VisualState.Session||PaintedRevision!=VisualState.Revision))
 {
  PaintedSession=VisualState.Session;PaintedRevision=VisualState.Revision;
  UE_LOG(LogTemp,Display,TEXT("XinluoPainted session=%s revision=%lld group=%d id=%s view=%s"),*PaintedSession,PaintedRevision,VisualState.Nav.Group,*VisualState.Nav.Subject,*VisualState.Nav.View);
 }
 return C.Layer+(FMotionFlickerProbe::Enabled()?440:340);
}
