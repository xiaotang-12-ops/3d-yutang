// 只验证轮转覆盖、槽复用和跨周期连续性，不替代实际画面的遮挡与观感验收。-codex，2026-10-31
#include "MotionCarousel.h"
#include "Misc/AutomationTest.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionCarouselCoverageTest,"PondMotion.Carousel.CoverageAndContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMotionCarouselCoverageTest::RunTest(const FString&)
{
 using namespace MotionCarousel;
 TSet<int32> Fronts;
 for(int32 Sample=0;Sample<=2600;++Sample)
 {
  const auto Frame=Build(double(Sample)/100.,26,Sample);
  TestTrue(TEXT("最多七个可见槽"),Frame.Items.Num()>=6&&Frame.Items.Num()<=PoolSize);
  TSet<int32> Slots,Subjects,CardSides;
  float LastDepth=-2.f;
  for(const auto& Item:Frame.Items)
  {
   TestTrue(TEXT("物理槽与真实索引均合法"),Item.Slot>=0&&Item.Slot<PoolSize&&Item.Index>=0&&Item.Index<26);
   TestFalse(TEXT("同帧无槽碰撞"),Slots.Contains(Item.Slot));Slots.Add(Item.Slot);
   TestFalse(TEXT("同帧无重复主体"),Subjects.Contains(Item.Index));Subjects.Add(Item.Index);
   TestTrue(TEXT("位置及尺度有限"),!Item.Anchor.ContainsNaN()&&FMath::IsFinite(Item.Scale)&&Item.Scale>0);
   TestTrue(TEXT("淡出有界"),Item.Opacity>=0&&Item.Opacity<=1);
   TestTrue(TEXT("按真实深度远到近排序"),Item.Depth>=LastDepth);LastDepth=Item.Depth;
   if(IsSideCard(Frame,Item)){const int32 Side=Item.Theta<0?0:1;TestFalse(TEXT("交接时同侧资料不重叠"),CardSides.Contains(Side));CardSides.Add(Side);}
  }
  TestTrue(TEXT("前景索引位于当前帧"),Subjects.Contains(Frame.FocusIndex));Fronts.Add(Frame.FocusIndex);
  const auto* Focus=Frame.Items.FindByPredicate([&](const FItem& V){return V.Slot==Frame.FocusSlot;});
  if(Focus)for(const auto& Neighbour:Frame.Items)
   if(Neighbour.Slot!=Focus->Slot&&FMath::Abs(Neighbour.Theta)<.9f)
    TestTrue(TEXT("前景与近侧塘屏幕包络分开"),FMath::Abs(Focus->Anchor.X-Neighbour.Anchor.X)>370.f*(Focus->Scale+Neighbour.Scale));
 }
 TestEqual(TEXT("一周期覆盖全部26主体"),Fronts.Num(),26);
 const auto Before=Build(25.9999,26,1),After=Build(26.0001,26,2);
 for(const auto& A:Before.Items)
  if(const auto* B=After.Items.FindByPredicate([&](const FItem& V){return V.Slot==A.Slot;}))
  {
   TestEqual(TEXT("跨26家边界不换可见槽内容"),A.Index,B->Index);
   TestTrue(TEXT("跨边界位置连续"),FVector2D::Distance(A.Anchor,B->Anchor)<1.);
  }
 const auto Start=Build(0,26,1),Later=Build(.35,26,2);
 const auto* Hidden=Start.Items.FindByPredicate([](const FItem& V){return V.Index==24;});
 TestTrue(TEXT("已淡出远塘不响应旧误选坐标"),Hidden&&PondOpacity(*Hidden)==0.f&&!CanSelectPond(*Hidden,{350,310}));
 const auto* Near=Start.Items.FindByPredicate([](const FItem& V){return V.Index==25;});
 TestTrue(TEXT("可见近侧塘仍可选择"),Near&&CanSelectPond(*Near,Near->Anchor));
 FItem OverMetrics;OverMetrics.Scale=1;OverMetrics.Opacity=1;OverMetrics.Anchor={260,400};
 TestFalse(TEXT("固定统计区不穿透选择背后塘"),CanSelectPond(OverMetrics,{260,400}));
 const auto* A=Start.Items.FindByPredicate([](const FItem& V){return V.Index==0;});
 const auto* B=Later.Items.FindByPredicate([](const FItem& V){return V.Index==0;});
 TestTrue(TEXT("前景塘持续向左退出"),A&&B&&B->Anchor.X<A->Anchor.X);
 const auto Dwell=Build(.1,26,4);
 const auto* D=Dwell.Items.FindByPredicate([](const FItem& V){return V.Index==0;});
 TestTrue(TEXT("短停期间保持前景锚点"),A&&D&&FVector2D::Distance(A->Anchor,D->Anchor)<.01);
 const auto Repeated=Build(26.*PoolSize,26,3);
 TestEqual(TEXT("共同周期前景一致"),Start.FocusIndex,Repeated.FocusIndex);
 TestTrue(TEXT("空数据不生成默认塘"),Build(0,0,1).Items.IsEmpty());
 TestTrue(TEXT("非法相位不生成坐标"),Build(std::numeric_limits<double>::quiet_NaN(),26,1).Items.IsEmpty());
 return true;
}
#endif
