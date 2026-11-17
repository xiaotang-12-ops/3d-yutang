// 对照同控件与独立Slate兄弟的标记，区分UI漏画与Present之后的显示问题。
// 异步读回顶部区域，仅保存标记计数与真实内容亮度；仅诊断开关启用，不能把无丢样解释为显示器无闪。-codex，2026-10-24
#include "MotionFlickerProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/SlateViewportProvider.h"
#include "Widgets/SWindow.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Styling/CoreStyle.h"
#include "RHIGPUReadback.h"
#include "RHICommandList.h"
#include "RenderingThread.h"

struct FMotionProbeRenderState
{
 struct FSlot
 {
  TUniquePtr<FRHIGPUTextureReadback> Readback;
  bool Pending=false;EPixelFormat Format=PF_Unknown;double Sent=0;uint64 Frame=0;
 };
 FSlot Slots[2];
 double Started=0,LastReport=0;uint64 Frame=0,Submitted=0,Ready=0,Skipped=0,Failed=0,MissingInside=0,MissingSibling=0;
 bool Baseline=false,Done=false;int32 LastInside=0,LastSibling=0;double MaxLatency=0;
 double ContentBase=0,TitleMean=0,NumberMean=0,RingMean=0,TitleMin=255,TitleMax=0;uint64 ContentSamples=0,ContentDrops=0;
 static FIntVector Decode(const uint8* P,EPixelFormat Format)
 {
  if(Format==PF_A2B10G10R10)
  {uint32 V=0;FMemory::Memcpy(&V,P,4);return FIntVector((V&1023u)*255u/1023u,((V>>10)&1023u)*255u/1023u,((V>>20)&1023u)*255u/1023u);}
  return FIntVector(P[Format==PF_B8G8R8A8?2:0],P[1],P[Format==PF_B8G8R8A8?0:2]);
 }
 void Read(FRHITexture* Texture)
 {
  check(IsInRenderingThread());
  if(Done||!Texture)return;
  const double Now=FPlatformTime::Seconds();if(Started==0)Started=Now;++Frame;
  for(FSlot& Slot:Slots)if(Slot.Pending&&Slot.Readback->IsReady())
  {
   int32 Pitch=0,Height=0;const uint8* Bytes=static_cast<const uint8*>(Slot.Readback->Lock(Pitch,&Height));
   if(Bytes&&Pitch>=1920&&Height>=256)
   {
    int32 Magenta=0,Cyan=0;
    for(int32 Y=0;Y<32;++Y)for(int32 X=0;X<64;++X)
    {
     const uint8* P=Bytes+(Y*Pitch+X)*4;
     const auto RGB=Decode(P,Slot.Format);const int32 R=RGB.X,G=RGB.Y,B=RGB.Z;
     Magenta+=(R>180&&G<80&&B>180);Cyan+=(R<80&&G>180&&B>180);
    }
    auto Mean=[&](int32 X0,int32 Y0,int32 X1,int32 Y1)
    {
     uint64 Sum=0;for(int32 Y=Y0;Y<Y1;++Y)for(int32 X=X0;X<X1;++X)
     {const auto V=Decode(Bytes+(Y*Pitch+X)*4,Slot.Format);Sum+=V.X+V.Y+V.Z;}
     return double(Sum)/(3.*(X1-X0)*(Y1-Y0));
    };
    TitleMean=Mean(365,40,690,120);NumberMean=Mean(1320,155,1590,215);RingMean=Mean(500,180,600,220);
    if(Now-Started>5.)
    {
     if(ContentBase==0)ContentBase=TitleMean;
     ++ContentSamples;TitleMin=FMath::Min(TitleMin,TitleMean);TitleMax=FMath::Max(TitleMax,TitleMean);
     if(FMath::Abs(TitleMean-ContentBase)>ContentBase*.1)
     {++ContentDrops;if(ContentDrops<=12)UE_LOG(LogTemp,Warning,TEXT("MotionProbe contentChange frame=%llu title=%.4f base=%.4f number=%.4f ring=%.4f"),Slot.Frame,TitleMean,ContentBase,NumberMean,RingMean);}
    }
    ++Ready;LastInside=Magenta;LastSibling=Cyan;MaxLatency=FMath::Max(MaxLatency,(Now-Slot.Sent)*1000.);
    if(Magenta>=180&&Cyan>=180)Baseline=true;
    if(Baseline&&(Magenta<180||Cyan<180))
    {
     MissingInside+=Magenta<180;MissingSibling+=Cyan<180;
     if(MissingInside+MissingSibling<=12)UE_LOG(LogTemp,Warning,TEXT("MotionProbe missing submitFrame=%llu ageMs=%.2f inside=%d sibling=%d"),Slot.Frame,(Now-Slot.Sent)*1000.,Magenta,Cyan);
    }
   }
   else ++Failed;
   // 成功或失败的映射均成对释放，不能把映射失败当成UI消失。-codex，2026-10-24
   Slot.Readback->Unlock();Slot.Pending=false;
  }
  const auto Size=Texture->GetSizeXY();const EPixelFormat Format=Texture->GetFormat();
  const bool Supported=Size==FIntPoint(1920,1080)&&(Format==PF_B8G8R8A8||Format==PF_R8G8B8A8||Format==PF_A2B10G10R10);
  bool Sent=false;
  if(Now-Started<90.&&Supported)
  {
   for(FSlot& Slot:Slots)if(!Slot.Pending)
   {
    if(!Slot.Readback||Slot.Format!=Format)Slot.Readback=MakeUnique<FRHIGPUTextureReadback>(TEXT("MotionFlickerProbe"));
    Slot.Format=Format;Slot.Sent=Now;Slot.Frame=Frame;
    auto& Cmd=FRHICommandListExecutor::GetImmediateCommandList();
    Cmd.Transition(FRHITransitionInfo(Texture,ERHIAccess::Unknown,ERHIAccess::CopySrc));
    Slot.Readback->EnqueueCopy(Cmd,Texture,FResolveRect(0,0,1920,256));
    Cmd.Transition(FRHITransitionInfo(Texture,ERHIAccess::CopySrc,ERHIAccess::Present));
    Slot.Pending=true;++Submitted;Sent=true;break;
   }
   if(!Sent)++Skipped;
  }
  if(Now-LastReport>=1.)
  {
   LastReport=Now;
   UE_LOG(LogTemp,Display,TEXT("MotionProbe content samples=%llu changes=%llu title=%.4f min=%.4f max=%.4f number=%.4f ring=%.4f"),ContentSamples,ContentDrops,TitleMean,TitleMin,TitleMax,NumberMean,RingMean);
   UE_LOG(LogTemp,Display,TEXT("MotionProbe GPU elapsed=%.2f frame=%llu submitted=%llu ready=%llu skipped=%llu mapFailed=%llu supported=%d format=%d baseline=%d inside=%d sibling=%d missingInside=%llu missingSibling=%llu maxLatencyMs=%.2f"),Now-Started,Frame,Submitted,Ready,Skipped,Failed,Supported,int32(Format),Baseline,LastInside,LastSibling,MissingInside,MissingSibling,MaxLatency);
  }
  if(Now-Started>=90.&&!Slots[0].Pending&&!Slots[1].Pending)
  {Done=true;UE_LOG(LogTemp,Display,TEXT("MotionProbe done ready=%llu baseline=%d missingInside=%llu missingSibling=%llu mapFailed=%llu"),Ready,Baseline,MissingInside,MissingSibling,Failed);}
 }
};

