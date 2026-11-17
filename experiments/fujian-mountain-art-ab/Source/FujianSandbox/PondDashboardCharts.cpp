// v7经营星盘：程序化弧段和透视承托层、按阶段展开的两翼与双趋势。
// 数值始终用台账终值；入场几何不是另一组业务数据，单塘轮廓明确为通用示意。-codex，2026-10-22
#include "PondDashboardCharts.h"

namespace PondDashboard
{
static FLinearColor SpeciesColor(int32 I)
{
    static const FLinearColor Colors[]={RGB(42,179,255),RGB(122,113,245),RGB(37,210,193),RGB(87,211,255)};
    return Colors[I%4];
}
static void Disc(const FCanvas& C,float X,float Y,float RX,float RY,FLinearColor Color)
{
    TArray<FVector2D> P;for(int32 I=0;I<96;I++){const float A=2*PI*I/96;P.Add({X+RX*FMath::Cos(A),Y+RY*FMath::Sin(A)});}C.Polygon(P,Color);
}
static float PlaneReflection(FVector2D Position,float CenterX,float CenterY,float Width)
{
    // 高光服从同一屏幕平面光向，不随环半径绕圈，避免把厚片误读成凹槽。-codex，2026-10-22
    return FMath::Exp(-FMath::Square(float(Position.X-CenterX+1.45f*(Position.Y-CenterY))/Width));
}
static void FineRim(const FCanvas& C,float X,float Y,float RX,float RY,float Begin,float End,float Width,FLinearColor Color,float HaloRadius=6.f)
{
    if(RX<=0||RY<=0||End<=Begin||Width<=0||HaloRadius<=Width)return;
    // 亮核与有界柔光共用横截面网格；横向、弧端均归零，不能用粗线叠加恢复矩形头。-codex，2026-10-22
    const int32 Count=FMath::Max(1,FMath::CeilToInt((End-Begin)*40));
    constexpr int32 AcrossCount=9;
    const float HalfCore=Width*.5f;
    const float Across[]={-HaloRadius,-HaloRadius*.55f,-HalfCore*1.8f,-HalfCore,0,HalfCore,HalfCore*1.8f,HaloRadius*.55f,HaloRadius};
    constexpr float Profile[]={0,.055f,.20f,.72f,1.f,.72f,.20f,.055f,0};
    const bool Closed=FMath::IsNearlyEqual(End-Begin,2*PI,.001f);
    TArray<FVector2D> RimPoints;TArray<FLinearColor> RimColors;TArray<SlateIndex> RimIndices;
    for(int32 RimStep=0;RimStep<=Count;RimStep++)
    {
        const float Angle=FMath::Lerp(Begin,End,float(RimStep)/Count),Cosine=FMath::Cos(Angle),Sine=FMath::Sin(Angle);
        const FVector2D Position(X+RX*Cosine,Y+RY*Sine),Normal=FVector2D(Cosine/RX,Sine/RY).GetSafeNormal();
        const float Speed=FMath::Sqrt(FMath::Square(RX*Sine)+FMath::Square(RY*Cosine));
        const float EndDistance=FMath::Min(Angle-Begin,End-Angle)*Speed;
        const float EndFade=Closed?1.f:FMath::SmoothStep(0.f,HaloRadius*1.5f,EndDistance);
        const float Energy=.45f+.35f*FMath::Square(.5f+.5f*FMath::Cos(Angle+2.2f))+.20f*Sine*Sine;
        for(int32 AcrossIndex=0;AcrossIndex<AcrossCount;AcrossIndex++)
        {
            auto Shade=FMath::Lerp(Color,RGB(206,246,255),.68f*Profile[AcrossIndex]);
            Shade.A=Color.A*Energy*Profile[AcrossIndex]*EndFade;
            RimPoints.Add(Position+Normal*Across[AcrossIndex]);RimColors.Add(Shade);
            if(RimStep<Count&&AcrossIndex<AcrossCount-1)
            {
                const SlateIndex RimIndex=RimStep*AcrossCount+AcrossIndex;
                RimIndices.Append({RimIndex,SlateIndex(RimIndex+AcrossCount),SlateIndex(RimIndex+AcrossCount+1),RimIndex,SlateIndex(RimIndex+AcrossCount+1),SlateIndex(RimIndex+1)});
            }
        }
    }
    C.Mesh(RimPoints,RimColors,RimIndices);
}
static void RevealLine(const FCanvas& C,const TArray<FVector2D>& P,float Progress,FLinearColor Color,float Width=1.f)
{
    if(P.Num()<2||Progress<=0)return;
    float Length=0;for(int32 I=1;I<P.Num();I++)Length+=FVector2D::Distance(P[I-1],P[I]);
    float Remaining=Length*FMath::Clamp(Progress,0.f,1.f);TArray<FVector2D> Visible;Visible.Add(P[0]);
    for(int32 I=1;I<P.Num()&&Remaining>0;I++)
    {
        const float Segment=FVector2D::Distance(P[I-1],P[I]);
        if(Segment<=KINDA_SMALL_NUMBER)continue;
        Visible.Add(FMath::Lerp(P[I-1],P[I],FMath::Min(Remaining/Segment,1.f)));Remaining-=Segment;
    }
    C.Lines(Visible,Color,Width);
}
void Backdrop(const FCanvas& Base,float Time)
{
    (void)Time;
    FCanvas C=Base;
    if(C.bChartRGBBackground&&C.Chart3DBrush)
        C.Image(0,0,1920,1080,*C.Chart3DBrush);
    else
    {
    C.Gradient(0,0,1920,1080,RGB(5,24,47),Navy,32);
    // 背景沿用Time接口但不消费时钟，避免停稳后仍有与业务无关的扫描动画。-codex，2026-10-22
    for(int32 I=0;I<16;I++)
    {
        const float X=960+(I-7.5f)*190;C.Line(960+(X-960)*.22f,450,X,1040,RGB(21,99,147,15));
    }
    for(int32 I=0;I<8;I++){const float Y=560+I*I*8.f;C.Line(50,Y,1870,Y,RGB(21,99,147,15));}
    C.Layer+=2;
    if(C.StageBrush)
    {
        // 只替换固定承托，保留PNG真实比例和透明边；业务环在Base+10单独绘制。-codex，2026-10-23
        FCanvas Stage=C;Stage.Layer++;
        const float StageWidth=1400.f;
        const float StageHeight=StageWidth*C.StageBrush->ImageSize.Y/C.StageBrush->ImageSize.X;
        Stage.Image(960-StageWidth*.5f,320,StageWidth,StageHeight,*C.StageBrush);
        // 压光只作用于前缘和标签净区，保留顶面、侧壁的层次，不整体淡化素材。-codex，2026-10-23
        Stage.Layer++;
        Stage.RadialGlow(960,648,870,64,RGB(2,13,27,175));
        Stage.RadialGlow(565,576,190,55,RGB(3,18,34,175));
        Stage.RadialGlow(1300,576,190,55,RGB(3,18,34,175));
    }
    else
    {
    Disc(C,960,604,553,103,RGB(1,9,23,250));
    // 前半椭圆为实体侧面，顶面独立分层；光向统一来自左后上方。-codex，2026-10-22
    TArray<FVector2D> Side;TArray<FLinearColor> Shades;TArray<SlateIndex> Indices;
    for(int32 I=0;I<=64;I++)
    {
        const float A=PI*I/64.f,X=960+538*FMath::Cos(A),Y=566+103*FMath::Sin(A);
        const float Light=.4f+.6f*FMath::Max(0.f,FMath::Cos(A+2.2f));
        Side.Append({{X,Y},{X,Y+32}});Shades.Append({FMath::Lerp(RGB(3,20,38),RGB(8,43,71),Light),RGB(1,9,21)});
        if(I<64){const SlateIndex V=I*2;Indices.Append({V,SlateIndex(V+2),SlateIndex(V+3),V,SlateIndex(V+3),SlateIndex(V+1)});}
    }
    C.Mesh(Side,Shades,Indices);
    FCanvas Top=C;Top.Layer++;
    TArray<FVector2D> DeckVertices;TArray<FLinearColor> DeckColors;TArray<SlateIndex> DeckIndices;
    auto DeckShade=[](FVector2D Position)
    {
        const float Front=FMath::Clamp(float((Position.Y-463)/206),0.f,1.f);
        auto DeckColor=FMath::Lerp(RGB(5,24,43),RGB(2,13,27),Front);
        const float Support=FMath::Exp(-FMath::Square(float((Position.X-960)/290))-FMath::Square(float((Position.Y-528)/43)));
        DeckColor=FMath::Lerp(DeckColor,RGB(12,65,106),.44f*Support);
        return DeckColor;
    };
    DeckVertices.Add({960,566});DeckColors.Add(DeckShade({960,566}));
    constexpr int32 DeckSegments=96,DeckRings=12;
    for(int32 DeckRing=1;DeckRing<=DeckRings;DeckRing++)
    {
        const float DeckRadius=float(DeckRing)/DeckRings;
        for(int32 DeckPoint=0;DeckPoint<DeckSegments;DeckPoint++)
        {
            const float DeckAngle=2*PI*DeckPoint/DeckSegments;
            const FVector2D DeckPosition(960+538*DeckRadius*FMath::Cos(DeckAngle),566+103*DeckRadius*FMath::Sin(DeckAngle));
            DeckVertices.Add(DeckPosition);DeckColors.Add(DeckShade(DeckPosition));
            const SlateIndex DeckCurrent=1+(DeckRing-1)*DeckSegments+DeckPoint,DeckNext=1+(DeckRing-1)*DeckSegments+(DeckPoint+1)%DeckSegments;
            if(DeckRing==1)DeckIndices.Append({0,DeckCurrent,DeckNext});
            else{const SlateIndex DeckPrevious=DeckCurrent-DeckSegments,DeckPreviousNext=DeckNext-DeckSegments;DeckIndices.Append({DeckPrevious,DeckCurrent,DeckNext,DeckPrevious,DeckNext,DeckPreviousNext});}
        }
    }
    Top.Mesh(DeckVertices,DeckColors,DeckIndices);
    // 顶面后提交、同层合批，确保光池不被实色台座盖住；主环层为Base+10，仍在其下。
    // 两个有限范围均低于孔中央且避开左右标签，不恢复整块蓝色承托板。-codex，2026-10-22
    Top.RadialGlow(960,606,245,16,RGB(19,139,222,100));
    Top.RadialGlow(960,654,180,9,RGB(30,174,249,112));
    FineRim(Top,960,566,538,103,-PI,PI,1.05f,RGB(54,151,202,130),3.f);
    FineRim(Top,960,568,535,102,0,PI,.85f,RGB(87,187,228,140),3.f);
    FineRim(C,960,598,538,103,0,PI,.8f,RGB(22,94,149,135),3.f);
    }
    }
    C.Line(34,79,1886,79,RGB(18,118,173));
    C.Glow({{34,79},{270,79}},RGB(57,200,255,150),1.4f);
    C.Text(38,22,TEXT("福建 · 山海养殖"),32,White,410);
    C.Text(450,36,TEXT("模拟经营数据 / 2025 演示年度"),20,Muted,660);
    C.Text(1380,36,TEXT("产值为演示估值，非收入或利润"),18,Muted,502,2);
}

static void Star(const FCanvas& C,const FPondSnapshot& S,float Grow)
{
    if(Grow<=0||S.Harvest<=0)return;
    const float CX=960,CY=432, RX=300,RY=132,Inner=.68f,Depth=24;
    FCanvas Contact=C;Contact.Opacity*=Grow;
    const bool RGBComplete=C.bChartRGBBackground&&C.Chart3DBrush&&C.Chart3DRevision==S.Revision&&Grow>=1.f;
    if(!RGBComplete)
    {
    Contact.RadialGlow(CX,CY+RY+Depth+3,RX+32,35,RGB(0,4,15,220));
    Contact.RadialGlow(CX-65,CY+RY+Depth+10,RX,19,RGB(20,143,225,62));
    float Start=-PI*.5f;
    for(int32 J=0;J<S.Varieties.Num();J++)
    {
        const auto& V=S.Varieties[J];if(V.Value<=0)continue;
        const float Span=2*PI*double(V.Value)/S.Harvest,Gap=FMath::Min(.025f,Span*.12f);
        const float A0=Start+Gap,A1=Start+FMath::Max(Gap,(Span-Gap)*Grow);
        const int32 Steps=FMath::Max(1,FMath::CeilToInt((A1-A0)*40));const auto Color=SpeciesColor(J);
        // 单RT只替换当前终态紫段；退场/快切继续原绘图，不把新对象图像画给旧快照。-codex，2026-10-22
        if(J==1&&Grow>=1.f&&C.Chart3DBrush&&C.Chart3DRevision==S.Revision)
        {
            C.Image(C.Chart3DRect.X,C.Chart3DRect.Y,C.Chart3DRect.Z,C.Chart3DRect.W,*C.Chart3DBrush);
            Start+=Span;continue;
        }
        const bool AtlasTopCandidate=C.GlassBrush&&C.bAtlasTopOnly&&J==1;
        const bool OpticalCandidate=C.GlassBrush&&!C.bAtlasTopOnly&&J%4==1;
        auto P=[&](float A,float R,float H=0.f){return FVector2D(CX+RX*R*FMath::Cos(A),CY+RY*R*FMath::Sin(A)+H);};
        auto OpticalUV=[](float Angle,float Radius,float Face,float Height)
        {return FVector4f(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),Face,Height);};
        TArray<FVector2D> TopVertices,SideVertices;TArray<FLinearColor> TopColors,SideColors;TArray<SlateIndex> TopIndices,SideIndices;
        TArray<FVector4f> TopUV,SideUV;
        // 闭合反射斑来自统一屏幕光向，边界值和斜率均归零；只取面内颜色，不创建无限斜条。
        // 补足二维采样后才叠加反射，避免大三角形把局部亮斑拉成整片灰白。-codex，2026-10-22
        const FVector2D ReflectionCenter0=P(-.65f,.84f),ReflectionCenter1=P(1.30f,.85f);
        auto ReflectionPatch=[](FVector2D Position,FVector2D Center,float Length,float Breadth)
        {
            const FVector2D Delta=Position-Center;
            const float Along=float(Delta.X*.8660254-Delta.Y*.5),Across=float(Delta.X*.5+Delta.Y*.8660254);
            const float Distance=FMath::Square(Along/Length)+FMath::Square(Across/Breadth);
            return FMath::Square(FMath::Max(0.f,1-Distance));
        };
        auto Surface=[&](float A,float U)
        {
            const float Direction=.5f+.5f*FMath::Cos(A+2.2f),Radius=FMath::Lerp(Inner,1.f,U);
            const float PlanarLight=.58f+.20f*Direction-.025f*Radius*FMath::Sin(A);
            auto Top=FMath::Lerp(Color*PlanarLight,RGB(160,215,239),.012f+.018f*Direction);
            const FVector2D SurfacePosition=P(A,Radius);
            const float Reflection=.62f*ReflectionPatch(SurfacePosition,ReflectionCenter0,54,20)+.50f*ReflectionPatch(SurfacePosition,ReflectionCenter1,42,18);
            Top=FMath::Lerp(Top,RGB(181,237,255),FMath::Min(Reflection,.68f));
            const float EdgeDistance=FMath::Min(U,1-U),Bevel=FMath::Square(FMath::Max(0.f,1-EdgeDistance/.025f));
            Top=FMath::Lerp(Top,FMath::Lerp(Color,RGB(174,225,245),.45f),.32f*Bevel*(.32f+.68f*Direction));
            Top.A=.88f+.05f*Bevel;return Top;
        };
        auto SideShade=[&](float A,float H)
        {
            const float Light=FMath::Max(0.f,FMath::Cos(A+2.2f));
            auto Shade=FMath::Lerp(Color*(.14f+.11f*Light+.19f*(1-H)),RGB(139,198,225),.014f*Light);
            Shade.A=.95f;return Shade;
        };
        for(int32 I=0;I<Steps;I++)
        {
            const float A=FMath::Lerp(A0,A1,float(I)/Steps),B=FMath::Lerp(A0,A1,float(I+1)/Steps);
            // 正面厚度只沿可见弧绘制；角度来自原kg占比，不让透视改变份额。-codex，2026-10-22
            const float WallRadius=FMath::Sin((A+B)*.5f)>0?1.f:Inner;
            for(int32 Band=0;Band<3;Band++)
            {
                const float U=Band/3.f,NextU=(Band+1)/3.f;const SlateIndex Index=SideVertices.Num();
                SideVertices.Append({P(A,WallRadius,Depth*U),P(B,WallRadius,Depth*U),P(B,WallRadius,Depth*NextU),P(A,WallRadius,Depth*NextU)});
                SideColors.Append({SideShade(A,U),SideShade(B,U),SideShade(B,NextU),SideShade(A,NextU)});
                if(OpticalCandidate)
                {
                    const float Face=WallRadius==1.f?1.f:2.f;
                    SideUV.Append({OpticalUV(A,WallRadius,Face,U),OpticalUV(B,WallRadius,Face,U),OpticalUV(B,WallRadius,Face,NextU),OpticalUV(A,WallRadius,Face,NextU)});
                }
                SideIndices.Append({Index,SlateIndex(Index+1),SlateIndex(Index+2),Index,SlateIndex(Index+2),SlateIndex(Index+3)});
            }
            // 面内约8逻辑单位一档，细倒角仍独立限宽；几何始终同一平顶，不以明暗模拟宽U槽。-codex，2026-10-22
            constexpr float SurfaceBands[]={0.f,.018f,.04f,.10f,.18f,.26f,.34f,.42f,.50f,.58f,.66f,.74f,.82f,.90f,.96f,.982f,1.f};
            for(int32 Band=0;Band<16;Band++)
            {
                const float U=SurfaceBands[Band],NextU=SurfaceBands[Band+1],R0=FMath::Lerp(Inner,1.f,U),R1=FMath::Lerp(Inner,1.f,NextU);
                const SlateIndex Index=TopVertices.Num();TopVertices.Append({P(A,R1),P(B,R1),P(B,R0),P(A,R0)});
                TopColors.Append({Surface(A,NextU),Surface(B,NextU),Surface(B,U),Surface(A,U)});
                if(OpticalCandidate||AtlasTopCandidate)TopUV.Append({OpticalUV(A,R1,0,0),OpticalUV(B,R1,0,0),OpticalUV(B,R0,0,0),OpticalUV(A,R0,0,0)});
                TopIndices.Append({Index,SlateIndex(Index+1),SlateIndex(Index+2),Index,SlateIndex(Index+2),SlateIndex(Index+3)});
            }
        }
        // 每品种顶面合为一批顶点，避免玻璃层次变成上千次独立绘制。-codex，2026-10-22
        if(OpticalCandidate)
        {
            // 只替换紫段的整块厚片（顶/内外壁/切面）；不把半透明盖板叠在原实色墙上。-codex，2026-10-22
            C.OpticalMesh(SideVertices,SideUV,SideIndices,Color);C.OpticalMesh(TopVertices,TopUV,TopIndices,Color);
        }
        else
        {
            C.Mesh(SideVertices,SideColors,SideIndices);
            // Atlas只替换顶面这一批，UV仍是全环平面坐标；不重映射每品种份额，保留程序切面和亮缘。-codex，2026-10-23
            if(AtlasTopCandidate)C.OpticalMesh(TopVertices,TopUV,TopIndices,Color);
            else C.Mesh(TopVertices,TopColors,TopIndices);
        }
        if(!OpticalCandidate)
        {
            auto Edge=FMath::Lerp(Color,RGB(166,223,244),.34f);Edge.A=.94f;
            FineRim(C,CX,CY,RX,RY,A0,A1,1.05f,Edge);
            Edge.A=.82f;FineRim(C,CX,CY,RX*Inner,RY*Inner,A0,A1,.75f,Edge);
        }
        // 起点外法线为负切向、终点为正切向；只画朝观察者的面，否则背面会盖住平顶形成假槽壁。-codex，2026-10-22
        for(int32 CutIndex=0;CutIndex<2;CutIndex++)
        {
            const float CutAngle=CutIndex==0?A0:A1;
            const bool Visible=CutIndex==0?FMath::Cos(CutAngle)<0:FMath::Cos(CutAngle)>0;if(!Visible)continue;
            if(OpticalCandidate)
            {
                const float CutFace=CutIndex==0?3.f:4.f;
                C.OpticalMesh({P(CutAngle,Inner),P(CutAngle,1),P(CutAngle,1,Depth),P(CutAngle,Inner,Depth)},
                    {OpticalUV(CutAngle,Inner,CutFace,0),OpticalUV(CutAngle,1,CutFace,0),OpticalUV(CutAngle,1,CutFace,1),OpticalUV(CutAngle,Inner,CutFace,1)},
                    {0,1,2,0,2,3},Color);
            }
            else C.Mesh({P(CutAngle,Inner),P(CutAngle,1),P(CutAngle,1,Depth),P(CutAngle,Inner,Depth)},
                    {Surface(CutAngle,0),Surface(CutAngle,1),SideShade(CutAngle,1),SideShade(CutAngle,1)},{0,1,2,0,2,3});
        }
        Start+=Span;
    }
    }
    // 名称和读数放在稳定正面区域；连接点才落到透视环上，避免绕环字难读。-codex，2026-10-22
    if(Grow>.92f)
    {
        const FVector2D Positions[]={{1190,278},{1190,517},{446,517},{446,278}};
        float Acc=-PI*.5f;
        for(int32 J=0;J<FMath::Min(4,S.Varieties.Num());J++)
        {
            const auto& V=S.Varieties[J];const auto Pos=Positions[J];const float Mid=Acc+PI*double(V.Value)/S.Harvest;Acc+=2*PI*double(V.Value)/S.Harvest;
            const bool Right=Pos.X>960;const float End=Right?Pos.X:Pos.X+235;
            const FVector2D Anchor(CX+RX*FMath::Cos(Mid),CY+RY*FMath::Sin(Mid));
            const float Reveal=Phase(Grow,.92f,.08f);RevealLine(C,{Anchor,{Right?1170.f:750.f,Pos.Y+67},{End,Pos.Y+67}},Reveal,SpeciesColor(J),1.2f);
            FCanvas Label=C;Label.Opacity*=Reveal;
            Label.Text(Pos.X,Pos.Y,V.Name,25,White,235,Right?0:2);
            Label.Text(Pos.X,Pos.Y+34,Tons(V.Value),32,White,170,Right?0:2,true);
            Label.Text(Pos.X+177,Pos.Y+43,TEXT("吨"),18,Muted,45);
        }
    }
}

