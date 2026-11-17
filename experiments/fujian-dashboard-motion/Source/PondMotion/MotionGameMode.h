// 只建立隔离B屏；不生成三维地图、不连接或修改主工程控制器。-codex，2026-10-23
#pragma once
#include "GameFramework/GameModeBase.h"
#include "MotionGameMode.generated.h"
UCLASS()
class AMotionGameMode:public AGameModeBase
{
 GENERATED_BODY()
 public: AMotionGameMode(); virtual void StartPlay() override;
};
