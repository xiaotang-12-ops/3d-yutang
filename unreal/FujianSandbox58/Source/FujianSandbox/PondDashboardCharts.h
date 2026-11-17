// 经营星盘、塘平面与两翼/月度图的确定性绘制接口；所有读数仍来自同一选择快照。-codex，2026-10-22
#pragma once
#include "PondDashboardDraw.h"
#include "PondPresentation.h"
namespace PondDashboard
{
void Backdrop(const FCanvas& C,float Time);
void Composition(const FCanvas& C,const FPondSnapshot& S,float Time,int32 HoverMonth);
}