static void Pond(const FCanvas& C,const FPondSnapshot& S,float Grow)
{
    if(Grow<=0)return;
    const FVector2D Center(960,467);FCanvas Shape=C;Shape.Opacity*=Grow;
    constexpr int32 Segments=96,Rings=16;
    auto Boundary=[&](float A)
    {
        const float R=1+.06f*FMath::Sin(3*A)+.035f*FMath::Cos(7*A);
        // 平面锚点和透视保持不动，只把承托椭圆变成通用塘形；不是缩放后再上移。-codex，2026-10-22
        return Center+FMath::Lerp(FVector2D(300*FMath::Cos(A),132*FMath::Sin(A)),FVector2D(345*R*FMath::Cos(A),137*R*FMath::Sin(A)),Grow);
    };
    Shape.RadialGlow(960,614,355,23,RGB(0,5,16,205));
    TArray<FVector2D> Side,Vertices,Outline;TArray<FLinearColor> SideColors,Colors;TArray<SlateIndex> SideIndices,Indices;
    for(int32 I=0;I<=Segments/2;I++)
    {
        const float A=PI*I/(Segments/2);const auto P=Boundary(A);Side.Append({P,P+FVector2D(0,13)});
        SideColors.Append({RGB(16,109,151,240),RGB(2,26,47,250)});
        if(I<Segments/2){const SlateIndex V=2*I;SideIndices.Append({V,SlateIndex(V+2),SlateIndex(V+3),V,SlateIndex(V+3),SlateIndex(V+1)});}
    }
    Shape.Mesh(Side,SideColors,SideIndices);
    auto PondShade=[](FVector2D Position)
    {
        const float Front=FMath::Clamp(float((Position.Y-330)/274),0.f,1.f);
        auto PondColor=FMath::Lerp(RGB(20,118,158,250),RGB(3,36,65,250),Front);
        const float LocalReflection=.40f*PlaneReflection(Position,1000,467,22)*FMath::Exp(-FMath::Square(float((Position.Y-480)/115)));
        PondColor=FMath::Lerp(PondColor,RGB(113,204,227,250),LocalReflection);return PondColor;
    };
    Vertices.Add(Center);Colors.Add(PondShade(Center));
    for(int32 Ring=1;Ring<=Rings;Ring++)
    {
        const float U=float(Ring)/Rings;
        for(int32 I=0;I<Segments;I++)
        {
            const float A=2*PI*I/Segments;const auto SurfacePosition=Center+(Boundary(A)-Center)*U;
            Vertices.Add(SurfacePosition);Colors.Add(PondShade(SurfacePosition));
            const SlateIndex V=1+(Ring-1)*Segments+I,N=1+(Ring-1)*Segments+(I+1)%Segments;
            if(Ring==1)Indices.Append({0,V,N});else{const SlateIndex D=V-Segments,E=N-Segments;Indices.Append({D,V,N,D,N,E});}
        }
    }
    // 通用星形轮廓可以径向三角化；真实塘界若非星形必须另做多边形三角化。-codex，2026-10-22
    Shape.Mesh(Vertices,Colors,Indices);
    for(int32 I=0;I<=Segments;I++)Outline.Add(Boundary(2*PI*I/Segments));
    Shape.Lines(Outline,RGB(54,181,214,195),1.2f);
    TArray<FVector2D> Front;for(int32 I=0;I<=Segments/2;I++)Front.Add(Boundary(PI*I/(Segments/2))+FVector2D(0,2));
    Shape.Lines(Front,RGB(82,203,230,165),1.3f);
    C.Text(730,407,S.Species,28,White,460,1);C.Text(730,448,FString::FromInt(S.Area)+TEXT(" 亩"),38,Cyan,460,1);
    if(C.StageBrush)
    {
        // 字下保留完整暗度，仅在外围8px/22px渐隐，避免声明成为硬贴条。-codex，2026-10-23
        const float X[]={741,763,1157,1179},Y[]={614,622,644,652};
        TArray<FVector2D> Backing;TArray<FLinearColor> BackingColors;TArray<SlateIndex> BackingIndices;
        for(int32 Row=0;Row<4;Row++)for(int32 Col=0;Col<4;Col++)
        {
            Backing.Add({X[Col],Y[Row]});BackingColors.Add(RGB(3,19,35,(Row==0||Row==3||Col==0||Col==3)?0:232));
            if(Row<3&&Col<3){const SlateIndex V=Row*4+Col;BackingIndices.Append({V,SlateIndex(V+1),SlateIndex(V+5),V,SlateIndex(V+5),SlateIndex(V+4)});}
        }
        C.Mesh(Backing,BackingColors,BackingIndices);
    }
    C.Text(650,619,TEXT("通用平面示意 · 非测绘 / 不按比例"),19,Muted,620,1);
}

