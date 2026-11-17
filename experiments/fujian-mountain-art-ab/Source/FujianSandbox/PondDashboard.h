// 原生B屏只绘制二维经营快照；所有数值来自PondPresentation，不嵌入网页截图。-codex，2026-10-21
#pragma once
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"
#include "UObject/GCObject.h"
class UMaterialInterface;
class UTexture2D;
class UWorld;
class FPondChartCapture;
class FPondPresentation;
class SPondDashboard : public SLeafWidget, public FGCObject
{
public:
    SLATE_BEGIN_ARGS(SPondDashboard){} SLATE_ARGUMENT(TSharedPtr<FPondPresentation>,Presentation) SLATE_ARGUMENT(UWorld*,CaptureWorld) SLATE_END_ARGS()
    SPondDashboard();
    void Construct(const FArguments& Args);
    virtual ~SPondDashboard() override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override {return TEXT("SPondDashboard.OpticalCandidate");}
    virtual void Tick(const FGeometry&,double,float) override;
    virtual FVector2D ComputeDesiredSize(float) const override {return FVector2D(1920,1080);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
    virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent&) override;
    virtual void OnMouseLeave(const FPointerEvent&) override;
private:
    TWeakPtr<FPondPresentation> Presentation;
    // Slate不是UObject；窗口存续期间显式保活材质，不在OnPaint创建MID或变更共享参数。-codex，2026-10-22
    TObjectPtr<UMaterialInterface> GlassMaterial=nullptr;
    FSlateBrush GlassBrush;
    bool bAtlasTopOnly=false;
    // 固定环境图不包含业务读数；显式候选启用，纹理生命周期随B窗口。-codex，2026-10-23
    TObjectPtr<UTexture2D> StageTexture=nullptr;
    FSlateBrush StageBrush;
    TUniquePtr<FPondChartCapture> ChartCapture;
    int32 HoverMonth=INDEX_NONE;
};
