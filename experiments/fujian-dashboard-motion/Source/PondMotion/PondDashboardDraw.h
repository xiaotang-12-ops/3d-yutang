// 经营星盘的逻辑画布：原生Slate图元、字体与低成本边缘光；不持有业务状态。-codex，2026-10-22
#pragma once
#include "CoreMinimal.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

namespace PondDashboard
{
FLinearColor RGB(uint8 R,uint8 G,uint8 B,uint8 A=255);
extern const FLinearColor Navy,Cyan,White,Muted,Grid,Gold;
float Ease(float T);
float Phase(float Time,float Start,float Duration);
FString Tons(int64 Kg);
FString Value(int64 Cents);
// 上两项仅保留旧绘图工具兼容签名；真实调查页不调用，不代表调查含产量或金额。-codex，2026-10-28

struct FCanvas
{
    const FGeometry& Geometry;
    FSlateWindowElementList& Elements;
    // 组内填面为Layer，轮廓为Layer+1，文字为Layer+4；组间由调用者明确分层。-codex，2026-10-22
    int32 Layer;
    float Scale,Opacity=1.f,Zoom=1.f;
    FVector2D Offset,Shift=FVector2D::ZeroVector;
    const FSlateBrush* GlassBrush=nullptr;
    bool bAtlasTopOnly=false;
    const FSlateBrush* StageBrush=nullptr;
    const FSlateBrush* Chart3DBrush=nullptr;
    bool bChartRGBBackground=false;
    uint32 Chart3DRevision=0;
    FVector4f Chart3DRect=FVector4f(0,0,0,0);
    FCanvas(const FGeometry& G,FSlateWindowElementList& E,int32 L);
    FVector2D Point(FVector2D P) const;
    FPaintGeometry At(float X,float Y,float W,float H) const;
    FLinearColor Tint(FLinearColor C) const;
    void Box(float X,float Y,float W,float H,FLinearColor Color) const;
    void Image(float X,float Y,float W,float H,const FSlateBrush& Brush) const;
    void Gradient(float X,float Y,float W,float H,FLinearColor Top,FLinearColor Bottom,int32 Steps=16) const;
    void RadialGlow(float X,float Y,float RX,float RY,FLinearColor Color) const;
    void Lines(const TArray<FVector2D>& Points,FLinearColor Color,float Width=1.f) const;
    void Line(float X,float Y,float X2,float Y2,FLinearColor Color,float Width=1.f) const;
    void Glow(const TArray<FVector2D>& Points,FLinearColor Color,float Width=1.5f) const;
    void Polygon(const TArray<FVector2D>& Points,FLinearColor Color) const;
    void Mesh(const TArray<FVector2D>& Points,const TArray<FLinearColor>& Colors,const TArray<SlateIndex>& Indices) const;
    void OpticalMesh(const TArray<FVector2D>& Points,const TArray<FVector4f>& Coordinates,const TArray<SlateIndex>& Indices,FLinearColor Color) const;
    void Quad(FVector2D A,FVector2D B,FVector2D C,FVector2D D,FLinearColor Color) const;
    void Ellipse(float X,float Y,float RX,float RY,FLinearColor Color,float Width=1.f,float Start=0,float End=2*PI) const;
    void Text(float X,float Y,const FString& Value,int32 Size,FLinearColor Color,float Width=0,int32 Align=0,bool Numeric=false) const;
    float WrappedText(float X,float Y,const FString& Value,int32 Size,FLinearColor Color,float Width,float LineHeight) const;
    void SpeciesIcon(float X,float Y,const FString& Species,float Size,FLinearColor Color) const;
    void Readout(float X,float Y,float Width,const FString& Value,int32 Size,const FString& Unit,int32 UnitSize,FLinearColor Color,int32 Align=0,float UnitOpacity=1.f) const;
    void Panel(float X,float Y,float W,float H,const FString& Title) const;
};
}
