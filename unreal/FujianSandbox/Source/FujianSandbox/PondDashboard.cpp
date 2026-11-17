// B屏原生经营阅读层：只读取同选择快照，按1920×1080等比排版，不持有另一套选择或经营状态。
// 月账以kg/分原整数绘图后再格式化；塘形仅示意，不能替代测绘或真实塘界。-codex，2026-10-21
#include "PondDashboard.h"
#include "PondPresentation.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/CompositeFont.h"
#include "Rendering/SlateRenderer.h"

namespace PondDashboard
{
    const FLinearColor Navy(.006f,.019f,.039f,1.f), Panel(.009f,.039f,.069f,1.f);
    const FLinearColor Cyan(.025f,.79f,.91f,1.f), White(.87f,.95f,1.f,1.f);
    const FLinearColor Muted(.36f,.57f,.70f,1.f), Grid(.028f,.15f,.23f,1.f);
    const FLinearColor Gold(1.f,.66f,.13f,1.f), Water(.012f,.20f,.31f,1.f);

    // 统一变换保证任何客户区保留完整逻辑画布；字号与线宽一起缩放。-codex，2026-10-21
    struct FCanvas
    {
        const FGeometry& Geometry;
        FSlateWindowElementList& Elements;
        int32 Layer;
        float Scale, Opacity, Shift;
        FVector2D Offset;
        FString FontPath;