bool FMotionFlickerProbe::Enabled()
{static const bool Value=FParse::Param(FCommandLine::Get(),TEXT("MotionFlickerProbe"));return Value;}
void FMotionFlickerProbe::AddIndependentMarker()
{
 if(!Enabled()||!GEngine||!GEngine->GameViewport)return;
 GEngine->GameViewport->AddViewportWidgetContent(SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
  +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(40,8,0,0))
  [SNew(SBox).WidthOverride(16).HeightOverride(16)
   [SNew(SImage).Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).ColorAndOpacity(FLinearColor(0,1,1,1))]],101);
}
void FMotionFlickerProbe::PaintMarker(PondDashboard::FCanvas C)
{C.Layer+=400;C.Box(8,8,16,16,FLinearColor(1,0,1,1));}
FMotionFlickerProbe::FMotionFlickerProbe()
{
 if(!GEngine||!GEngine->GameViewport)return;
 const auto Window=GEngine->GameViewport->GetWindow();if(!Window)return;
 State=MakeShared<FMotionProbeRenderState,ESPMode::ThreadSafe>();const auto Shared=State;const SWindow* Target=Window.Get();
 Handle=FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddLambda([Shared,Target](SWindow& W,ISlateViewportProvider& Provider)
 {if(&W==Target)Shared->Read(Provider.GetBackBufferResource());});
}
FMotionFlickerProbe::~FMotionFlickerProbe()
{
 if(Handle.IsValid()&&FSlateApplication::IsInitialized())FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(Handle);
 // 注销后排空已有回调，再把GPU读回资源的最后引用交回渲染线程释放。仅关闭诊断时同步。-codex，2026-10-24
 FlushRenderingCommands();auto Shared=MoveTemp(State);
 ENQUEUE_RENDER_COMMAND(MotionProbeRelease)([Shared](FRHICommandListImmediate&){});
}
void FMotionFlickerProbe::Tick(uint64 PaintCount,bool Switching,float Veil,FVector2D Size,bool Volatile)
{
 const double Now=FPlatformTime::Seconds();if(Now-LastReport<1.)return;LastReport=Now;
 auto Get=[](const TCHAR* Name){const auto* V=IConsoleManager::Get().FindConsoleVariable(Name);return V?V->GetInt():-1;};
 UE_LOG(LogTemp,Display,TEXT("MotionProbe UI paintDelta=%llu total=%llu switching=%d veil=%.4f size=%.0fx%.0f volatile=%d globalInvalidation=%d vsync=%d maxFps=%d"),PaintCount-LastPaintCount,PaintCount,Switching,Veil,Size.X,Size.Y,Volatile,Get(TEXT("Slate.EnableGlobalInvalidation")),Get(TEXT("r.VSync")),Get(TEXT("t.MaxFPS")));LastPaintCount=PaintCount;
}