static void LeftWing(const FCanvas& C,const FPondSnapshot& S)
{
    C.Panel(68,220,310,375,TEXT(""));
    if(S.Site==INDEX_NONE)
    {
        C.Readout(96,241,254,FString::FromInt(S.Count),46,TEXT("塘"),22,Cyan);
        C.Line(96,321,350,321,Grid);
        C.Readout(96,337,254,FString::FromInt(S.Area),44,TEXT("亩"),22,Cyan);
    }
    else
    {
        C.Text(96,240,TEXT("养殖资料"),24,White,254);C.Text(96,290,S.Species,36,Cyan,254);
        C.Text(96,347,S.Kind,22,White,254);C.Text(96,389,FString::FromInt(S.Area)+TEXT(" 亩  /  ")+S.Id.Right(2)+TEXT(" 号塘"),22,Muted,254);
    }
    C.Text(96,442,TEXT("全年模拟产值"),22,White,254);
    C.Readout(96,493,254,Value(S.Value),40,TEXT("万元"),19,Gold);
}
static void RightWing(const FCanvas& C,const FPondSnapshot& S,float Reveal)
{
    const float X=1460,W=388;
    if(S.Site!=INDEX_NONE)
    {
        C.Panel(X,220,W,375,TEXT("单塘经营档案"));
        C.Text(X+22,298,TEXT("鱼塘编号"),21,Muted,150);C.Text(X+22,333,S.Id,27,White,344);
        C.Text(X+22,392,TEXT("2025-12-31 存量"),21,Muted,344);
        C.Text(X+22,430,FString::Printf(TEXT("%.1f"),double(S.Stock)/10000),48,Cyan,220,0,true);C.Text(X+244,457,TEXT("万尾"),22,White,100);
        C.Text(X+22,535,TEXT("时点存量与全年收获量分开统计"),18,Muted,344);return;
    }
    C.Panel(X,210,W,405,S.City==INDEX_NONE?TEXT("城市模拟产值 / 万元"):TEXT("鱼塘模拟产值 / 万元"));
    const int32 Count=FMath::Min(S.City==INDEX_NONE?9:5,S.Ranking.Num());
    const float RowH=Count>5?35.f:62.f;const int64 Max=Count?S.Ranking[0].Value:0;
    for(int32 I=0;I<Count;I++)
    {
        const float Local=Phase(Reveal,I*.035f,.68f);if(Local<=0)continue;
        FCanvas Row=C;Row.Opacity*=Local;
        const auto& R=S.Ranking[I];const float Y=281+I*RowH;const bool Province=S.City==INDEX_NONE;
        const FString Name=Province?R.Name:R.Id.Right(2)+TEXT("号");
        Row.Text(X+20,Y,Name,Province?20:25,White,Province?96:78);
        const float BX=X+123,BW=118,Height=Province?8:13;
        Row.Box(BX,Y+14,BW,Height,RGB(7,41,66));
        if(Max>0)
        {
            const float Width=BW*double(R.Value)/Max*Local;Row.Gradient(BX,Y+14,Width,Height,I==0?Gold:Cyan,I==0?RGB(181,115,37):RGB(3,93,174),6);
            Row.Line(BX,Y+14,BX+Width,Y+14,I==0?RGB(255,230,170):RGB(132,233,255),1);
        }
        Row.Text(X+252,Y,Value(R.Value),Province?22:26,I==0?Gold:Cyan,115,2,true);
    }
}

