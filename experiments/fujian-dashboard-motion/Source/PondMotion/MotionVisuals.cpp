// 新批准稿的2D塘体与独立文字层；图像仅为主体示意，不编码真实塘界。-codex，2026-11-03
#include "MotionDashboard.h"
#include "MotionPondSprites.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
using namespace PondDashboard;
namespace
{
const auto Aqua=RGB(133,221,227),Sand=RGB(245,215,148),Soft=RGB(164,197,202),Ink=RGB(231,245,244);
TSharedRef<FCompositeFont> DisplayFont(bool Numeric)
{
 // 使用本机已安装字体，不复制Windows字体。其他系统缺失时退回引擎随包字体。-codex，2026-11-03
 static const auto Title=MakeShared<FCompositeFont>(NAME_None,[](){const FString P=FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")),TEXT("Fonts/simkai.ttf"));return FPaths::FileExists(P)?P:FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf");}(),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
 static const auto Number=MakeShared<FCompositeFont>(NAME_None,[](){const FString P=FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")),TEXT("Fonts/georgiai.ttf"));return FPaths::FileExists(P)?P:FPaths::EngineContentDir()/TEXT("Slate/Fonts/Roboto-BoldItalic.ttf");}(),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
 return Numeric?Number:Title;
}
void Accent(const PondDashboard::FCanvas& C,float X,float Y,const FString& S,int Size,FLinearColor Color,float W=0,bool Numeric=false,int Align=0)
{
 if(S.IsEmpty()||C.Opacity<.005f)return;
 auto Font=FSlateFontInfo(DisplayFont(Numeric),float(Size));const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
 while(W>0&&Font.Size>14&&Measure->Measure(S,Font).X>W)--Font.Size;
 const float Width=Measure->Measure(S,Font).X;
 if(W>0)X+=(Align==1?(W-Width)*.5f:Align==2?W-Width:0);
 FSlateDrawElement::MakeText(C.Elements,C.Layer+4,C.At(X,Y,FMath::Max(Width,1.f),Font.Size*1.7f),S,Font,ESlateDrawEffect::None,C.Tint(Color));
}
void Number(const PondDashboard::FCanvas& C,float X,float Y,float W,const FString& S,int Size,const FString& Unit,int UnitSize)
{
 auto Font=FSlateFontInfo(DisplayFont(true),float(Size));const auto M=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
 const float UW=Unit.Len()*UnitSize;
 while(Font.Size>18&&M->Measure(S,Font).X+UW+10>W)--Font.Size;
 const float NW=M->Measure(S,Font).X;Accent(C,X,Y,S,int(Font.Size),Sand,NW+1,true);
 Accent(C,X+NW+10,Y+(Font.Size-UnitSize)*.87f,Unit,UnitSize,Sand,UW+5);
}
TArray<FVector2D> Rounded(float X,float Y,float W,float H,float Radius)
{
 TArray<FVector2D> P;const FVector2D Corners[]={{X+Radius,Y+Radius},{X+W-Radius,Y+Radius},{X+W-Radius,Y+H-Radius},{X+Radius,Y+H-Radius}};
 for(int K=0;K<4;++K)for(int I=0;I<=10;++I){const float A=PI+K*PI*.5f+I*PI/20.f;P.Add(Corners[K]+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);}return P;
}
void Glass(PondDashboard::FCanvas C,float X,float Y,float W,float H,float R=18,bool Hero=false)
{
 auto Edge=Rounded(X,Y,W,H,R);TArray<FVector2D>P;TArray<FLinearColor>Colors;TArray<SlateIndex>I;
 P.Add({X+W*.5f,Y+H*.5f});Colors.Add(RGB(2,25,35,230));
 for(const auto& V:Edge){P.Add(V);const float T=float((V.Y-Y)/H);Colors.Add(FMath::Lerp(RGB(6,51,65,225),RGB(3,37,48,207),T));}
 for(int K=0;K<Edge.Num();++K)I.Append({0,SlateIndex(K+1),SlateIndex((K+1)%Edge.Num()+1)});
 C.Mesh(P,Colors,I);const auto EdgeStart=Edge[0];Edge.Add(EdgeStart);C.Lines(Edge,RGB(111,215,230,175),1.1f);
 C.Layer+=2;C.Line(X+R,Y+1,X+W-R,Y+1,RGB(151,240,243,110),1.f);
 if(Hero)
 {
  C.Glow({{X,Y+31},{X+22,Y+3},{X+82,Y+1}},RGB(255,226,164,240),1.7f);
  C.Glow({{X+W-82,Y+H-1},{X+W-22,Y+H-3},{X+W,Y+H-28}},RGB(255,226,164,240),1.7f);
  C.RadialGlow(X+26,Y+15,62,33,RGB(251,214,139,85));C.RadialGlow(X+W-26,Y+H-10,72,28,RGB(251,214,139,75));
 }
}
void Wave(const PondDashboard::FCanvas& C,float X,float Y,float W,float H,FLinearColor Color,float Phase=0)
{
 TArray<FVector2D>P;for(int I=0;I<=60;++I){const float T=I/60.f;P.Add({X+W*T,Y+FMath::Sin(T*PI*2+Phase)*H*FMath::Sin(T*PI)});}C.Lines(P,Color,1.2f);
}
void AreaIcon(const PondDashboard::FCanvas& C,float X,float Y,float S,FLinearColor Color)
{
 C.Lines({{X-S*.5,Y+S*.3},{X-S*.35,Y-S*.3},{X+S*.35,Y-S*.3},{X+S*.5,Y+S*.3},{X-S*.5,Y+S*.3}},Color,1.3f);C.Line(X-S*.34,Y,X+S*.34,Y,Color);C.Line(X,Y-S*.3,X,Y+S*.3,Color);
}
void StackIcon(const PondDashboard::FCanvas& C,float X,float Y,float S,FLinearColor Color)
{
 for(int K=0;K<3;++K)C.Lines({{X-S*.5,Y+K*5},{X,Y+S*.25+K*5},{X+S*.5,Y+K*5}},Color,1.3f);
 C.Lines({{X-S*.5,Y},{X,Y-S*.25},{X+S*.5,Y}},Color,1.3f);
}
void Pin(const PondDashboard::FCanvas& C,float X,float Y,FLinearColor Color)
{
 C.Ellipse(X,Y,5,5,Color,2);C.Lines({{X-4,Y+4},{X,Y+10},{X+4,Y+4}},Color,1.3f);
}
void Fish(PondDashboard::FCanvas C,float X,float Y,const FString& Species,float Size,FLinearColor Color)
{
 if(Species==TEXT("其他")){for(int K=-1;K<=1;++K)C.RadialGlow(X+K*Size*.23,Y,5,5,Color);return;}
 const bool Eel=Species==TEXT("鳗鲡"),Silver=Species==TEXT("鲢鱼"),Perch=Species==TEXT("淡水鲈鱼"),Snake=Species==TEXT("乌鳢"),Sturgeon=Species==TEXT("鲟鱼");
 // 沿用原生矢量图标入口，以各鱼种体态、鳍尾和背腹明暗区别，避免同一网格骨架换名字。-codex，2026-11-03
 auto Dark=Color*.31f;Dark.A=Color.A*.92f;auto Mid=Color*.72f;Mid.A=Color.A;
 auto Light=FMath::Lerp(Color,FLinearColor::White,.42f);Light.A=Color.A;
 const float Length=Eel?1.02f:Snake?.94f:Sturgeon?.84f:Silver?.69f:.80f;
 const float H=Eel?.038f:Snake?.105f:Sturgeon?.115f:Silver?.205f:Perch?.17f:.135f;
 C.RadialGlow(X,Y+Size*.21,Size*.55,Size*.10,RGB(42,196,213,50));
 auto Point=[&](float U,float V){return FVector2D(X+Size*U,Y+Size*V);};
 const float Start=-Length*.43f,End=Length*.47f;
 if(!Eel)
 {
  const float Tail=Snake?.11f:Sturgeon?.20f:.18f;
  TArray<FVector2D>TailShape={Point(Start+.05f,0),Point(-.51f,-Tail),Point(-.445f,0),Point(-.51f,Tail*(Sturgeon?.45f:1.f)),Point(Start+.05f,0)};
  C.Polygon(TailShape,Mid);C.Lines(TailShape,Color,1.f);
  if(Perch)
  {
   TArray<FVector2D>Fin={Point(-.24f,-H*.7f)};
   for(int K=0;K<6;++K){const float U=-.21f+K*.067f;Fin.Add(Point(U,-H-.10f+K*.01f));Fin.Add(Point(U+.032f,-H*.76f));}
   C.Polygon(Fin,Mid);C.Lines(Fin,Color,.9f);
  }
  else if(Snake)
  {C.Polygon({Point(-.31f,-.055f),Point(-.27f,-.16f),Point(.18f,-.13f),Point(.29f,-.045f)},Mid);}
  else
  {C.Polygon({Point(-.18f,-H*.7f),Point(-.055f,-H-.10f),Point(.10f,-H*.88f)},Mid);}
  C.Polygon({Point(-.08f,H*.65f),Point(.04f,H+.065f),Point(.12f,H*.66f)},Mid);
 }
 TArray<FVector2D>Vertices,Top,Bottom;TArray<FLinearColor>Colors;TArray<SlateIndex>Indices;
 constexpr int N=32;
 for(int K=0;K<=N;++K)
 {
  const float T=K/float(N),U=FMath::Lerp(Start,End,T);
  const float Axis=Eel?.13f*FMath::Sin(T*PI*2):-.012f*FMath::Sin(T*PI);
  const float Profile=FMath::Pow(FMath::Max(0.f,FMath::Sin(T*PI)),Snake?.40f:Eel?.52f:.78f);
  const float Width=H*Profile*(Eel?(.55f+T*.65f):1.f);
  Top.Add(Point(U,Axis-Width));Bottom.Add(Point(U,Axis+Width*(Silver?1.08f:.80f)));
  Vertices.Append({Top.Last(),Point(U,Axis+Width*.12f),Bottom.Last()});Colors.Append({Dark,Light,Mid});
  if(K<N){const SlateIndex A=SlateIndex(K*3),B=SlateIndex((K+1)*3);Indices.Append({A,SlateIndex(A+1),B,B,SlateIndex(A+1),SlateIndex(B+1),SlateIndex(A+1),SlateIndex(A+2),SlateIndex(B+1),SlateIndex(B+1),SlateIndex(A+2),SlateIndex(B+2)});}
 }
 C.Mesh(Vertices,Colors,Indices);C.Lines(Top,Color,1.f);C.Lines(Bottom,Mid,.8f);
 if(Sturgeon)
 {
  C.Polygon({Point(End-.09f,-.045f),Point(.55f,-.012f),Point(End-.02f,.035f)},Mid);
  for(int K=0;K<6;++K){const float U=-.24f+K*.083f;C.Polygon({Point(U,-.035f),Point(U+.027f,-.062f),Point(U+.05f,-.025f),Point(U+.022f,0)},Light);}
 }
 else if(Perch)
 {for(int K=0;K<5;++K){const float U=-.20f+K*.09f;C.Line(X+Size*U,Y-Size*H*.67f,X+Size*(U+.05f),Y+Size*H*.57f,Dark,2.2f);}}
 else if(Snake)
 {for(int Row=0;Row<2;++Row)for(int K=0;K<7;++K)C.Ellipse(X+Size*(-.26f+K*.077f),Y+Size*(Row?-.025f:.045f),Size*.018f,Size*.011f,Dark,1.8f);}
 else if(!Eel)
 {for(int Row=0;Row<2;++Row)for(int K=0;K<6;++K){const float U=-.23f+K*.075f;C.Lines({Point(U,.005f+Row*.04f),Point(U+.025f,-.015f+Row*.04f),Point(U+.05f,.005f+Row*.04f)},Mid,.65f);}}
 if(!Eel)C.Lines({Point(End-.15f,-H*.52f),Point(End-.18f,.005f),Point(End-.12f,H*.47f)},Dark,1.1f);
 C.Ellipse(X+Size*(End-.065f),Y-Size*(Eel?.035f:.036f),Size*.020f,Size*.020f,Light,1.8f);
 C.Ellipse(X+Size*(End-.062f),Y-Size*(Eel?.035f:.036f),Size*.008f,Size*.008f,Dark,1.5f);
}
}
FBox2D SMotionDashboard::PondImageRect(int32 Index,const FBox2D& Bounds)const
{
 const int32 Variant=MotionCarousel::Wrap(Index,FMotionPondSprites::VariantCount);
 const auto Source=PondSprites?PondSprites->AspectSize(Variant):FVector2D::ZeroVector;
 if(Source.X<=0||Source.Y<=0)return FBox2D(FVector2D::ZeroVector,FVector2D::ZeroVector);
 // 原PNG宽高比完整保留，展示区只控制等比contain；树与设备不能随塘体被压扁。绘制和点击共用此矩形。-codex，2026-11-03
 const double S=FMath::Min(Bounds.GetSize().X/Source.X,Bounds.GetSize().Y/Source.Y);
 const FVector2D Size=Source*S;
 const double Bottom=Bounds.Max.Y-15.*Bounds.GetSize().X/820.;
 return FBox2D(FVector2D(Bounds.GetCenter().X-Size.X*.5,Bottom-Size.Y),FVector2D(Bounds.GetCenter().X+Size.X*.5,Bottom));
}
void SMotionDashboard::DrawPond(PondDashboard::FCanvas C,int32 Index,const FBox2D& Bounds)const
{
 const int32 Variant=MotionCarousel::Wrap(Index,FMotionPondSprites::VariantCount);const auto* Brush=PondSprites?PondSprites->Brush(Variant):nullptr;
 if(!Brush){C.Text(Bounds.Min.X,Bounds.Max.Y-50,TEXT("鱼塘素材暂不可用"),18,Soft,Bounds.GetSize().X,1);return;}
 const auto Image=PondImageRect(Index,Bounds);
 C.Image(Image.Min.X,Image.Min.Y,Image.GetSize().X,Image.GetSize().Y,*Brush);
}
void SMotionDashboard::DrawSubject(PondDashboard::FCanvas C,const Xinluo::FNavigation& Nav)const
{
 const int32 Index=Data.Survey.Find(Nav.Subject);if(!Data.Survey.Subjects.IsValidIndex(Index))return;
 const auto& R=Data.Survey.Subjects[Index];const float Reveal=Ease(float((FPlatformTime::Seconds()-PageEntered)/.8));
 C.Gradient(0,0,1920,160,RGB(2,17,26,170),RGB(2,17,26,0));Accent(C,75,40,TEXT("新罗区"),45,Ink,180);Accent(C,267,40,TEXT("渔场档案"),45,Aqua,300);
 Wave(C,60,109,440,12,RGB(86,220,235,165));
 if(!bLinked)C.Text(1450,44,TEXT("← 返回养殖分布"),17,Soft,210,2);
 auto Pond=C;Pond.Opacity*=Reveal;Pond.Shift.Y+=20*(1-Reveal);C.RadialGlow(670,825,580,80,RGB(29,194,217,65));
 DrawPond(Pond,Index,FBox2D(FVector2D(55,190),FVector2D(1235,866)));C.Layer+=20;
 auto Card=C;Card.Shift.X+=30*(1-Reveal);Card.Opacity*=Reveal;Glass(Card,1290,183,570,741,24,true);
 int Split=R.Name.Len()>11?(R.Name.Len()+1)/2:R.Name.Len();
 Accent(Card,1327,218,R.Name.Left(Split),35,Sand,495);if(Split<R.Name.Len())Accent(Card,1327,264,R.Name.Mid(Split),35,Sand,495);
 Card.Line(1325,330,1825,330,RGB(118,210,216,110));Pin(Card,1338,362,Aqua);Card.WrappedText(1361,347,R.Address,21,Aqua,450,30);
 Fish(Card,1384,477,R.Species,95,Aqua);Card.Text(1460,437,TEXT("养殖品种"),18,Soft,330);Accent(Card,1460,471,R.Species,35,Sand,330);
 Card.Line(1325,541,1825,541,RGB(118,210,216,95));AreaIcon(Card,1382,621,47,Aqua);Card.Text(1460,568,TEXT("登记面积"),18,Soft,330);Number(Card,1456,602,340,Xinluo::Area(R.AreaMuTenths),47,TEXT("亩"),23);
 Card.Line(1325,700,1825,700,RGB(118,210,216,95));StackIcon(Card,1382,779,43,Aqua);Card.Text(1460,731,TEXT("养殖模式"),18,Soft,330);Accent(Card,1460,773,R.Mode,34,Sand,330);
 Card.Text(1327,876,R.HasCoordinate?TEXT("资料来源：本次调研登记"):TEXT("本条未登记坐标，仍可查看资料"),16,Soft,498);
 C.Text(82,1010,TEXT("塘形为主体示意，非真实场址或面积比例"),15,Soft,1060);
}
void SMotionDashboard::DrawCity(PondDashboard::FCanvas C,const Xinluo::FNavigation&)const
{
 const auto S=Data.Snapshot(0);const double Time=FPlatformTime::Seconds()-Started;
 C.Gradient(0,0,1920,170,RGB(2,16,25,120),RGB(2,16,25,0));
 Accent(C,76,39,TEXT("新罗区"),45,Ink,180);Accent(C,266,39,TEXT("水产调研"),45,Aqua,300);
 Wave(C,48,102,430,14,RGB(99,226,237,200));Wave(C,90,106,380,8,RGB(68,180,202,85),PI*.7f);
 // 固定统计层高于轮转塘和引线；近侧塘经过面板时不能盖住数字、单位。命中也排除此区。-codex，2026-11-03
 auto Metrics=C;Metrics.Layer+=110;const auto MetricsRect=MotionCarousel::MetricsCard();
 Glass(Metrics,MetricsRect.Min.X,MetricsRect.Min.Y,MetricsRect.GetSize().X,MetricsRect.GetSize().Y,32);StackIcon(Metrics,72,184,23,Aqua);Metrics.Text(97,175,TEXT("本次调研"),18,Soft,190);
 Number(Metrics,64,210,215,FString::FromInt(S.Count),64,TEXT("家主体"),20);Metrics.Line(56,292,262,292,RGB(110,206,218,155),1.f);
 Pin(Metrics,74,332,Aqua);Metrics.Text(97,321,TEXT("登记面积"),18,Soft,190);Number(Metrics,63,355,208,Xinluo::Area(S.AreaMuTenths),45,TEXT("亩"),18);
 Metrics.Text(64,423,TEXT("面积含水库"),14,RGB(130,168,178),235);Wave(Metrics,40,462,232,12,RGB(80,219,235,160),PI*.3);
 if(!bLinked)C.Text(1450,44,TEXT("← 返回上一级"),17,Soft,210,2);
 // 背轨低亮，前轨有刻度；层号低于全部塘体，不能遮住资料。-codex，2026-11-03
 C.Ellipse(1040,535,780,172,RGB(74,195,218,65),1.f,PI,2*PI);C.Ellipse(1040,535,780,172,RGB(94,226,241,180),1.4f,0,PI);
 C.Ellipse(1040,535,775,169,RGB(35,199,226,35),9.f,0,PI);C.RadialGlow(1040,704,660,26,RGB(70,215,235,60));
 for(int K=0;K<=80;++K){const float A=K*PI/80;C.Line(1040+769*FMath::Cos(A),535+162*FMath::Sin(A),1040+779*FMath::Cos(A),535+171*FMath::Sin(A),RGB(63,175,197,90));}
 C.Layer+=10;
 const float Entry=Ease(float((FPlatformTime::Seconds()-PageEntered)/.8));
 for(const auto& Item:CarouselFrame.Items)
 {
  auto Pond=C;Pond.Opacity*=MotionCarousel::PondOpacity(Item)*Entry;Pond.Shift.Y+=14*(1-Entry);
  DrawPond(Pond,Item.Index,MotionCarousel::PondHit(Item));C.Layer+=3;
 }
 C.Layer+=10;
 for(const auto& Item:CarouselFrame.Items)
 {
  if(!MotionCarousel::IsSideCard(CarouselFrame,Item)||!Data.Survey.Subjects.IsValidIndex(Item.Index))continue;
  const float Weight=MotionCarousel::SideCardWeight(Item);
  const auto& R=Data.Survey.Subjects[Item.Index];const bool Left=Item.Theta<0;
  const float X=(Left?403.f:1490.f)+float(Item.Anchor.X-(Left?448.:1696.))*.13f,Y=193+float(Item.Anchor.Y-496)*.2f,W=Left?325.f:348.f;
  auto Card=C;Card.Opacity*=Weight*Entry;Glass(Card,X,Y,W,165,16);
  Card.WrappedText(X+18,Y+12,R.Name,18,Ink,W-36,22);
  Card.Line(X+18,Y+57,X+W-18,Y+57,RGB(95,177,195,65));Fish(Card,X+46,Y+76,R.Species,51,Aqua);Card.Text(X+82,Y+63,R.Species,19,Ink,W-100);
  Card.Line(X+18,Y+94,X+W-18,Y+94,RGB(95,177,195,65));AreaIcon(Card,X+32,Y+111,23,Soft);Card.Text(X+57,Y+101,TEXT("登记面积"),16,Soft,112);Card.Text(X+182,Y+99,Xinluo::Area(R.AreaMuTenths)+TEXT(" 亩"),19,Ink,W-194);
  StackIcon(Card,X+32,Y+139,22,Soft);Card.Text(X+57,Y+128,TEXT("养殖模式"),16,Soft,112);Card.Text(X+182,Y+127,R.Mode,18,Ink,W-194);
  const FVector2D End(Item.Anchor.X,Item.Anchor.Y-10);Card.Line(X+W*(Left?.33:.55),Y+165,End.X,End.Y,RGB(159,231,232,160),1.f);Card.RadialGlow(End.X,End.Y,11,11,RGB(166,247,244,180));Card.Ellipse(End.X,End.Y,2.6f,2.6f,RGB(234,255,247),2.f);
 }
 C.Layer+=10;
 if(Data.Survey.Subjects.IsValidIndex(CarouselFrame.FocusIndex))
 {
  const auto& R=Data.Survey.Subjects[CarouselFrame.FocusIndex];const auto Rect=MotionCarousel::HeroCard();const float X=Rect.Min.X,Y=Rect.Min.Y,W=Rect.GetSize().X;
  auto Card=C;const float A=MotionCarousel::HeroOpacity(CarouselFrame);Card.Opacity*=A*Entry;Card.Shift.Y+=9*(1-A);
  Glass(Card,X,Y,W,146,23,true);Fish(Card,X+70,Y+65,R.Species,83,Sand);
  const int Split=R.Name.Len()>10?(R.Name.Len()+1)/2:R.Name.Len();
  Accent(Card,X+124,Y+(Split<R.Name.Len()?19:33),R.Name.Left(Split),30,Sand,288);
  if(Split<R.Name.Len())Accent(Card,X+124,Y+57,R.Name.Mid(Split),30,Sand,288);
  Pin(Card,X+128,Y+118,Aqua);Card.Text(X+145,Y+106,R.Address,17,Soft,265);
  Card.Line(X+418,Y+28,X+418,Y+94,RGB(80,169,188,115));Card.Line(X+608,Y+28,X+608,Y+94,RGB(80,169,188,115));
  AreaIcon(Card,X+450,Y+41,25,Soft);Card.Text(X+481,Y+28,TEXT("登记面积"),17,Soft,120);Number(Card,X+473,Y+65,125,Xinluo::Area(R.AreaMuTenths),37,TEXT("亩"),19);
  StackIcon(Card,X+645,Y+38,23,Soft);Card.Text(X+672,Y+28,TEXT("养殖模式"),17,Soft,100);Accent(Card,X+656,Y+70,R.Mode,28,Sand,118);
  Wave(Card,X+W-195,Y+132,170,8,RGB(184,207,178,110));
 }
 for(int K=0;K<2;++K)
 {
  const float X=K?1643:442,Y=618;C.RadialGlow(X,Y,36,36,RGB(65,216,230,55));C.Ellipse(X,Y,27,27,RGB(122,219,230,180),1.1f);
  // 批准稿中的轨道方向装饰，不承诺新增业务控制；细人字线避免辉光叠成菱形。-codex，2026-11-03
  const float Sign=K?1.f:-1.f;C.Lines({{X-Sign*5,Y-10},{X+Sign*5,Y},{X-Sign*5,Y+10}},RGB(211,246,240,230),2.f);
 }
 for(int K=0;K<3;++K){const bool Active=MotionCarousel::Wrap(CarouselFrame.FocusIndex,3)==K;C.RadialGlow(1014+K*26,762,7,7,Active?RGB(247,222,155,210):RGB(98,195,210,120));C.Ellipse(1014+K*26,762,2.5,2.5,Active?Sand:Aqua,2.f);}
 C.Layer+=20;Glass(C,40,805,1840,192,28);Wave(C,55,978,1810,10,RGB(54,160,186,55),float(Time*.15));
 Wave(C,66,944,210,8,RGB(89,216,231,155));Wave(C,89,865,142,9,RGB(101,222,230,160),PI*.5);
 Accent(C,78,887,TEXT("养殖品种汇总"),29,Ink,220);C.Line(320,844,320,952,RGB(91,183,203,140));
 for(int I=0;I<S.SpeciesValues.Num();++I)
 {
  const float X=326+I*218;const auto& Item=S.SpeciesValues[I];Fish(C,X+100,845,Item.Key,88,Aqua);Accent(C,X+10,879,Item.Key,25,Ink,180,false,1);
  Number(C,X+69,918,120,FString::Printf(TEXT("%lld"),Item.Value),47,TEXT("家"),21);
  if(I<6)C.Line(X+211,844,X+211,952,RGB(91,183,203,140));
 }
 C.Text(55,1028,TEXT("场景单元为主体示意，非真实边界"),14,RGB(139,169,178),900);
}
