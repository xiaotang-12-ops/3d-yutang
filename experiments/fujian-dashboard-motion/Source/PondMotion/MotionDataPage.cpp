// 新罗调研总览：七类为等规格分类标签，数值编码只用真实主体数量，不借带长伪造比例。-codex，2026-10-28
#include "MotionDataPage.h"
namespace MotionDataPage
{
using namespace PondDashboard;
void Draw(PondDashboard::FCanvas C,const FMotionSnapshot& S,float Reveal,double Time,int32)
{
 const FLinearColor Ink=RGB(239,255,246),Soft=RGB(158,193,178),Jade=RGB(100,225,182),Sand=RGB(237,207,143),Rule=RGB(69,118,97,170);
 C.Opacity*=Ease(Reveal);C.Text(300,42,TEXT("新罗区调研总览"),48,Ink,820);C.Line(300,115,560,115,Sand,2);
 C.Text(900,47,TEXT("水产养殖资源调研"),25,Soft,380);C.Text(902,92,TEXT("本次调研 · 主体数量与登记面积"),14,Soft,370);
 C.Line(1265,145,1265,1015,Rule);
 const float CX=650,CY=580,RX=540,RY=335;
 for(int I=0;I<S.SpeciesValues.Num();++I)
 {
  const auto& Item=S.SpeciesValues[I];const float A=-PI*.5f+I*2*PI/7,Half=.37f;
  TArray<FVector2D> Band;
  for(int K=0;K<=18;++K){const float T=A-Half+2*Half*K/18;Band.Add({CX+(RX+62)*FMath::Cos(T),CY+(RY+46)*FMath::Sin(T)});}
  for(int K=18;K>=0;--K){const float T=A-Half+2*Half*K/18;Band.Add({CX+(RX-66)*FMath::Cos(T),CY+(RY-48)*FMath::Sin(T)});}
  C.Polygon(Band,RGB(17,104,78,115));auto Outline=Band;Outline.Add(Band[0]);C.Lines(Outline,RGB(87,218,169,190),1.2f);
  const float X=CX+RX*FMath::Cos(A),Y=CY+RY*FMath::Sin(A);
  if(I==2||I==5)
  {
   // 两侧窄弧板使用居中纵向组，避免图标越过外弧或数量挤向中央塘形。
   C.SpeciesIcon(X,Y-36,Item.Key,48,Jade);C.Text(X-54,Y-13,Item.Key,16,Ink,108,1);
   C.Readout(X-45,Y+14,90,FString::Printf(TEXT("%lld"),Item.Value),27,TEXT("家"),15,Sand,1);
  }
  else
  {
   C.SpeciesIcon(X-42,Y-9,Item.Key,48,Jade);C.Text(X-10,Y-31,Item.Key,16,Ink,105);
   C.Readout(X-10,Y-5,100,FString::Printf(TEXT("%lld"),Item.Value),29,TEXT("家"),15,Sand);
  }
 }
 C.Layer+=10;C.Text(1305,148,TEXT("本次调研"),20,Soft);C.Readout(1300,188,230,FString::FromInt(S.Count),58,TEXT("家主体"),21,Sand);
 C.Line(1530,145,1530,270,Rule);C.Text(1570,148,TEXT("登记面积"),20,Soft);C.Readout(1560,200,315,Xinluo::Area(S.AreaMuTenths),43,TEXT("亩"),18,Sand);
 C.Line(1300,305,1875,305,Rule);C.Text(1305,340,TEXT("养殖品种分布"),24,Ink,550);
 for(int I=0;I<S.SpeciesValues.Num();++I)
 {
  const auto& Item=S.SpeciesValues[I];const float Y=410+I*52;
  C.SpeciesIcon(1340,Y+15,Item.Key,65,Jade);C.Text(1400,Y,Item.Key,21,Ink,250);
  C.Readout(1690,Y-1,155,FString::Printf(TEXT("%lld"),Item.Value),26,TEXT("家"),17,Sand,2);
 }
 C.Line(1300,795,1875,795,Rule);C.Text(1305,827,TEXT("养殖模式"),24,Ink,500);
 for(int I=0;I<S.Modes.Num();++I)
 {
  const float X=1300+I*295;
  C.Gradient(X,883,280,115,RGB(9,47,35,175),RGB(4,29,23,150));
  C.Lines({{X,998},{X,883},{X+280,883},{X+280,998},{X,998}},Rule);
  if(I==0){C.Ellipse(X+40,925,20,10,Jade,1.5f);C.Ellipse(X+40,931,20,10,Jade,1.f);}
  else {for(int K=0;K<3;++K)C.Ellipse(X+27+K*13,925,3,3,Jade,1.5f);}
  C.Text(X+76,899,S.Modes[I].Key,19,Ink,190);
  C.Readout(X+76,932,190,FString::Printf(TEXT("%lld"),S.Modes[I].Value),38,TEXT("家"),18,Sand);
 }
 C.Text(1300,1015,TEXT("面积包含水库 · 塘形仅为示意"),15,Soft,575,2);
}
}
