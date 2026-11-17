#include "MediaLoopBenchGameMode.h"
#include "Modules/ModuleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "ImgMediaSource.h"
#include "ImgMediaSettings.h"
#include "MediaPlayerFacade.h"
#include "IMediaPlayer.h"
#include "IMediaCache.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SWindow.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, MediaLoopBench, "MediaLoopBench");

// Present回调只记CPU单调时刻，不取GPU像素；退出后由共享状态保证回调引用有效。-codex，2026-10-26
struct FBenchPresentState
{
    FCriticalSection Mutex;
    struct FRow { uint64 Number; double Wall; };
    TArray<FRow> Rows;
    uint64 Count = 0;
    bool Recording = false;
};

AMediaLoopBenchGameMode::AMediaLoopBenchGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = nullptr;
}

void AMediaLoopBenchGameMode::StartPlay()
{
    Super::StartPlay();
    OpenAt = LastTick = FPlatformTime::Seconds();
    FParse::Value(FCommandLine::Get(), TEXT("MediaCase="), Case);
    FParse::Value(FCommandLine::Get(), TEXT("RunLabel="), Label);
    FParse::Value(FCommandLine::Get(), TEXT("EvidenceRoot="), Output);
    FParse::Value(FCommandLine::Get(), TEXT("Duration="), Duration);
    if (Case != TEXT("original") && Case != TEXT("concat") && Case != TEXT("sequence"))
    { Finish(false, TEXT("invalid MediaCase")); return; }
    if (Label.IsEmpty() || Output.IsEmpty() || Duration <= 0 || Duration > 120)
    { Finish(false, TEXT("missing label/root or invalid duration")); return; }
    for (TCHAR C : Label)
        if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_'))
        { Output.Empty(); Finish(false, TEXT("unsafe RunLabel")); return; }
    Output = FPaths::Combine(Output, Label);
    if (IFileManager::Get().DirectoryExists(*Output))
    { Output.Empty(); Finish(false, TEXT("RunLabel already exists; refusing evidence overwrite")); return; }
    IFileManager::Get().MakeDirectory(*Output, true);
    TickCsv = TEXT("tick,wall_s,wall_delta_ms,sample_elapsed_s,media_s,sample_valid,actual_lower_s,actual_upper_s,sequence,loop,present_count,full_cache,playing,error\n");
    const FString Movies = TEXT("D:/3d-research/experiments/fujian-dashboard-motion/Content/Movies");
    Player = NewObject<UMediaPlayer>(this);
    Texture = NewObject<UMediaTexture>(this);
    Player->NativeAudioOut = false;
    Player->PlayOnOpen = false;
    Texture->NewStyleOutput = true;
    Texture->SRGB = false;
    Texture->AutoClear = false;
    Texture->SetMediaPlayer(Player);
    Texture->UpdateResource();
    if (Case == TEXT("sequence"))
    {
        constexpr uint64 GiB = 1024ull * 1024ull * 1024ull;
        if (FPlatformMemory::GetStats().AvailablePhysical < GiB * 17 / 4)
        { Finish(false, TEXT("less than 3GiB reserve plus 1.25GiB cache")); return; }
        const UImgMediaSettings* Settings = GetDefault<UImgMediaSettings>();
        if (Settings->UseGlobalCache || Settings->bEnablePrecachingTimeInterval || Settings->BandwidthThrottlingEnabled
            || !FMath::IsNearlyEqual(Settings->CacheSizeGB, 1.25f) || !FMath::IsNearlyZero(Settings->CacheBehindPercentage))
        { Finish(false, TEXT("ImgMedia cache configuration mismatch")); return; }
        const FString Directory = FPaths::Combine(Movies, TEXT("pond-background-frames"));
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.jpg")), true, false);
        if (Files.Num() != 120) { Finish(false, TEXT("sequence must contain 120 JPG frames")); return; }
        for (int32 Index = 0; Index < 120; ++Index)
            if (!Files.Contains(FString::Printf(TEXT("frame_%04d.jpg"), Index)))
            { Finish(false, TEXT("sequence filename gap")); return; }
        UImgMediaSource* Images = NewObject<UImgMediaSource>(this);
        Images->SetSequencePath(Directory);
        Images->FrameRateOverride = FFrameRate(24, 1);
        Images->bFillGapsInSequence = false;
        Source = Images;
        Player->SetDesiredPlayerName(TEXT("ImgMedia"));
    }
    else
    {
        UFileMediaSource* File = NewObject<UFileMediaSource>(this);
        File->SetFilePath(Case == TEXT("original") ? FPaths::Combine(Movies, TEXT("pond-background-loop.mp4"))
            : TEXT("D:/3d-research/artifacts/fujian-dashboard-motion/media-compare-20261026/triple-loop.mp4"));
        Source = File;
        Player->SetDesiredPlayerName(TEXT("WmfMedia"));
    }
    Brush.SetResourceObject(Texture);
    Brush.ImageSize = FVector2D(1920, 1080);
    Brush.DrawAs = ESlateBrushDrawType::Image;
    Present = MakeShared<FBenchPresentState, ESPMode::ThreadSafe>();
    Present->Rows.Reserve(12000);
    const SWindow* TargetWindow = GEngine->GameViewport->GetWindow().Get();
    const auto State = Present;
    PresentHandle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddLambda(
        [State, TargetWindow](SWindow& Window, ISlateViewportProvider&)
        {
            if (&Window != TargetWindow) return;
            const double Wall = FPlatformTime::Seconds();
            FScopeLock Lock(&State->Mutex);
            ++State->Count;
            if (State->Recording) State->Rows.Add({State->Count, Wall});
        });
    Panel = SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
        [SNew(SImage).Image(&Brush).ColorAndOpacity_Lambda([this]()
            { return FSlateColor(ReadyAt > 0 ? FLinearColor::White : FLinearColor::Black); })]
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(12)
        [SNew(SBorder).BorderBackgroundColor(FLinearColor(0, 0, 0, .8f))
            [SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text_Lambda([this]()
            {
                return FText::FromString(FString::Printf(TEXT("%s / %s | tick %llu | wall %.6f | %s"),
                    *Case, *Label, TickNumber, FPlatformTime::Seconds(), SampleAt > 0 ? TEXT("SAMPLE") : TEXT("WARMUP")));
            })]];
    GEngine->GameViewport->AddViewportWidgetContent(Panel.ToSharedRef(), 100);
    FMediaPlayerOptions Options;
    Options.PlayOnOpen = EMediaPlayerOptionBooleanOverride::Disabled;
    Options.Loop = EMediaPlayerOptionBooleanOverride::Enabled;
    Options.Tracks.Audio = INDEX_NONE;
    Options.InternalCustomOptions.Add(MediaPlayerOptionValues::ImgMediaSmartCacheEnabled(), FVariant(false));
    if (!Source->Validate() || !Player->OpenSourceWithOptions(Source, Options))
    { Finish(false, TEXT("source validation/open rejected")); return; }
    UE_LOG(LogTemp, Display, TEXT("MediaBench OPEN case=%s label=%s source=%s"), *Case, *Label, *Source->GetUrl());
}

