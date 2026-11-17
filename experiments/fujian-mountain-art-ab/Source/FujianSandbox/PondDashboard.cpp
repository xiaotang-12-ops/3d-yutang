// 原生B屏v7：共享选择快照、旧组合收拢和新星盘/两翼/趋势分阶段展开。
// 仅月份悬停是本地阅读状态；A选择、返回和镜头仍由控制器独占。-codex，2026-10-22
#include "PondDashboard.h"
#include "PondPresentation.h"
#include "PondDashboardCharts.h"
#include "PondChartCapture.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

// 构造异常清理也会实例化UniquePtr析构，必须与析构一样放在捕获类型完整的翻译单元。-codex，2026-10-22
SPondDashboard::SPondDashboard()=default;

void SPondDashboard::Construct(const FArguments& Args)
{
    Presentation=Args._Presentation;
    if(FParse::Param(FCommandLine::Get(),TEXT("PondStageExperiment")))
    {
        StageTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Dashboard/T_PondStageEnvironment.T_PondStageEnvironment"));
        if(StageTexture&&StageTexture->GetSizeX()>0&&StageTexture->GetSizeY()>0)
        {
            StageBrush.DrawAs=ESlateBrushDrawType::Image;
            // 编辑器异步纹理编译首帧可能返回32x32占位资源；布局使用受限导入脚本验证过的源尺寸契约。
            // 替换素材时须与导入器的尺寸检查同步，不能用临时GPU资源大小改变画面比例。-codex，2026-10-23
            StageBrush.ImageSize=FVector2D(2182,721);
            StageBrush.SetResourceObject(StageTexture);
            UE_LOG(LogTemp,Display,TEXT("PondStage candidate loaded: source contract 2182x721, current resource %dx%d; environment only, data remain procedural"),StageTexture->GetSizeX(),StageTexture->GetSizeY());
        }
        else
        {
            StageTexture=nullptr;
            UE_LOG(LogTemp,Warning,TEXT("PondStage texture unavailable; retaining procedural pedestal"));
        }
    }
    const bool bAtlasRequested=FParse::Param(FCommandLine::Get(),TEXT("PondAtlasExperiment"));
    // 候选共享一份Glass资源；同时指定不同实验会令对照失效，因此明确拒绝混用。-codex，2026-10-23
    if(bAtlasRequested&&(FParse::Param(FCommandLine::Get(),TEXT("PondGlassExperiment"))
        ||FParse::Param(FCommandLine::Get(),TEXT("PondChart3DExperiment"))))
    {UE_LOG(LogTemp,Warning,TEXT("PondAtlas conflicts with PondGlass/PondChart3D; retaining procedural ring"));return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("PondChartRGBExperiment")))
    {ChartCapture=MakeUnique<FPondChartCapture>(Args._CaptureWorld);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("PondChart3DExperiment")))
    {ChartCapture=MakeUnique<FPondChartCapture>(Args._CaptureWorld);return;}
    // 光学候选未通过视觉门槛，只有显式实验参数才加载；默认保留R2及其缺资产无警告行为。-codex，2026-10-22
    if(!bAtlasRequested&&!FParse::Param(FCommandLine::Get(),TEXT("PondGlassExperiment")))return;
    GlassMaterial=LoadObject<UMaterialInterface>(nullptr,bAtlasRequested
        ?TEXT("/Game/Dashboard/M_PondGlassAtlas.M_PondGlassAtlas")
        :TEXT("/Game/Dashboard/M_PondGlass.M_PondGlass"));
    if(!GlassMaterial||!GlassMaterial->IsUIMaterial()||GlassMaterial->GetBlendMode()!=BLEND_Translucent)
    {
        GlassMaterial=nullptr;
        UE_LOG(LogTemp,Warning,TEXT("%s candidate unavailable or incompatible; using procedural ring. Expected Dashboard material, UI/Translucent."),bAtlasRequested?TEXT("PondAtlas"):TEXT("PondGlass"));
        return;
    }
    GlassBrush.DrawAs=ESlateBrushDrawType::Image;GlassBrush.ImageSize=FVector2D(600,264);GlassBrush.SetResourceObject(GlassMaterial);
    bAtlasTopOnly=bAtlasRequested;
    if(bAtlasTopOnly)
    {
        UE_LOG(LogTemp,Display,TEXT("PondAtlas candidate loaded: purple TOP only; whole-circle angle/radius UV; procedural sides/cuts/rims retained; no clock"));
    }
    else
    {
        UE_LOG(LogTemp,Display,TEXT("PondGlass candidate ready: purple sector only; immutable UV/vertex-opacity material, no independent clock."));
    }
}
SPondDashboard::~SPondDashboard()
{
    ChartCapture.Reset();
    if(GlassMaterial&&FSlateApplication::IsInitialized())FSlateApplication::Get().GetRenderer()->ReleaseDynamicResource(GlassBrush);
    GlassBrush.SetResourceObject(nullptr);GlassMaterial=nullptr;
    if(StageTexture&&FSlateApplication::IsInitialized())FSlateApplication::Get().GetRenderer()->ReleaseDynamicResource(StageBrush);
    StageBrush.SetResourceObject(nullptr);StageTexture=nullptr;
}
void SPondDashboard::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
{
    SLeafWidget::Tick(Geometry,CurrentTime,DeltaTime);
    if(ChartCapture)if(const auto Model=Presentation.Pin())ChartCapture->Update(Model->Snapshot(),Model->TransitionTime());
}
void SPondDashboard::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(GlassMaterial);
    Collector.AddReferencedObject(StageTexture);
}

