// 原生B屏只绘制二维经营快照；所有数值来自PondPresentation，不嵌入网页截图。-codex，2026-10-21
#pragma once
#include "Widgets/SLeafWidget.h"
class FPondPresentation;
class SPondDashboard : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SPondDashboard){} SLATE_ARGUMENT(TSharedPtr<FPondPresentation>,Presentation) SLATE_END_ARGS()
    void Construct(const FArguments& Args){Presentation=Args._Presentation;}
    virtual FVector2D ComputeDesiredSize(float) const override {return FVector2D(1920,1080);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
private:
    TWeakPtr<FPondPresentation> Presentation;
};
