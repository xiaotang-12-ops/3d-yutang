// 城市展示轮转的唯一位置帧：只表达呈现索引，不修改A导航、台账或业务选中。-codex，2026-10-31
#pragma once
#include "CoreMinimal.h"

namespace MotionCarousel
{
constexpr int32 PoolSize=7;
constexpr double SlotStep=.59;
constexpr double SecondsPerSubject=3.6;
constexpr double RadiansPerSecond=SlotStep/SecondsPerSubject;
constexpr double EdgeAngle=1.95;
struct FItem
{
 int32 Slot=INDEX_NONE,Index=INDEX_NONE;
 float Theta=0,Depth=0,Scale=0,Opacity=0;
 FVector2D Anchor=FVector2D::ZeroVector;
};
struct FFrame
{
 uint64 Serial=0;
 int32 FocusIndex=INDEX_NONE,FocusSlot=INDEX_NONE;
 TArray<FItem> Items;
};
inline int32 Wrap(int32 Value,int32 Count){return Count>0?(Value%Count+Count)%Count:INDEX_NONE;}

// 保留七槽与主体数的共同周期；跨26家回绕时，隐藏槽才重用，不在可见区换塘。-codex，2026-10-31
inline FFrame Build(double Position,int32 Count,uint64 Serial)
{
 FFrame Frame;Frame.Serial=Serial;
 if(Count<=0||!FMath::IsFinite(Position))return Frame;
 const double Phase=FMath::Fmod(Position,double(Count)*PoolSize);
 const double Whole=FMath::FloorToDouble(Phase),Fraction=Phase-Whole;
 // 每家短暂停留再平滑交接，给真实长名称阅读时间；起止速度为零，避免匀速掠过。-codex，2026-10-31
 const double Move=FMath::Clamp((Fraction-.22)/.78,0.,1.);
 const double Center=Whole+Move*Move*(3.-2.*Move);
 const int32 Middle=FMath::FloorToInt(Center);
 float Closest=TNumericLimits<float>::Max();
 for(int32 K=Middle-4;K<=Middle+4;++K)
 {
  const double Theta=(K-Center)*SlotStep,Distance=FMath::Abs(Theta);
  if(Distance>=EdgeAngle)continue;
  FItem Item;Item.Slot=Wrap(K,PoolSize);Item.Index=Wrap(K,Count);Item.Theta=float(Theta);
  Item.Depth=float(FMath::Cos(Theta));
  const double NarrowFocus=FMath::Exp(-Theta*Theta/(2.*.24*.24));
  Item.Scale=float(.26+.30*FMath::Pow(FMath::Max(0.,double(Item.Depth)),4.)+.45*NarrowFocus);
  const double Fade=FMath::Clamp((Distance-.7)/1.05,0.,1.);
  Item.Opacity=float(1.-Fade*Fade*(3.-2.*Fade));
  Item.Anchor=FVector2D(1072.+740.*FMath::Sin(Theta*1.7),360.+175.*FMath::Cos(Theta*1.15));
  Frame.Items.Add(Item);
  if(Distance<Closest){Closest=float(Distance);Frame.FocusIndex=Item.Index;Frame.FocusSlot=Item.Slot;}
 }
 Frame.Items.Sort([](const FItem& A,const FItem& B){return A.Depth<B.Depth;});
 return Frame;
}
inline FBox2D HeroCard(){return FBox2D(FVector2D(624,592),FVector2D(1408,738));}
inline FBox2D MetricsCard(){return FBox2D(FVector2D(34,154),FVector2D(280,475));}
// 边缘槽只为连续轮转保留；最终可见性须同时用于绘制与命中，防止选到已淡出的塘。-codex，2026-11-03
inline float PondOpacity(const FItem& Item)
{
 const float Fade=FMath::Clamp((FMath::Abs(Item.Theta)-.8f)/.3f,0.f,1.f);
 return Item.Opacity*(1-Fade*Fade*(3-2*Fade));
}
inline float SideCardWeight(const FItem& Item)
{const float D=(FMath::Abs(Item.Theta)-.59f)/.24f;return FMath::Exp(-FMath::Pow(D,4.f));}
inline bool IsSideCard(const FFrame& Frame,const FItem& Item)
{
 const float Weight=SideCardWeight(Item);if(Weight<.02f)return false;
 // 同侧交接时只保留最靠近卡片位置的主体，不能把两套企业文字叠到同一资料框。-codex，2026-11-03
 for(const auto& Other:Frame.Items)
  if((Other.Theta<0)==(Item.Theta<0)&&Other.Slot!=Item.Slot)
  {
   const float W=SideCardWeight(Other);
   if(W>Weight||(W==Weight&&Other.Slot<Item.Slot))return false;
  }
 return true;
}
inline float HeroOpacity(const FFrame& Frame)
{
 const FItem* Item=Frame.Items.FindByPredicate([&](const FItem& I){return I.Slot==Frame.FocusSlot;});
 if(!Item)return 0.f;
 const float T=FMath::Clamp((.295f-FMath::Abs(Item->Theta))/.13f,0.f,1.f);
 return T*T*(3.f-2.f*T);
}
inline FBox2D PondHit(const FItem& Item)
{
 return FBox2D(Item.Anchor+FVector2D(-410.*Item.Scale,-310.*Item.Scale),Item.Anchor+FVector2D(410.*Item.Scale,80.*Item.Scale));
}
inline bool CanSelectPond(const FItem& Item,const FVector2D& Point)
{return PondOpacity(Item)>.15f&&!MetricsCard().IsInsideOrOn(Point)&&PondHit(Item).IsInsideOrOn(Point);}
}