bool AMediaLoopBenchGameMode::FullCache() const
{
    if (!Player || Case != TEXT("sequence")) return false;
    const auto Native = Player->GetPlayerFacade()->GetPlayer();
    TRangeSet<FTimespan> Ranges;
    return Native.IsValid() && Native->GetCache().QueryCacheState(EMediaCacheState::Loaded, Ranges)
        && Ranges.Contains(TRange<FTimespan>(FTimespan::Zero(), FTimespan::FromSeconds(5)));
}

void AMediaLoopBenchGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished || !Player) return;
    const double Now = FPlatformTime::Seconds();
    const double WallDelta = Now - LastTick;
    LastTick = Now;
    ++TickNumber;
    if (Player->HasError()) { Finish(false, TEXT("media player error")); return; }
    if (ReadyAt == 0 && Now - OpenAt > 45)
    { Finish(false, FString::Printf(TEXT("ready timeout ready=%d playing=%d cache=%d size=%dx%d"),
        Player->IsReady(), Player->IsPlaying(), FullCache(), Texture->GetWidth(), Texture->GetHeight())); return; }
    const bool Cached = FullCache();
    if (!bPlayStarted && Player->IsReady() && (Case != TEXT("sequence") || Cached))
    {
        if (Case == TEXT("sequence") && FPlatformMemory::GetStats().AvailablePhysical < 3ull * 1024 * 1024 * 1024)
        { Finish(false, TEXT("less than 3GiB reserve after caching")); return; }
        if (Case == TEXT("sequence") && (FMath::Abs(Player->GetDuration().GetTotalSeconds() - 5.) > .001
            || FMath::Abs(Player->GetVideoTrackFrameRate(INDEX_NONE, INDEX_NONE) - 24.f) > .01f))
        { Finish(false, TEXT("sequence duration/fps mismatch")); return; }
        if (!Player->Play()) { Finish(false, TEXT("Play rejected")); return; }
        bPlayStarted = true;
        PlayAt = Now;
        UE_LOG(LogTemp, Display, TEXT("MediaBench PLAY full_cache=%d duration=%.6f fps=%.3f"), Cached,
            Player->GetDuration().GetTotalSeconds(), Player->GetVideoTrackFrameRate(INDEX_NONE, INDEX_NONE));
    }
    const auto Range = Player->GetPlayerFacade()->GetLastProcessedVideoSampleTimeRange();
    const bool Actual = !Range.IsEmpty() && Range.HasLowerBound() && Range.HasUpperBound();
    if (bPlayStarted && ReadyAt == 0 && Actual && Texture->GetWidth() == 1920 && Texture->GetHeight() == 1080)
    {
        if (!bFenceStarted) { FirstFrameFence.BeginFence(); bFenceStarted = true; }
        else if (FirstFrameFence.IsFenceComplete())
        { ReadyAt = Now; UE_LOG(LogTemp, Display, TEXT("MediaBench FIRST_FRAME wall=%.6f"), Now); }
    }
    if (ReadyAt > 0 && SampleAt == 0 && Now - ReadyAt >= 2.)
    {
        SampleAt = Now;
        FScopeLock Lock(&Present->Mutex);
        Present->Recording = true;
        UE_LOG(LogTemp, Display, TEXT("MediaBench SAMPLE_START wall=%.6f duration=%.3f"), Now, Duration);
    }
    if (Case == TEXT("sequence") && bPlayStarted && !bCacheChecked && Now - PlayAt >= 6.)
    {
        bCacheChecked = true;
        UE_LOG(LogTemp, Display, TEXT("MediaBench CACHE_AFTER_LOOP full=%d freeGiB=%.3f"), Cached,
            double(FPlatformMemory::GetStats().AvailablePhysical) / (1024. * 1024. * 1024.));
        if (!Cached) { Finish(false, TEXT("full cache not retained after first loop")); return; }
    }
    if (SampleAt == 0) return;
    uint64 Presents;
    { FScopeLock Lock(&Present->Mutex); Presents = Present->Count; }
    TickCsv += FString::Printf(TEXT("%llu,%.9f,%.6f,%.9f,%.9f,%d,%.9f,%.9f,%d,%d,%llu,%d,%d,%d\n"),
        TickNumber, Now, WallDelta * 1000, Now - SampleAt, Player->GetTime().GetTotalSeconds(), Actual,
        Actual ? Range.GetLowerBoundValue().Time.GetTotalSeconds() : -1.,
        Actual ? Range.GetUpperBoundValue().Time.GetTotalSeconds() : -1.,
        Actual ? Range.GetLowerBoundValue().GetSequenceIndex() : -1,
        Actual ? Range.GetLowerBoundValue().GetLoopIndex() : -1, Presents, Cached, Player->IsPlaying(), Player->HasError());
    if (Now - SampleAt >= Duration) Finish(true, TEXT("sampling duration reached"));
}

