// 数据总览只消费同一调查快照，不持有选择或写入导航。-codex，2026-10-28
#pragma once
#include "PondDashboardDraw.h"
#include "MotionData.h"
namespace MotionDataPage
{
void Draw(PondDashboard::FCanvas C,const FMotionSnapshot& Snapshot,float Reveal,double Time,int32 UnusedHover);
}