int32 SPondDashboard::OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle&,bool) const
{
    using namespace PondDashboard;
    FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Navy);
    PondDashboard::FCanvas C(Geometry,Elements,LayerId+1);if(C.Scale<=0)return LayerId;
    C.GlassBrush=GlassMaterial?&GlassBrush:nullptr;
    C.bAtlasTopOnly=bAtlasTopOnly;
    C.StageBrush=StageTexture?&StageBrush:nullptr;
    // 完整RGB必须占据旧Backdrop背景层，标题/数据仍由下方原生Slate单独绘制。-codex，2026-10-23
    const auto Model=Presentation.Pin();
    if(ChartCapture&&ChartCapture->IsRGB()&&Model.IsValid()&&Model->TransitionTime()>=4.f)
    {
        C.Chart3DBrush=ChartCapture->BrushFor(Model->Snapshot().Revision);
        C.Chart3DRevision=Model->Snapshot().Revision;C.Chart3DRect=ChartCapture->Rect();
        C.bChartRGBBackground=C.Chart3DBrush!=nullptr;
    }
    Backdrop(C,float(FMath::Fmod(FPlatformTime::Seconds(),10000.0)));
    // Slate会合批同层不同图元；业务面必须高于整个背景，不能仅依赖提交顺序。-codex，2026-10-22
    C.Layer+=10;
    if(!Model.IsValid()||!Model->Snapshot().Error.IsEmpty())
    {
        C.Panel(410,350,1100,270,TEXT("经营资料尚未同步"));
        C.Text(452,452,Model.IsValid()?Model->Snapshot().Error:TEXT("数据屏未绑定选择状态"),25,Gold,1016);
        C.Text(452,525,TEXT("暂不展示旧对象读数，请检查本地模拟资料。"),22,Muted,1016);
        return C.Layer+4;
    }
    const float T=Model->TransitionTime();const auto& S=Model->Snapshot();
    if(ChartCapture&&!ChartCapture->IsRGB()&&T>=4.f)
    {C.Chart3DBrush=ChartCapture->BrushFor(S.Revision);C.Chart3DRevision=S.Revision;C.Chart3DRect=ChartCapture->Rect();}
    // 各组合连同标题读数一起退出/进入；快速连续点击保留的是实际显现阶段，不补画未出现的两翼。
    // 空白收束区显示明确的目标名；不会以新标题标注旧快照。-codex，2026-10-22
    if(T<.38f&&Model->PreviousSnapshot().Revision>0&&Model->PreviousSnapshot().Error.IsEmpty())
    {
        PondDashboard::FCanvas Previous=C;const float Exit=Phase(T,0,.38f);Previous.Opacity=1-Exit;Previous.Zoom=1-.055f*Exit;
        Composition(Previous,Model->PreviousSnapshot(),Model->PreviousTime(),INDEX_NONE);
    }
    Composition(C,S,T,HoverMonth);
    C.Text(40,1048,T<3.4f?TEXT("正在切换至 ")+S.Title:TEXT("经营星盘 · 本地模拟资料"),18,Muted,650);
    C.Text(730,1048,TEXT("产值=收获量×演示估值单价 | 合计按未舍入值计算 | 单塘平面仅作示意"),17,Muted,1146,2);
    return C.Layer+4;
}
FReply SPondDashboard::OnMouseMove(const FGeometry& G,const FPointerEvent& Event)
{
    const float Scale=FMath::Min(G.GetLocalSize().X/1920.f,G.GetLocalSize().Y/1080.f);
    if(Scale<=0)return FReply::Unhandled();
    const auto P=(G.AbsoluteToLocal(Event.GetScreenSpacePosition())-(G.GetLocalSize()-FVector2D(1920,1080)*Scale)*.5)/Scale;
    const int32 Next=(P.X>=142&&P.X<1370&&P.Y>=730&&P.Y<=1035)?FMath::Clamp(int32((P.X-142)/(1228.f/12.f)),0,11):INDEX_NONE;
    if(Next!=HoverMonth){HoverMonth=Next;Invalidate(EInvalidateWidgetReason::Paint);}
    return FReply::Unhandled();
}
void SPondDashboard::OnMouseLeave(const FPointerEvent& Event)
{
    SLeafWidget::OnMouseLeave(Event);HoverMonth=INDEX_NONE;Invalidate(EInvalidateWidgetReason::Paint);
}
