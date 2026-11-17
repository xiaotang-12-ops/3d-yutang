// 将1920×1080逻辑坐标映射到窗口，图元和文字共用变换，兼容并排预览及DPI。
// 颜色按sRGB转换，字体仅复用引擎随包资源；不把目标PNG贴成数据界面。-codex，2026-10-22
#include "PondDashboardDraw.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/CompositeFont.h"
#include "Rendering/SlateRenderer.h"

namespace PondDashboard
{
FLinearColor RGB(uint8 R,uint8 G,uint8 B,uint8 A){return FLinearColor::FromSRGBColor(FColor(R,G,B,A));}
const FLinearColor Navy=RGB(2,11,26),Cyan=RGB(40,207,255),White=RGB(236,247,255);
const FLinearColor Muted=RGB(145,181,210),Grid=RGB(16,61,90),Gold=RGB(255,198,109);
float Ease(float T){T=FMath::Clamp(T,0.f,1.f);return 1.f-FMath::Pow(1.f-T,3.f);}
float Phase(float T,float Start,float Duration){return Ease((T-Start)/Duration);}
static FString Hundredths(int64 Raw,int64 Divisor)
{
    const int64 Rounded=Raw/Divisor+(Raw%Divisor>=Divisor/2?1:0);
    return FString::Printf(TEXT("%lld.%02lld"),Rounded/100,Rounded%100);
}
FString Tons(int64 Kg){return Hundredths(Kg,10);}
FString Value(int64 Cents){return Hundredths(Cents,10000);}
FCanvas::FCanvas(const FGeometry& G,FSlateWindowElementList& E,int32 L):Geometry(G),Elements(E),Layer(L)
{
    Scale=FMath::Min(G.GetLocalSize().X/1920.f,G.GetLocalSize().Y/1080.f);
    Offset=(G.GetLocalSize()-FVector2D(1920,1080)*Scale)*.5;
}
FVector2D FCanvas::Point(FVector2D P) const{return FVector2D(960,540)+(P-FVector2D(960,540))*Zoom+Shift;}
FPaintGeometry FCanvas::At(float X,float Y,float W,float H) const
{return Geometry.ToPaintGeometry(FVector2D(W,H),FSlateLayoutTransform(Scale*Zoom,Offset+Point({X,Y})*Scale));}
FLinearColor FCanvas::Tint(FLinearColor C) const{C.A*=Opacity;return C;}
void FCanvas::Box(float X,float Y,float W,float H,FLinearColor C) const
{if(W>0&&H>0&&Opacity>.001f)FSlateDrawElement::MakeBox(Elements,Layer,At(X,Y,W,H),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Tint(C));}
void FCanvas::Image(float X,float Y,float W,float H,const FSlateBrush& Brush) const
{if(W>0&&H>0&&Opacity>.001f)FSlateDrawElement::MakeBox(Elements,Layer,At(X,Y,W,H),&Brush,ESlateDrawEffect::None,Tint(FLinearColor::White));}
void FCanvas::Gradient(float X,float Y,float W,float H,FLinearColor Top,FLinearColor Bottom,int32 Steps) const
{
    // 保留Steps以兼容原调用；同一高度使用相同颜色，两三角形共边不产生梯度折角。-codex，2026-10-22
    (void)Steps;if(W<=0||H<=0)return;
    Mesh({{X,Y},{X+W,Y},{X+W,Y+H},{X,Y+H}},{Top,Top,Bottom,Bottom},{0,1,2,0,2,3});
}
void FCanvas::RadialGlow(float X,float Y,float RX,float RY,FLinearColor Color) const
{
    if(RX<=0||RY<=0||Color.A<=0)return;
    // 有限椭圆内衰减到透明，避免多层线条叠成条纹，也不把光铺满数据区域。-codex，2026-10-22
    constexpr int32 Segments=64,Rings=6;TArray<FVector2D> P;TArray<FLinearColor> Colors;TArray<SlateIndex> Indices;
    P.Add({X,Y});Colors.Add(Color);
    for(int32 Ring=1;Ring<=Rings;Ring++)
    {
        const float R=float(Ring)/Rings;auto Shade=Color;Shade.A*=FMath::Square(1-R);
        for(int32 I=0;I<Segments;I++){const float A=2*PI*I/Segments;P.Add({X+RX*R*FMath::Cos(A),Y+RY*R*FMath::Sin(A)});Colors.Add(Shade);}
        for(int32 I=0;I<Segments;I++)
        {
            const SlateIndex A=1+(Ring-1)*Segments+I,B=1+(Ring-1)*Segments+(I+1)%Segments;
            if(Ring==1)Indices.Append({0,A,B});
            else {const SlateIndex D=A-Segments,E=B-Segments;Indices.Append({D,A,B,D,B,E});}
        }
    }
    Mesh(P,Colors,Indices);
}
void FCanvas::Lines(const TArray<FVector2D>& P,FLinearColor C,float Width) const
{if(P.Num()>1&&Opacity>.001f)FSlateDrawElement::MakeLines(Elements,Layer+1,At(0,0,1920,1080),P,ESlateDrawEffect::None,Tint(C),true,Width);}
void FCanvas::Line(float X,float Y,float X2,float Y2,FLinearColor C,float Width) const{Lines({{X,Y},{X2,Y2}},C,Width);}
void FCanvas::Glow(const TArray<FVector2D>& P,FLinearColor C,float Width) const
{
    auto Halo=C;Halo.A*=.10f;Lines(P,Halo,Width+16);Halo.A=C.A*.24f;Lines(P,Halo,Width+6);Lines(P,C,Width);
}
void FCanvas::Polygon(const TArray<FVector2D>& P,FLinearColor C) const
{
    if(P.Num()<3||Opacity<=.001f)return;
    TArray<FLinearColor> Colors;Colors.Init(C,P.Num());TArray<SlateIndex> Indices;
    for(int32 I=1;I<P.Num()-1;I++){Indices.Add(0);Indices.Add(I);Indices.Add(I+1);}
    Mesh(P,Colors,Indices);
}
void FCanvas::Mesh(const TArray<FVector2D>& P,const TArray<FLinearColor>& Colors,const TArray<SlateIndex>& Indices) const
{
    if(P.Num()!=Colors.Num()||Indices.IsEmpty()||Opacity<=.001f)return;
    TArray<FSlateVertex> V;V.Reserve(P.Num());
    for(int32 I=0;I<P.Num();I++)
    {
        const FVector2f Absolute(Geometry.LocalToAbsolute(Offset+Point(P[I])*Scale));
        V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(FSlateRenderTransform(),Absolute,FVector2f(.5f,.5f),Tint(Colors[I]).ToFColor(true)));
    }
    const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
    FSlateDrawElement::MakeCustomVerts(Elements,Layer,Resource,V,Indices,nullptr,0,0);
}
void FCanvas::OpticalMesh(const TArray<FVector2D>& P,const TArray<FVector4f>& Coordinates,const TArray<SlateIndex>& Indices,FLinearColor Color) const
{
    if(!GlassBrush||P.Num()!=Coordinates.Num()||Indices.IsEmpty()||Opacity<=.001f)return;
    TArray<FSlateVertex> OpticalVertices;OpticalVertices.Reserve(P.Num());
    for(int32 VertexIndex=0;VertexIndex<P.Num();VertexIndex++)
    {
        const FVector2f Absolute(Geometry.LocalToAbsolute(Offset+Point(P[VertexIndex])*Scale));
        // Slate自定义材质直接输出颜色，不自动乘顶点色；材质显式读取顶点RGBA且只乘一次快照透明度。
        // ST_Custom直接映射UV0=xy平面位置、UV1=zw面类型/厚度，不走普通Slate元素的UV重排。-codex，2026-10-22
        OpticalVertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(FSlateRenderTransform(),Absolute,Coordinates[VertexIndex],FVector2f::ZeroVector,Tint(Color).ToFColor(true)));
    }
    const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*GlassBrush);
    FSlateDrawElement::MakeCustomVerts(Elements,Layer,Resource,OpticalVertices,Indices,nullptr,0,0);
}
void FCanvas::Readout(float X,float Y,float W,const FString& Value,int32 Size,const FString& Unit,int32 UnitSize,FLinearColor Color,int32 Align,float UnitOpacity) const
{
    static const auto NumericFont=MakeShared<FCompositeFont>(NAME_None,FPaths::EngineContentDir()/TEXT("Slate/Fonts/Roboto-Bold.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    static const auto UnitFont=MakeShared<FCompositeFont>(NAME_None,FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    FSlateFontInfo Number(NumericFont,float(Size)),Suffix(UnitFont,float(UnitSize));
    const float UnitWidth=Measure->Measure(Unit,Suffix).X;
    while(Number.Size>18&&Measure->Measure(Value,Number).X+UnitWidth+12>W)Number.Size--;
    const float NumberWidth=Measure->Measure(Value,Number).X,Total=NumberWidth+12+UnitWidth;
    const float Start=X+(Align==1?(W-Total)*.5f:Align==2?W-Total:0);
    Text(Start,Y,Value,int32(Number.Size),Color,NumberWidth+1,0,true);
    auto UnitColor=Color;UnitColor.A*=FMath::Clamp(UnitOpacity,0.f,1.f);
    Text(Start+NumberWidth+12,Y+(Number.Size-UnitSize)*.95f,Unit,UnitSize,UnitColor,UnitWidth+2);
}
void FCanvas::Quad(FVector2D A,FVector2D B,FVector2D C,FVector2D D,FLinearColor Color) const{Polygon({A,B,C,D},Color);}
void FCanvas::Ellipse(float X,float Y,float RX,float RY,FLinearColor C,float Width,float Start,float End) const
{
    TArray<FVector2D> P;const int32 N=FMath::Max(2,FMath::CeilToInt(FMath::Abs(End-Start)*18));P.Reserve(N+1);
    for(int32 I=0;I<=N;I++){const float A=FMath::Lerp(Start,End,float(I)/N);P.Add({X+RX*FMath::Cos(A),Y+RY*FMath::Sin(A)});}Lines(P,C,Width);
}
void FCanvas::Text(float X,float Y,const FString& S,int32 Size,FLinearColor C,float W,int32 Align,bool Numeric) const
{
    if(Opacity<.01f||S.IsEmpty())return;
    static const auto Chinese=MakeShared<FCompositeFont>(NAME_None,FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    static const auto Numbers=MakeShared<FCompositeFont>(NAME_None,FPaths::EngineContentDir()/TEXT("Slate/Fonts/Roboto-Bold.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    FSlateFontInfo Font(Numeric?Numbers:Chinese,float(Size));FString Display=S;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    if(Numeric&&W>0){while(Font.Size>18&&Measure->Measure(Display,Font).X>W)Font.Size-=1;}
    else if(W>0)while(Display.Len()>1&&Measure->Measure(Display,Font).X>W){Display.LeftChopInline(Display.EndsWith(TEXT("…"))?2:1);Display+=TEXT("…");}
    const float Measured=Measure->Measure(Display,Font).X;
    if(W>0)X+=Align==1?(W-Measured)*.5f:Align==2?W-Measured:0;
    FSlateDrawElement::MakeText(Elements,Layer+4,At(X,Y,FMath::Max(Measured,1.f),Font.Size*1.7f),Display,Font,ESlateDrawEffect::None,Tint(C));
}
void FCanvas::Panel(float X,float Y,float W,float H,const FString& Title) const
{
    Gradient(X,Y,W,H,RGB(3,31,55,225),RGB(2,17,34,180));
    Lines({{X,Y+20},{X,Y+8},{X+8,Y},{X+W-12,Y},{X+W,Y+12}},RGB(23,134,177));
    Line(X,Y+H,X+W,Y+H,Grid);Line(X+2,Y+1,X+36,Y+1,Cyan,2);
    if(!Title.IsEmpty()){Text(X+20,Y+17,Title,23,White,W-40);Line(X+20,Y+58,X+W-20,Y+58,Grid);}
}
}