static void Trends(const FCanvas& C,const FPondSnapshot& S,float Grow,int32 Hover)
{
    const float X=70,W=1328,L=142,R=1370,Step=(R-L)/12.f;
    if(S.Months.Num()!=12){C.Text(X+20,761,TEXT("月度资料不完整"),25,Gold,W-40);return;}
    int64 MaxHarvest=0,MaxValue=0;for(const auto& M:S.Months){MaxHarvest=FMath::Max(MaxHarvest,M.Harvest);MaxValue=FMath::Max(MaxValue,M.Value);}
    const double UpperH=FMath::Max(1.0,FMath::CeilToDouble(double(MaxHarvest)/1000/4)*4);
    const double UpperV=FMath::Max(1.0,FMath::CeilToDouble(double(MaxValue)/1000000/4)*4);
    C.Text(X+20,698,TEXT("月度收获量 / 吨"),24,White,W-40);C.Text(X+20,880,TEXT("月度模拟产值 / 万元"),24,Gold,W-40);
    for(int32 Axis=0;Axis<2;Axis++)
    {
        const float Top=Axis==0?752:927,Bottom=Axis==0?841:1007;const double Upper=Axis==0?UpperH:UpperV;
        for(int32 I=0;I<=2;I++)
        {
            const float Y=Bottom-(Bottom-Top)*I/2;C.Line(L,Y,R,Y,Grid);
            C.Text(X+4,Y-12,FString::Printf(TEXT("%.0f"),Upper*I/2),17,Muted,56,2,true);
        }
        TArray<FVector2D> Curve,FullCurve;float TotalLength=0,Travelled=0;
        if(Axis==1)
        {
            for(int32 I=0;I<12;I++)FullCurve.Add({L+(I+.5f)*Step,Bottom-(Bottom-Top)*(double(S.Months[I].Value)/1000000)/Upper});
            for(int32 I=1;I<FullCurve.Num();I++)TotalLength+=FVector2D::Distance(FullCurve[I-1],FullCurve[I]);
        }
        for(int32 I=0;I<12;I++)
        {
            const float PX=L+(I+.5f)*Step;
            const double Amount=Axis==0?double(S.Months[I].Harvest)/1000:double(S.Months[I].Value)/1000000;
            const float Local=Phase(Grow,I*.025f,.725f),H=(Bottom-Top)*Amount/Upper;
            if(Axis==0)
            {
                const float BW=Step*.39f;C.Gradient(PX-BW/2,Bottom-H*Local,BW,H*Local,RGB(57,218,253),RGB(3,84,143),10);
                C.Line(PX-BW/2,Bottom-H*Local,PX+BW/2,Bottom-H*Local,Cyan,1.6f);
            }
            else if(Grow>0)
            {
                const auto P=FullCurve[I];
                if(I==0){Curve.Add(P);C.Ellipse(P.X,P.Y,4,4,Gold,2);}
                else
                {
                    const auto Q=FullCurve[I-1];const float Length=FVector2D::Distance(Q,P);
                    // 用弧长预算裁切当前线段，点和末端提示只有曲线到达后才显现；全部来自Grow，无残留缓存。-codex，2026-10-22
                    const float SegmentProgress=Grow>=1.f?1.f:FMath::Clamp((TotalLength*Grow-Travelled)/FMath::Max(Length,KINDA_SMALL_NUMBER),0.f,1.f);
                    if(SegmentProgress>0)
                    {
                        const auto Tip=FMath::Lerp(Q,P,SegmentProgress);Curve.Add(Tip);
                        C.Mesh({Q,Tip,{Tip.X,Bottom},{Q.X,Bottom}},
                            {RGB(203,143,40,38),RGB(203,143,40,38),RGB(203,143,40,0),RGB(203,143,40,0)},{0,1,2,0,2,3});
                        if(SegmentProgress>=1)C.Ellipse(P.X,P.Y,4,4,Gold,2);
                    }
                    Travelled+=Length;
                }
            }
            C.Text(L+I*Step,Bottom+7,FString::Printf(TEXT("%02d"),I+1),18,White,Step,1,true);
            if(I==Hover&&Grow>=1.f)
            {
                C.Line(PX,Top-7,PX,Bottom,RGB(132,216,250,145));
                C.Text(PX-70,Top-36,Axis==0?Tons(S.Months[I].Harvest):Value(S.Months[I].Value),22,Axis==0?Cyan:Gold,140,1,true);
            }
        }
        if(Curve.Num()>1)C.Glow(Curve,Gold,1.8f);
    }
    C.Text(1020,700,Hover==INDEX_NONE||Grow<1.f?TEXT("指向月份查看读数"):FString::Printf(TEXT("%02d 月 · 对应月账"),Hover+1),18,Muted,350,2);
}

