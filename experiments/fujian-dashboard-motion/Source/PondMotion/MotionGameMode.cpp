// 隔离UE运行入口，数据屏使用真实Slate输入；没有自动轮播代替点击。-codex，2026-10-23
#include "MotionGameMode.h"
#include "MotionDashboard.h"
#include "MotionStage.h"
#include "MotionFlickerProbe.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl,PondMotion,"PondMotion");
AMotionGameMode::AMotionGameMode(){DefaultPawnClass=nullptr;}
void AMotionGameMode::StartPlay()
{
 Super::StartPlay();
 if(!GEngine||!GEngine->GameViewport)return;
 auto* Stage=GetWorld()->SpawnActor<AMotionStage>();
 auto Screen=SNew(SMotionDashboard).Stage(Stage);GEngine->GameViewport->AddViewportWidgetContent(Screen,100);
 FMotionFlickerProbe::AddIndependentMarker();
 if(auto* PC=GetWorld()->GetFirstPlayerController())
 {
  PC->bShowMouseCursor=true;FInputModeUIOnly Mode;Mode.SetWidgetToFocus(Screen);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
 }
 FSlateApplication::Get().SetKeyboardFocus(Screen);
 UE_LOG(LogTemp,Display,TEXT("MotionDashboard native Slate ready; simulation source; no automatic selection"));
}
