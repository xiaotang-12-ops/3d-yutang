// 仅-MotionFlickerProbe启用的90秒UI诊断；保存计数，不保存帧/像素或录像。-codex，2026-10-24
#pragma once
#include "CoreMinimal.h"
#include "PondDashboardDraw.h"
struct FMotionProbeRenderState;
class FMotionFlickerProbe
{
public:
 static bool Enabled();
 static void AddIndependentMarker();
 static void PaintMarker(PondDashboard::FCanvas C);
 FMotionFlickerProbe();
 ~FMotionFlickerProbe();
 void Tick(uint64 PaintCount,bool Switching,float Veil,FVector2D Size,bool Volatile);
private:
 FDelegateHandle Handle;
 TSharedPtr<FMotionProbeRenderState,ESPMode::ThreadSafe> State;
 double LastReport=0;uint64 LastPaintCount=0;
};