static void Records(const FCanvas& C,const FPondSnapshot& S)
{
    C.Panel(1460,704,388,319,S.Site==INDEX_NONE?TEXT("重点对象 / 万元"):TEXT("本塘年度摘要"));
    if(S.Site!=INDEX_NONE)
    {
        C.Text(1482,788,TEXT("全年收获"),21,Muted,144);C.Readout(1620,782,200,Tons(S.Harvest),31,TEXT("吨"),18,Cyan,2);
        C.Text(1482,849,TEXT("养殖面积 / 亩"),21,Muted,185);C.Text(1668,845,FString::FromInt(S.Area),32,White,152,2,true);
        C.Text(1482,927,TEXT("月份完整 · 12 个月"),21,White,336);C.Text(1482,970,TEXT("收获吨数与产值使用同一台账"),18,Muted,336);return;
    }
    for(int32 I=0;I<FMath::Min(3,S.Ranking.Num());I++)
    {
        const auto& R=S.Ranking[I];const float Y=796+I*66;
        C.Text(1482,Y,S.City==INDEX_NONE?R.Name:R.Id.Right(2)+TEXT(" 号塘"),22,White,175);
        C.Text(1654,Y,Value(R.Value),28,Cyan,174,2,true);C.Line(1482,Y+53,1826,Y+53,Grid);
    }
    const FString Scope=S.City==INDEX_NONE?FString::Printf(TEXT("城市产值前3 · 共%d市"),S.Ranking.Num()):FString::Printf(TEXT("鱼塘产值前3 · 共%d塘"),S.Count);
    C.Text(1482,768,Scope,16,Muted,344);
}

