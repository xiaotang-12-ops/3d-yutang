// 参考沙盘的离线高程载入、沿地轮廓与舞台几何；不管理省市业务状态。
// 数据与材质缺失时明确失败，不用随机地形伪装成功。-codex，2026-10-18
#pragma once
#include "CoreMinimal.h"
class AFujianMap;
class UMaterialInterface;
namespace TerrainScene
{
    bool Build(AFujianMap* Map, FString& Error);
}
