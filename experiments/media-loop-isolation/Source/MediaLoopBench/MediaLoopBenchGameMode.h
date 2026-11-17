#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RenderCommandFence.h"
#include "Styling/SlateBrush.h"
#include "MediaLoopBenchGameMode.generated.h"

class UMediaPlayer;
class UMediaTexture;
class UMediaSource;
class SWidget;
struct FBenchPresentState;

UCLASS()
class AMediaLoopBenchGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMediaLoopBenchGameMode();
    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool FullCache() const;
    void Finish(bool Success, const FString& Reason);
    UPROPERTY() TObjectPtr<UMediaPlayer> Player;
    UPROPERTY() TObjectPtr<UMediaTexture> Texture;
    UPROPERTY() TObjectPtr<UMediaSource> Source;
    TSharedPtr<SWidget> Panel;
    TSharedPtr<FBenchPresentState, ESPMode::ThreadSafe> Present;
    FDelegateHandle PresentHandle;
    FSlateBrush Brush;
    FRenderCommandFence FirstFrameFence;
    FString Case, Label, Output, TickCsv;
    double OpenAt = 0, PlayAt = 0, ReadyAt = 0, SampleAt = 0, LastTick = 0, Duration = 18;
    uint64 TickNumber = 0;
    bool bPlayStarted = false, bFenceStarted = false, bFinished = false, bCacheChecked = false;
};