void Composition(const FCanvas& Base,const FPondSnapshot& S,float T,int32 HoverMonth)
{
    FCanvas C=Base;const float Main=Phase(T,.38f,.8f);C.Opacity*=Main;C.Shift.Y+=22*(1-Main);
    C.Text(560,100,S.Title,38,White,800,1);
    C.Text(700,154,TEXT("全年收获量"),23,Muted,520,1);
    C.Readout(600,184,720,Tons(S.Harvest),61,TEXT("吨"),20,RGB(191,234,246),1,.58f);
    const float Chart=Phase(T,.6f,1.05f);
    if(S.Site==INDEX_NONE)Star(C,S,Chart);
    else
    {
        // 标题入场仍可轻移，但塘面继承舞台固定锚点，不能形变时又随标题上浮。-codex，2026-10-22
        FCanvas Plane=C;Plane.Shift.Y-=22*(1-Main);Pond(Plane,S,Chart);
    }
    const float JoinProgress=Phase(T,1.05f,.55f);
    if(JoinProgress>0)
    {
        FCanvas Join=Base;Join.Opacity*=.6f;
        // 从中心向两翼展开，连线先建立关系再显现资料；退出时仍受Base旧快照透明度约束。-codex，2026-10-22
        RevealLine(Join,{{650,605},{455,605},{378,540}},JoinProgress,Cyan);
        RevealLine(Join,{{1270,605},{1405,605},{1460,540}},JoinProgress,Cyan);
    }
    const float Wing=Phase(T,1.45f,.85f);FCanvas Left=Base;Left.Opacity*=Wing;Left.Shift.X+=110*(1-Wing);
    FCanvas Right=Base;Right.Opacity*=Wing;Right.Shift.X-=110*(1-Wing);LeftWing(Left,S);RightWing(Right,S,Phase(T,1.5f,.8f));
    const float Trend=Phase(T,2.15f,1.2f);FCanvas Lower=Base;Lower.Opacity*=Trend;Lower.Shift.Y+=32*(1-Trend);
    Trends(Lower,S,Trend,HoverMonth);Records(Lower,S);
}
}