        FCanvas(const FGeometry& G,FSlateWindowElementList& E,int32 L,float Reveal)
            :Geometry(G),Elements(E),Layer(L),Scale(FMath::Min(G.GetLocalSize().X/1920.f,G.GetLocalSize().Y/1080.f)),
             Opacity(.22f+.78f*Reveal),Shift(12.f*(1.f-Reveal)),
             Offset((G.GetLocalSize()-FVector2D(1920,1080)*Scale)*.5f),
             FontPath(FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf")){}

        FPaintGeometry At(float X,float Y,float W,float H) const
        {return Geometry.ToPaintGeometry(FVector2D(W,H),FSlateLayoutTransform(Scale,Offset+FVector2D(X,Y+Shift)*Scale));}
        FLinearColor Tint(FLinearColor Color) const {Color.A*=Opacity;return Color;}
        void Box(float X,float Y,float W,float H,FLinearColor Color) const
        {if(W>0&&H>0)FSlateDrawElement::MakeBox(Elements,Layer,At(X,Y,W,H),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Tint(Color));}
        void Lines(const TArray<FVector2D>& Points,FLinearColor Color,float Width=1.f) const
        {FSlateDrawElement::MakeLines(Elements,Layer,At(0,0,1920,1080),Points,ESlateDrawEffect::None,Tint(Color),true,Width);}
        void Line(float X,float Y,float X2,float Y2,FLinearColor Color,float Width=1.f) const
        {Lines({FVector2D(X,Y),FVector2D(X2,Y2)},Color,Width);}
        void Text(float X,float Y,const FString& Value,int32 Size,FLinearColor Color,float Width=0,int32 Align=0,bool FitValue=false) const
        {
            // UE5.6推荐复合字体；缓存同一文件的字体对象，字号仍按每处显式指定。-codex，2026-10-21
            static const TSharedPtr<const FCompositeFont> Composite=MakeShared<FCompositeFont>(NAME_None,FontPath,EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
            FSlateFontInfo Font(Composite,static_cast<float>(Size));
            const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
            FString Display=Value;
            // KPI数值必须完整：以真实字体宽度逐级缩字号，不走名称省略路径。-codex，2026-10-21
            if(FitValue&&Width>0)
            {
                while(Font.Size>1.f&&Measure->Measure(Display,Font).X>Width)Font.Size-=1.f;
                Y+=Size-Font.Size;
            }
            else if(Width>0)while(Display.Len()>1&&Measure->Measure(Display,Font).X>Width)
            {Display.LeftChopInline(Display.EndsWith(TEXT("…"))?2:1);Display+=TEXT("…");}
            const float Measured=Measure->Measure(Display,Font).X;
            if(Width>0)X+=Align==1?(Width-Measured)*.5f:Align==2?Width-Measured:0;
            FSlateDrawElement::MakeText(Elements,Layer+1,At(X,Y,FMath::Max(Measured,1.f),Font.Size*1.7f),Display,Font,ESlateDrawEffect::None,Tint(Color));
        }
        void Frame(float X,float Y,float W,float H,const FString& Title,const FString& Note=TEXT("")) const
        {
            Box(X,Y,W,H,Panel);
            Lines({{X+9,Y},{X+W-9,Y},{X+W,Y+9},{X+W,Y+H-9},{X+W-9,Y+H},{X+9,Y+H},{X,Y+H-9},{X,Y+9},{X+9,Y}},Grid);
            Line(X,Y+10,X,Y+30,Cyan,2);Line(X,Y+H-18,X,Y+H-6,Cyan,2);
            Line(X+14,Y+50,X+W-14,Y+50,Grid);
            Text(X+20,Y+11,Title,23,White,W-180);
            if(!Note.IsEmpty())Text(X+W-240,Y+17,Note,16,Muted,220,2);
        }
        void Metric(float X,const FString& Label,const FString& Value,const FString& Unit,bool Accent=false) const
        {
            Box(X,126,328,114,Panel);Line(X,126,X+328,126,Grid);Line(X,240,X+328,240,Grid);
            Line(X,142,X,170,Cyan,2);Text(X+22,140,Label,19,Muted,284);
            Text(X+22,176,Value,42,Accent?Gold:White,230,0,true);Text(X+255,198,Unit,18,Cyan,58,2);
        }
    };

    // 输入契约为非负整数；先商余四舍五入到百分单位，不加半除数，避免浮点尾位和加法溢出。-codex，2026-10-21
    FString FixedHundredths(int64 Raw,int64 Divisor)
    {
        const int64 Rounded=Raw/Divisor+(Raw%Divisor>=Divisor/2?1:0);
        return FString::Printf(TEXT("%lld.%02lld"),Rounded/100,Rounded%100);
    }
    FString Tons(int64 Kg){return FixedHundredths(Kg,10);}
    FString Value(int64 Cents){return FixedHundredths(Cents,10000);}

    // 柱高和峰值比较均使用原整数；刻度只负责阅读，不把舍入后的文字反算为数据。-codex，2026-10-21
    void Months(const FCanvas& C,const FPondSnapshot& S,float X,float Y,float W,float H)
    {
        C.Frame(X,Y,W,H,TEXT("2025 月度收获量"),TEXT("单位：吨"));
        if(S.Months.Num()!=12){C.Text(X+24,Y+90,TEXT("月度资料不完整"),22,Gold);return;}
        int64 Max=0;int32 Peak=0;
        for(int32 I=0;I<12;I++)if(S.Months[I].Harvest>Max){Max=S.Months[I].Harvest;Peak=I;}
        const double Upper=FMath::Max(1.0,FMath::CeilToDouble(static_cast<double>(Max)/1000.0/4.0)*4.0);
        const float Left=X+65,Top=Y+91,Bottom=Y+H-54,PlotH=Bottom-Top,Step=(W-90)/12.f;
        for(int32 T=0;T<=4;T++)
        {
            const float LineY=Bottom-PlotH*T/4.f;
            C.Line(Left,LineY,X+W-22,LineY,Grid);
            C.Text(X+8,LineY-12,FString::Printf(TEXT("%.0f"),Upper*T/4.0),16,Muted,44,2);
        }
        for(int32 I=0;I<12;I++)
        {
            const float Height=PlotH*static_cast<double>(S.Months[I].Harvest)/(Upper*1000.0);
            const float BX=Left+Step*I+Step*.19f,BW=Step*.58f;
            const FLinearColor Color=I==Peak?Gold:Cyan;
            C.Box(BX,Bottom-Height,BW,Height,Color*.72f);C.Box(BX,Bottom-Height,BW,3,Color);
            C.Box(BX,Bottom-Height,2,Height,Color);
            C.Text(Left+Step*I-3,Bottom-Height-28,Tons(S.Months[I].Harvest),16,I==Peak?Gold:White,Step+6,1);
            C.Text(Left+Step*I,Bottom+12,FString::Printf(TEXT("%02d"),I+1),17,White,Step,1);
        }
    }

    // 十二个月固定拆为两组六行，与上方柱图共用月账，不按已显示的小数求合计。-codex，2026-10-21
    void MonthlyTable(const FCanvas& C,const FPondSnapshot& S)
    {
        const float X=674,Y=674,W=1214,H=346;
        C.Frame(X,Y,W,H,TEXT("月度经营明细"),TEXT("2025 / 完整12月"));
        for(int32 Group=0;Group<2;Group++)
        {
            const float TX=X+18+Group*598,TW=580,RowH=36;
            C.Box(TX,Y+62,TW,36,FLinearColor(.013f,.083f,.13f,1));
            C.Text(TX,Y+68,TEXT("月份"),17,Muted,100,1);
            C.Text(TX+100,Y+68,TEXT("收获量（吨）"),17,Muted,218,1);
            C.Text(TX+318,Y+68,TEXT("模拟产值（万元）"),17,Muted,262,1);
            for(int32 I=0;I<6;I++)
            {
                const int32 Index=Group*6+I;const float RY=Y+98+I*RowH;
                if(I%2==0)C.Box(TX,RY,TW,RowH,FLinearColor(.008f,.047f,.080f,1));
                C.Line(TX,RY+RowH,TX+TW,RY+RowH,Grid);
                C.Text(TX,RY+6,FString::Printf(TEXT("%02d"),Index+1),19,White,100,1);
                if(S.Months.IsValidIndex(Index))
                {
                    C.Text(TX+100,RY+6,Tons(S.Months[Index].Harvest),19,White,218,1);
                    C.Text(TX+318,RY+6,Value(S.Months[Index].Value),19,White,262,1);
                }
            }
            C.Line(TX+100,Y+62,TX+100,Y+314,Grid);C.Line(TX+318,Y+62,TX+318,Y+314,Grid);
        }
    }

    // 轮廓由明确标注的程序示意构成；使用简单扫描线填充，避免生成图伪装真实塘界。-codex，2026-10-21
    void PondPlan(const FCanvas& C,const FPondSnapshot& S)
    {
        C.Frame(32,260,620,456,TEXT("鱼塘平面"),TEXT("示意 / 非测绘"));
        const TArray<FVector2D> Shape={{112,402},{162,365},{221,377},{247,399},{326,423},{393,418},{445,439},{470,468},{542,498},{565,554},{549,585},{512,594},{491,624},{421,637},{371,617},{329,581},{275,567},{228,531},{181,526},{154,492},{109,477},{112,402}};
        for(float Y=365;Y<638;Y+=2)
        {
            TArray<float> Cuts;
            for(int32 I=1;I<Shape.Num();I++)
            {
                const auto& A=Shape[I-1];const auto& B=Shape[I];
                if((A.Y<=Y&&B.Y>Y)||(B.Y<=Y&&A.Y>Y))Cuts.Add(A.X+(Y-A.Y)*(B.X-A.X)/(B.Y-A.Y));
            }
            Cuts.Sort();for(int32 I=1;I<Cuts.Num();I+=2)C.Box(Cuts[I-1],Y,Cuts[I]-Cuts[I-1],2,Water);
        }
        C.Lines(Shape,FLinearColor(.008f,.09f,.15f,1),16);C.Lines(Shape,Cyan,4);
        TArray<FVector2D> Inner;const FVector2D Center(336,500);
        for(const auto& P:Shape)Inner.Add(Center+(P-Center)*.966f);
        C.Lines(Inner,FLinearColor(.10f,.45f,.56f,1),1);
        C.Text(280,480,S.Species,30,White,180,1);C.Text(259,525,FString::FromInt(S.Area)+TEXT(" 亩"),21,Cyan,220,1);
        C.Text(566,332,TEXT("N"),18,White,32,1);C.Lines({{582,395},{582,360},{573,382},{582,376},{591,382},{582,360}},White,1.5f);
        C.Text(54,678,TEXT("平面示意，非测绘；不按比例"),17,Muted,570);
        C.Frame(32,736,620,284,TEXT("养殖资料"));
        const TArray<TPair<FString,FString>> Rows={{TEXT("鱼塘编号"),S.Id},{TEXT("鱼塘类型"),S.Kind},{TEXT("养殖品种"),S.Species},{TEXT("存量时点"),TEXT("2025-12-31")}};
        for(int32 I=0;I<Rows.Num();I++)
        {
            const float RY=803+I*48;C.Text(56,RY,Rows[I].Key,19,Muted,155);C.Text(222,RY,Rows[I].Value,20,White,396);
            if(I<Rows.Num()-1)C.Line(54,RY+36,630,RY+36,Grid);
        }
    }

    // UE坐标X北/Y东，先统一为横东/纵北再求边界与等比尺度；不新增底图或行政边界。-codex，2026-10-21
    void Distribution(const FCanvas& C,const FPondSnapshot& S)
    {
        C.Frame(32,260,590,400,TEXT("养殖点位分布"),FString::Printf(TEXT("%d 个点位"),S.Dots.Num()));
        const float X=70,Y=336,W=508,H=263;
        for(int32 I=0;I<6;I++){C.Line(X+W*I/5,Y,X+W*I/5,Y+H,Grid);C.Line(X,Y+H*I/5,X+W,Y+H*I/5,Grid);}
        if(S.Dots.Num()>0)
        {
            auto EastNorth=[](const FVector2D& P){return FVector2D(P.Y,P.X);};
            FVector2D Min=EastNorth(S.Dots[0].Position),Max=Min;
            for(const auto& D:S.Dots){const FVector2D P=EastNorth(D.Position);Min.X=FMath::Min(Min.X,P.X);Min.Y=FMath::Min(Min.Y,P.Y);Max.X=FMath::Max(Max.X,P.X);Max.Y=FMath::Max(Max.Y,P.Y);}
            const FVector2D Extent=Max-Min;const double Scale=FMath::Min((W-32)/FMath::Max(1.0,Extent.X),(H-32)/FMath::Max(1.0,Extent.Y));
            const FVector2D Center=(Max+Min)*.5;
            for(const auto& D:S.Dots)
            {
                const FVector2D P=EastNorth(D.Position);
                const float PX=X+W*.5+(P.X-Center.X)*Scale,PY=Y+H*.5-(P.Y-Center.Y)*Scale;
                C.Box(PX-5,PY-5,10,10,FLinearColor(.015f,.22f,.27f,1));C.Box(PX-2,PY-2,4,4,Cyan);
            }
        }
        C.Text(54,621,TEXT("原始点位坐标 · 同比例分布 · 非测绘底图"),16,Muted,546);
    }

    // 快照已按原产值排序；省态保留九市，市态显示九塘，绘制层不另排序。-codex，2026-10-21
    void Rankings(const FCanvas& C,const FPondSnapshot& S)
    {
        C.Frame(32,680,590,340,S.City==INDEX_NONE?TEXT("城市模拟产值排名"):TEXT("鱼塘模拟产值排名"),TEXT("单位：万元"));
        const int32 Count=FMath::Min(9,S.Ranking.Num());const int64 Max=Count?S.Ranking[0].Value:0;
        for(int32 I=0;I<Count;I++)
        {
            const auto& R=S.Ranking[I];const float Y=745+I*28;
            C.Text(50,Y,FString::Printf(TEXT("%02d"),I+1),16,I==0?Gold:Muted,30);
            C.Text(90,Y,R.Name,16,White,185);
            C.Box(283,Y+9,185,5,Grid);
            if(Max>0)C.Box(283,Y+9,185*static_cast<double>(R.Value)/Max,5,I==0?Gold:Cyan);
            C.Text(474,Y,Value(R.Value),16,White,126,2);
        }
    }

    // 品种条宽使用全年总收获量作分母；展示截取不会改变占比口径。-codex，2026-10-21
    void Varieties(const FCanvas& C,const FPondSnapshot& S)
    {
        C.Frame(644,680,460,340,TEXT("品种结构"),TEXT("按全年收获量"));
        const int32 Count=FMath::Min(5,S.Varieties.Num());
        for(int32 I=0;I<Count;I++)
        {
            const auto& V=S.Varieties[I];const float Y=747+I*45;
            C.Text(665,Y,V.Name,18,White,150);C.Text(813,Y,Tons(V.Value)+TEXT(" 吨"),17,Cyan,267,2);
            C.Box(665,Y+31,415,4,Grid);
            if(S.Harvest>0)C.Box(665,Y+31,415*static_cast<double>(V.Value)/S.Harvest,4,I==0?Gold:Cyan);
        }
        C.Text(665,987,S.Varieties.Num()>5?TEXT("显示收获量前5品种；比例按全部品种"):TEXT("占比依据原整数kg计算"),15,Muted,415);
    }

    // 明细明确标注前八，避免固定阅读屏的容量限制被误认为全量清单。-codex，2026-10-21
    void Details(const FCanvas& C,const FPondSnapshot& S)
    {
        C.Frame(1126,680,762,340,TEXT("鱼塘经营明细"),TEXT("模拟产值前8 / 万元"));
        C.Text(1147,743,TEXT("鱼塘名称"),17,Muted,310);C.Text(1463,743,TEXT("编号"),17,Muted,200);C.Text(1667,743,TEXT("年模拟产值"),17,Muted,199,2);
        for(int32 I=0;I<FMath::Min(8,S.Details.Num());I++)
        {
            const auto& R=S.Details[I];const float Y=776+I*28;
            if(I%2==0)C.Box(1143,Y-1,728,28,FLinearColor(.01f,.05f,.08f,1));
            C.Text(1147,Y,R.Name,16,White,310);C.Text(1463,Y,R.Id,16,Muted,200);C.Text(1667,Y,Value(R.Value),17,I==0?Gold:White,199,2);
        }
    }
}

// 同一帧只锁定并读取一个快照，B屏不反向改变A屏选择；资料坏账明确不可用。-codex，2026-10-21
int32 SPondDashboard::OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle&,bool) const
{
    using namespace PondDashboard;
    FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Navy);
    const auto Model=Presentation.Pin();
    PondDashboard::FCanvas C(Geometry,Elements,LayerId+1,Model.IsValid()?Model->Reveal():1.f);
    if(C.Scale<=0)return LayerId;
    C.Text(32,29,TEXT("FUJIAN  /  AQUACULTURE"),17,Cyan,440);
    C.Text(480,17,TEXT("福建 · 山海养殖"),35,White,960,1);
    C.Text(1420,29,TEXT("2025演示年度  ·  模拟经营数据"),18,Cyan,468,2);
    C.Line(32,82,1888,82,Grid);C.Line(738,84,1182,84,Cyan,2);
    C.Text(570,92,TEXT("经营数据屏 B  /  同选择快照"),17,Muted,780,1);
    if(!Model.IsValid()||!Model->Snapshot().Error.IsEmpty())
    {
        C.Frame(240,330,1440,360,TEXT("经营资料不可用"));
        C.Text(280,430,Model.IsValid()?Model->Snapshot().Error:TEXT("展示连接已关闭，请在主屏重新打开数据屏。"),26,Gold,1360);
        C.Text(280,515,TEXT("当前不展示指标与图表，避免将缺失资料误读为零。"),23,Muted,1360);
        return LayerId+3;
    }
    const FPondSnapshot& S=Model->Snapshot();
    const bool bPond=S.Site!=INDEX_NONE;
    C.Box(32,126,480,114,Panel);C.Line(32,126,512,126,Cyan);C.Line(32,240,512,240,Grid);
    C.Text(53,140,S.Title,29,White,438);C.Text(53,189,S.Id,22,Cyan,438);
    if(bPond)
    {
        C.Metric(528,TEXT("养殖面积"),FString::FromInt(S.Area),TEXT("亩"));
        C.Metric(872,TEXT("全年收获量"),Tons(S.Harvest),TEXT("吨"));
        C.Metric(1216,TEXT("全年模拟产值"),Value(S.Value),TEXT("万元"),true);
        C.Metric(1560,TEXT("2025-12-31存量"),FString::Printf(TEXT("%.1f"),static_cast<double>(S.Stock)/10000.0),TEXT("万尾"));
        PondPlan(C,S);Months(C,S,674,260,1214,392);MonthlyTable(C,S);
    }
    else
    {
        C.Metric(528,TEXT("养殖塘数"),FString::FromInt(S.Count),TEXT("塘"));
        C.Metric(872,TEXT("养殖总面积"),FString::FromInt(S.Area),TEXT("亩"));
        C.Metric(1216,TEXT("全年收获量"),Tons(S.Harvest),TEXT("吨"));
        C.Metric(1560,TEXT("全年模拟产值"),Value(S.Value),TEXT("万元"),true);
        Distribution(C,S);Months(C,S,644,260,1244,400);Rankings(C,S);Varieties(C,S);Details(C,S);
    }
    C.Text(32,1037,TEXT("产值=收获量×演示估值单价，非收入或利润  |  存量为2025-12-31时点快照，不与全年收获量相加  |  合计按未舍入值计算"),16,Muted,1620);
    C.Text(1665,1040,TEXT("本地模拟资料"),15,Cyan,223,2);
    return LayerId+3;
}
