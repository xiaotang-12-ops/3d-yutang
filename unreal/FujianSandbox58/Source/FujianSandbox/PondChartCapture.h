// 受控静态捕获：保留旧单紫段实验，RGB候选仅显式开启；复用同世界，不接管业务选择/动画。-codex，2026-10-23
#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/GCObject.h"
#include "RenderCommandFence.h"
class UWorld;
class AActor;
class UProceduralMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
struct FPondSnapshot;

class FPondChartCapture final : public FGCObject
{
public:
    explicit FPondChartCapture(UWorld* InWorld);
    virtual ~FPondChartCapture() override;
    void Update(const FPondSnapshot& Snapshot,float TransitionTime);
    const FSlateBrush* BrushFor(uint32 Revision) const;
    FVector4f Rect() const {return ImageRect;}
    bool IsRGB() const {return bRGB;}
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override {return TEXT("FPondChartCapture.StaticExperiment");}
private:
    bool Initialize();
    bool InitializeRGB();
    bool RGBResourcesReady() const;
    void LogRGBReadiness() const;
    bool BuildRGB(const FPondSnapshot& Snapshot);
    void UpdateRGB(const FPondSnapshot& Snapshot,float TransitionTime);
    bool ExportRGBDiagnostic(uint32 Revision);
    void ExportDiagnostic(uint32 Revision,const TCHAR* Phase);
    TWeakObjectPtr<UWorld> World;
    TObjectPtr<AActor> Rig=nullptr;
    TObjectPtr<UProceduralMeshComponent> Mesh=nullptr;
    TObjectPtr<USceneCaptureComponent2D> Capture=nullptr;
    TObjectPtr<UTextureRenderTarget2D> Target=nullptr;
    TObjectPtr<UMaterialInterface> Surface=nullptr;
    TObjectPtr<UMaterialInstanceDynamic> Composite=nullptr;
    TObjectPtr<UMaterialInterface> RGBBackground=nullptr,RGBStage=nullptr;
    TObjectPtr<UMaterialInstanceDynamic> RGBEdge=nullptr;
    TObjectPtr<UTexture2D> RGBStageTexture=nullptr;
    FRenderCommandFence RGBFence;
    bool bRGB=false,bRGBBackgroundOnly=false,bRGBWaitingFence=false;
    uint32 RGBPendingRevision=0,RGBCapturesForRevision=0;
    double RGBWaitStarted=0,RGBLastCaptureTime=0;
    mutable FString RGBLastReadinessState;
    mutable double RGBLastReadinessLogTime=0;
    mutable uint32 RGBReadinessLogCount=0;
    FSlateBrush ImageBrush;
    FVector4f ImageRect=FVector4f(0,0,0,0);
    uint32 AttemptedRevision=0,ReadyRevision=0,CaptureCount=0;
    bool bInitializationAttempted=false;
    bool bDiagnostics=false,bAfterPending=false;
    FString DiagnosticDirectory;
    uint32 PendingRevision=0;
    uint64 BeforeFrame=0;
    double BeforeWorldTime=0;
};