void AMediaLoopBenchGameMode::Finish(bool Success, const FString& Reason)
{
    if (bFinished) return;
    bFinished = true;
    FString Presents = TEXT("present,wall_s\n");
    if (Present)
    {
        FScopeLock Lock(&Present->Mutex);
        Present->Recording = false;
        for (const auto& Row : Present->Rows)
            Presents += FString::Printf(TEXT("%llu,%.9f\n"), Row.Number, Row.Wall);
    }
    bool Saved = !Output.IsEmpty();
    if (Saved)
    {
        Saved &= FFileHelper::SaveStringToFile(TickCsv, *FPaths::Combine(Output, TEXT("tick.csv")));
        Saved &= FFileHelper::SaveStringToFile(Presents, *FPaths::Combine(Output, TEXT("present.csv")));
        const FString Status = FString::Printf(TEXT("case=%s\nlabel=%s\nsuccess=%d\nreason=%s\nopen_wall=%.9f\nplay_wall=%.9f\nfirst_frame_wall=%.9f\nsample_wall=%.9f\nduration=%.3f\n"),
            *Case, *Label, Success, *Reason, OpenAt, PlayAt, ReadyAt, SampleAt, Duration);
        Saved &= FFileHelper::SaveStringToFile(Status, *FPaths::Combine(Output, TEXT("result.txt")));
    }
    UE_LOG(LogTemp, Display, TEXT("MediaBench %s saved=%d reason=%s output=%s"),
        Success && Saved ? TEXT("COMPLETE") : TEXT("FAILED"), Saved, *Reason, *Output);
    if (Player) Player->Close();
    FPlatformMisc::RequestExit(false);
}

void AMediaLoopBenchGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    if (!bFinished) Finish(false, TEXT("ended before completion"));
    if (FSlateApplication::IsInitialized() && PresentHandle.IsValid())
        FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(PresentHandle);
    if (GEngine && GEngine->GameViewport && Panel) GEngine->GameViewport->RemoveViewportWidgetContent(Panel.ToSharedRef());
    if (Player) Player->Close();
    Super::EndPlay(Reason);
}
