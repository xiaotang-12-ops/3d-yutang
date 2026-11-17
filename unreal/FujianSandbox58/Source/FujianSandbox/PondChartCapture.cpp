// 受控闭合厚片与固定正交捕获：旧512²紫段不变，显式RGB候选复用单RT捕获完整B背景。
// 所有组件随B窗口释放；失败保留原Slate，不修改A环境或全局渲染。-codex，2026-10-23
#include "PondChartCapture.h"
#include "PondPresentation.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "CoreGlobals.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "TextureResource.h"
#include "PipelineStateCache.h"
#include "ShaderPipelineCache.h"
#include "ShaderCore.h"
#include "HAL/IConsoleManager.h"

namespace
{
constexpr float Radius=300.f,InnerRadius=204.f,Bevel=2.f,SinElevation=.44f;
const FVector RigOrigin(0,0,-1000000);
}
FPondChartCapture::FPondChartCapture(UWorld* InWorld):World(InWorld)
{
    bRGB=FParse::Param(FCommandLine::Get(),TEXT("PondChartRGBExperiment"));
    bRGBBackgroundOnly=bRGB&&FParse::Param(FCommandLine::Get(),TEXT("PondChartRGBBackgroundOnly"));
    if(bRGB&&!FParse::Param(FCommandLine::Get(),TEXT("PondChartRGBDiagnostics")))return;
    if(!bRGB)
    if(!FParse::Param(FCommandLine::Get(),TEXT("PondChart3DDiagnostics")))return;
    // 诊断写盘必须有显式目录，不借用默认Saved路径；缺参仅禁用诊断，原实验单次捕获不变。-codex，2026-10-22
    if(!FParse::Value(FCommandLine::Get(),TEXT("PondCaptureDir="),DiagnosticDirectory)||DiagnosticDirectory.IsEmpty())
    {UE_LOG(LogTemp,Warning,TEXT("PondChart3D diagnostics disabled: explicit -PondCaptureDir required"));return;}
    DiagnosticDirectory=FPaths::ConvertRelativePathToFull(DiagnosticDirectory);
    bDiagnostics=IFileManager::Get().MakeDirectory(*DiagnosticDirectory,true);
    if(!bDiagnostics)UE_LOG(LogTemp,Warning,TEXT("PondChart3D diagnostics directory unavailable: %s"),*DiagnosticDirectory);
}
FPondChartCapture::~FPondChartCapture()
{
    if(Composite&&FSlateApplication::IsInitialized())FSlateApplication::Get().GetRenderer()->ReleaseDynamicResource(ImageBrush);
    ImageBrush.SetResourceObject(nullptr);
    if(IsValid(Rig.Get()))Rig->Destroy();
    Composite=nullptr;Surface=nullptr;Target=nullptr;Capture=nullptr;Mesh=nullptr;Rig=nullptr;
    UE_LOG(LogTemp,Display,TEXT("PondChart3D released captures=%u"),CaptureCount);
}
void FPondChartCapture::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(Rig);Collector.AddReferencedObject(Mesh);Collector.AddReferencedObject(Capture);
    Collector.AddReferencedObject(Target);Collector.AddReferencedObject(Surface);Collector.AddReferencedObject(Composite);
    Collector.AddReferencedObject(RGBBackground);Collector.AddReferencedObject(RGBStage);
    Collector.AddReferencedObject(RGBEdge);Collector.AddReferencedObject(RGBStageTexture);
}
const FSlateBrush* FPondChartCapture::BrushFor(uint32 Revision) const
{return ReadyRevision==Revision&&Composite?&ImageBrush:nullptr;}

void FPondChartCapture::ExportDiagnostic(uint32 Revision,const TCHAR* Phase)
{
    if(!bDiagnostics||!Target)return;
    const FString Filename=FPaths::Combine(DiagnosticDirectory,FString::Printf(TEXT("chart3d-rev-%u-%s.exr"),Revision,Phase));
    TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(*Filename));
    bool bSaved=false;
    if(Archive)
    {
        // 引擎EXR路径以RCM_MinMax读取RGBA16F，保留反向alpha；同步读回会扰动时序，不作性能样本。-codex，2026-10-22
        bSaved=FImageUtils::ExportRenderTarget2DAsEXR(Target.Get(),*Archive)&&!Archive->IsError();
        const bool bClosed=Archive->Close();bSaved=bSaved&&bClosed;
    }
    UE_LOG(LogTemp,Display,TEXT("PondChart3D diagnostic revision=%u phase=%s frame=%llu worldTime=%.6f saved=%d file=%s (readback; not a performance sample)"),
        Revision,Phase,GFrameCounter,World.IsValid()?World->GetTimeSeconds():-1.0,bSaved,*Filename);
}

bool FPondChartCapture::InitializeRGB()
{
    if(bInitializationAttempted)return IsValid(Capture.Get())&&Composite;
    bInitializationAttempted=true;
    UWorld* ActiveWorld=World.Get();if(!ActiveWorld)return false;
    Surface=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Dashboard/M_PondChartRGBSurface.M_PondChartRGBSurface"));
    auto* EdgeBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Dashboard/M_PondChartRGBEdge.M_PondChartRGBEdge"));
    auto* CompositeBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Dashboard/M_PondChartRGBComposite.M_PondChartRGBComposite"));
    RGBBackground=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Dashboard/M_PondChartRGBBackground.M_PondChartRGBBackground"));
    RGBStage=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Dashboard/M_PondChartRGBStage.M_PondChartRGBStage"));
    RGBStageTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Dashboard/T_PondStageEnvironment.T_PondStageEnvironment"));
    if(!Surface||!EdgeBase||!CompositeBase||!RGBBackground||!RGBStage||!RGBStageTexture
        ||!Surface->GetShadingModels().HasShadingModel(MSM_ThinTranslucent)
        ||!EdgeBase->GetShadingModels().HasShadingModel(MSM_ThinTranslucent)
        ||!CompositeBase->IsUIMaterial()||CompositeBase->GetBlendMode()!=BLEND_Opaque)
    {UE_LOG(LogTemp,Warning,TEXT("PondChartRGB unavailable/incompatible assets; original UI retained"));return false;}
#if WITH_EDITOR
    // 5.8按需模式PostLoad可留下无在途任务的不完整Map；先明确请求本批剩余shader，才有完整性可等。
    // 仅初始化一次，复用EnsureIsComplete的提交API但不调用同步FinishCompilation；不改全局cvar。-codex，2026-10-23
    const IConsoleVariable* JobCacheDDC=bDiagnostics?IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderCompiler.JobCacheDDC")):nullptr;
    const TArray<UMaterialInterface*> CompileMaterials={Surface.Get(),EdgeBase,RGBBackground.Get(),RGBStage.Get(),CompositeBase};
    for(UMaterialInterface* Material:CompileMaterials)
    {
        UMaterial* Base=Material?Material->GetMaterial():nullptr;
        FMaterialResource* Resource=Base?Base->GetMaterialResource(GMaxRHIShaderPlatform):nullptr;
        const bool bBeforeMap=Resource&&Resource->GetGameThreadShaderMap()!=nullptr;
        const bool bBeforeComplete=Resource&&Resource->IsGameThreadShaderMapComplete();
        const bool bBeforeFinished=bDiagnostics&&Resource&&Resource->IsCompilationFinished();
        const bool bBeforeCompiling=bDiagnostics&&Base&&Base->IsCompiling();
        const bool bRequestEligible=Resource&&bBeforeMap&&!bBeforeComplete;
        if(bRequestEligible)Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::ForceLocal);
        if(bDiagnostics)
        {
            UE_LOG(LogTemp,Display,TEXT("PondChartRGB shaderRequest material=%s platform=%d JobCacheDDC=%d resource=%d beforeMap=%d beforeComplete=%d beforeFinished=%d beforeBaseCompiling=%d apiCalled=%d afterMap=%d afterComplete=%d afterFinished=%d afterBaseCompiling=%d (void API cannot prove jobs enqueued; completion still gated)"),
                Base?*Base->GetName():TEXT("NULL"),int32(GMaxRHIShaderPlatform),JobCacheDDC?JobCacheDDC->GetInt():-1,
                Resource!=nullptr,bBeforeMap,bBeforeComplete,bBeforeFinished,bBeforeCompiling,bRequestEligible,
                Resource&&Resource->GetGameThreadShaderMap()!=nullptr,Resource&&Resource->IsGameThreadShaderMapComplete(),
                Resource&&Resource->IsCompilationFinished(),Base&&Base->IsCompiling());
            if(!bBeforeComplete&&!bRequestEligible)
                UE_LOG(LogTemp,Warning,TEXT("PondChartRGB shaderRequest API NOT called for %s: missing resource/map; original readiness/timeout gates retained"),Base?*Base->GetName():TEXT("NULL"));
        }
    }
#endif
    RGBEdge=UMaterialInstanceDynamic::Create(EdgeBase,GetTransientPackage());
    if(!RGBEdge)return false;
    RGBEdge->SetScalarParameterValue(TEXT("EdgeEmission"),FParse::Param(FCommandLine::Get(),TEXT("PondChartRGBNoEmission"))?0.f:7.5f);
    RGBStageTexture->SetForceMipLevelsToBeResident(60.f);
    Target=NewObject<UTextureRenderTarget2D>(GetTransientPackage());
    Target->ClearColor=FLinearColor(.00152f,.00913f,.02843f,1);
    Target->bAutoGenerateMips=false;
    // FinalColorLDR写入显示编码，sRGB纹理采样还原线性，再由Slate输出显示编码；不是旧HDR合成链。-codex，2026-10-23
    Target->InitCustomFormat(1920,1080,PF_B8G8R8A8,false);Target->UpdateResourceImmediate(true);
    Composite=UMaterialInstanceDynamic::Create(CompositeBase,GetTransientPackage());if(!Composite)return false;
    Composite->SetTextureParameterValue(TEXT("ChartCapture"),Target);
    ImageBrush.DrawAs=ESlateBrushDrawType::Image;ImageBrush.ImageSize=FVector2D(1920,1080);ImageBrush.SetResourceObject(Composite);
    ImageRect=FVector4f(0,0,1920,1080);
    FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Rig=ActiveWorld->SpawnActor<AActor>(AActor::StaticClass(),FTransform(RigOrigin),Spawn);if(!Rig)return false;
    Rig->SetActorEnableCollision(false);Rig->SetActorTickEnabled(false);
    Mesh=NewObject<UProceduralMeshComponent>(Rig);Rig->AddInstanceComponent(Mesh);Rig->SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);Mesh->SetAffectDynamicIndirectLighting(false);
    Mesh->SetAffectDistanceFieldLighting(false);Mesh->SetLightingChannels(false,false,true);Mesh->SetVisibleInSceneCaptureOnly(true);
    Mesh->RegisterComponent();Rig->SetActorLocation(RigOrigin);
    Capture=NewObject<USceneCaptureComponent2D>(Rig);Rig->AddInstanceComponent(Capture);Capture->SetupAttachment(Mesh);
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->bAlwaysPersistRenderingState=true;
    Capture->CaptureSource=SCS_FinalColorLDR;Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->TextureTarget=Target;Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->OrthoWidth=1920;
    Capture->MaxViewDistanceOverride=3000;Capture->bAutoCalculateOrthoPlanes=false;Capture->bUpdateOrthoPlanes=false;
    Capture->bUseRayTracingIfEnabled=false;Capture->ViewLightingChannels.bViewChannel0=false;Capture->ViewLightingChannels.bViewChannel2=true;
    auto& PP=Capture->PostProcessSettings;
    PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=0;
    PP.bOverride_BloomIntensity=true;PP.BloomIntensity=FParse::Param(FCommandLine::Get(),TEXT("PondChartRGBNoBloom"))?0.f:.16f;
    Capture->PostProcessBlendWeight=1;
    TArray<FEngineShowFlagsSetting> Flags;
    for(const TCHAR* Name:{TEXT("Atmosphere"),TEXT("Fog"),TEXT("SkyLighting"),TEXT("DirectionalLights"),TEXT("MotionBlur"),TEXT("TemporalAA"),TEXT("ScreenSpaceReflections"),TEXT("DynamicShadows")})
    {FEngineShowFlagsSetting Flag;Flag.ShowFlagName=Name;Flag.Enabled=false;Flags.Add(Flag);}
    Capture->SetShowFlagSettings(Flags);Capture->RegisterComponent();Capture->ShowOnlyComponent(Mesh);
    // 透明材质不受lighting channel隔离保证；远Rig与有限灯范围才是对A的空间隔离。-codex，2026-10-23
    for(int32 I=0;I<2;I++)
    {
        auto* Light=NewObject<UPointLightComponent>(Rig);Rig->AddInstanceComponent(Light);Light->SetupAttachment(Mesh);
        Light->SetMobility(EComponentMobility::Movable);Light->SetCastShadows(false);Light->SetLightingChannels(false,false,true);
        Light->ViewLightingChannels.bViewChannel0=false;Light->ViewLightingChannels.bViewChannel2=true;
        Light->SetAttenuationRadius(1100);Light->SetIntensityUnits(ELightUnits::Lumens);Light->SetIntensity(I==0?1800.f:650.f);
        Light->SetSourceRadius(I==0?18.f:32.f);Light->SetSourceLength(I==0?160.f:90.f);
        Light->SetLightColor(I==0?FLinearColor(.77f,.86f,1):FLinearColor(.4f,.6f,1),false);
        Light->SetRelativeLocation(I==0?FVector(160,-240,310):FVector(-260,100,160));Light->RegisterComponent();
    }
    UE_LOG(LogTemp,Display,TEXT("PondChartRGB initialized: FinalColorLDR 1920x1080 BGRA8 sRGB, nominalRT=8294400 bytes (7.91MiB); 7.91x pixels vs512sq, PP/transient peak NOT measured; backgroundOnly=%d"),bRGBBackgroundOnly);
    return true;
}

bool FPondChartCapture::RGBResourcesReady() const
{
    if(bDiagnostics)LogRGBReadiness();
    if(!Mesh||!Target||!Target->GetResource()||!RGBStageTexture
        ||!RGBStageTexture->GetResource()||!RGBStageTexture->IsFullyStreamedIn()||Mesh->IsPSOPrecaching())return false;
#if WITH_EDITOR
    if(RGBStageTexture->IsCompiling())return false;
#endif
    // ProceduralMesh没有CollectPSOPrecacheData覆盖，组件flag不能证明其PSO已准备。
    // 结合全局在途PSO门槛与实际捕获warmup；仍需PNG证明首个结果有效。-codex，2026-10-23
    if(PipelineStateCache::NumActivePrecacheRequests()>0||FShaderPipelineCache::NumPrecompilesRemaining()>0)return false;
    const TArray<UMaterialInterface*> Materials={Surface.Get(),RGBEdge.Get(),RGBBackground.Get(),RGBStage.Get(),Composite.Get()};
    for(UMaterialInterface* M:Materials)
    {
        if(!M||M->IsCompiling()||!M->IsComplete())return false;
        const auto* Base=M->GetMaterial();const auto* Resource=Base?Base->GetMaterialResource(GMaxRHIShaderPlatform):nullptr;
        if(!Resource||!Resource->GetGameThreadShaderMap()||!Resource->IsGameThreadShaderMapComplete())return false;
    }
    return true;
}

void FPondChartCapture::LogRGBReadiness() const
{
    if(!bDiagnostics||RGBReadinessLogCount>=30)return;
    // 仅显式诊断逐项观察原门槛；不FinishCompilation、不WaitForStreaming、不放松任何条件。
    // IsFullyStreamedIn受LOD设置约束，缺失optional mip会持续false；IsComplete在无资源时可能true。
    // 本日志最多30条/捕获对象，变化立即记录，其余最多每秒一条。-codex，2026-10-23
    bool bTextureCompiling=false;
#if WITH_EDITOR
    bTextureCompiling=RGBStageTexture&&RGBStageTexture->IsCompiling();
#endif
    const uint32 ActivePSO=PipelineStateCache::NumActivePrecacheRequests();
    const uint32 PipelineRemaining=FShaderPipelineCache::NumPrecompilesRemaining();
    FString State=FString::Printf(TEXT("revision=%u mesh=%d target=%d targetResource=%d texture=%s textureResource=%d textureCompiling=%d fullyStreamed=%d residentMips=%d totalMips=%d textureSize=%dx%d componentPSO=%d activePrecachePSO=%u shaderPipelineRemaining=%u shaderPlatform=%d"),
        RGBPendingRevision,Mesh!=nullptr,Target!=nullptr,Target&&Target->GetResource()!=nullptr,
        RGBStageTexture?*RGBStageTexture->GetName():TEXT("NULL"),RGBStageTexture&&RGBStageTexture->GetResource()!=nullptr,bTextureCompiling,
        RGBStageTexture&&RGBStageTexture->IsFullyStreamedIn(),RGBStageTexture?RGBStageTexture->GetNumResidentMips():-1,
        RGBStageTexture?RGBStageTexture->GetNumMips():-1,RGBStageTexture?RGBStageTexture->GetSizeX():-1,
        RGBStageTexture?RGBStageTexture->GetSizeY():-1,Mesh&&Mesh->IsPSOPrecaching(),ActivePSO,PipelineRemaining,int32(GMaxRHIShaderPlatform));
    const TArray<UMaterialInterface*> Materials={Surface.Get(),RGBEdge.Get(),RGBBackground.Get(),RGBStage.Get(),Composite.Get()};
    for(const UMaterialInterface* M:Materials)
    {
        const auto* Base=M?M->GetMaterial():nullptr;
        const auto* Resource=Base?Base->GetMaterialResource(GMaxRHIShaderPlatform):nullptr;
        State+=FString::Printf(TEXT(" | material=%s compiling=%d complete=%d base=%s baseCompiling=%d baseComplete=%d resource=%d shaderMap=%d shaderMapComplete=%d"),
            M?*M->GetName():TEXT("NULL"),M&&M->IsCompiling(),M&&M->IsComplete(),Base?*Base->GetName():TEXT("NULL"),
            Base&&Base->IsCompiling(),Base&&Base->IsComplete(),Resource!=nullptr,Resource&&Resource->GetGameThreadShaderMap()!=nullptr,
            Resource&&Resource->IsGameThreadShaderMapComplete());
    }
    const double Now=FPlatformTime::Seconds();
    if(State!=RGBLastReadinessState||Now-RGBLastReadinessLogTime>=1)
    {
        RGBLastReadinessState=State;RGBLastReadinessLogTime=Now;RGBReadinessLogCount++;
        UE_LOG(LogTemp,Display,TEXT("PondChartRGB readiness[%u/30] elapsed=%.2f boundedCaptures=%u %s"),
            RGBReadinessLogCount,RGBWaitStarted>0?Now-RGBWaitStarted:0,RGBCapturesForRevision,*State);
    }
}

bool FPondChartCapture::BuildRGB(const FPondSnapshot& S)
{
    const float CosElevation=FMath::Sqrt(1-SinElevation*SinElevation),Depth=24/CosElevation;
    const FVector Right(-1,0,0),Up(0,SinElevation,CosElevation),TowardCamera(0,-CosElevation,SinElevation);
    const FVector Aim=RigOrigin-Up*108;
    Capture->SetWorldLocationAndRotation(Aim+TowardCamera*1400,(-TowardCamera).Rotation());
    Mesh->ClearAllMeshSections();
    auto Plane=[&](int32 Index,float X,float Y,float W,float H,float Distance,UMaterialInterface* Material)
    {
        auto P=[&](float PX,float PY){return -Up*108+Right*(PX-960)+Up*(540-PY)-TowardCamera*Distance;};
        TArray<FVector> V={P(X,Y),P(X+W,Y),P(X+W,Y+H),P(X,Y+H)};
        TArray<FVector> N={TowardCamera,TowardCamera,TowardCamera,TowardCamera};
        TArray<FVector2D> UV={{0,0},{1,0},{1,1},{0,1}};
        TArray<FLinearColor> C={FLinearColor::White,FLinearColor::White,FLinearColor::White,FLinearColor::White};
        TArray<FProcMeshTangent> T;for(int32 I=0;I<4;I++)T.Add(FProcMeshTangent(Right,false));
        const bool Reverse=FVector::DotProduct(FVector::CrossProduct(V[1]-V[0],V[2]-V[0]),TowardCamera)>0;
        TArray<int32> Indices=Reverse?TArray<int32>{0,2,1,0,3,2}:TArray<int32>{0,1,2,0,2,3};
        Mesh->CreateMeshSection_LinearColor(Index,V,Indices,N,UV,C,T,false);Mesh->SetMaterial(Index,Material);
    };
    // 环最远深度包络<282；两平面置于包络后并留裕量，避免不透明背景穿入后半环。正交投影尺寸不变。-codex，2026-10-23
    Plane(5,0,0,1920,1080,440,RGBBackground);
    Plane(6,260,320,1400,1400.f*721/2182,360,RGBStage);
    if(!bRGBBackgroundOnly)
    {
        const FVector2D Section[]={{InnerRadius+Bevel,0},{Radius-Bevel,0},{Radius,-Bevel},{Radius,-Depth+Bevel},
            {Radius-Bevel,-Depth},{InnerRadius+Bevel,-Depth},{InnerRadius,-Depth+Bevel},{InnerRadius,-Bevel}};
        TArray<FVector> EdgeVertices,EdgeNormals;TArray<int32> EdgeIndices;TArray<FVector2D> EdgeUV;
        TArray<FLinearColor> EdgeColors;TArray<FProcMeshTangent> EdgeTangents;
        double Start=-PI*.5;
        for(int32 J=0;J<S.Varieties.Num();J++)
        {
            const auto& Variety=S.Varieties[J];if(Variety.Value<=0)continue;
            const double Span=2*PI*double(Variety.Value)/S.Harvest;if(!FMath::IsFinite(Span)||Span<=0)return false;
            const float Gap=FMath::Min(.025f,float(Span)*.12f),Begin=float(Start)+Gap,End=float(Start+Span)-Gap;Start+=Span;
            const FColor Palette[]={FColor(42,179,255),FColor(122,113,245),FColor(37,210,193),FColor(87,211,255)};
            const FLinearColor Color=FLinearColor(Palette[J%4]);
            TArray<FVector> V,N;TArray<int32> I;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> T;
            auto Position=[](float A,FVector2D Cross){return FVector(-Cross.X*FMath::Cos(A),-Cross.X*FMath::Sin(A),Cross.Y);};
            auto Face=[&](const TArray<FVector>& Points,FVector Normal,bool bEdge)
            {
                auto& Vertices=bEdge?EdgeVertices:V;auto& Normals=bEdge?EdgeNormals:N;auto& Indices=bEdge?EdgeIndices:I;
                auto& UVs=bEdge?EdgeUV:UV;auto& Colors=bEdge?EdgeColors:C;auto& Tangents=bEdge?EdgeTangents:T;
                const int32 Base=Vertices.Num();Normal.Normalize();
                const bool Reverse=FVector::DotProduct(FVector::CrossProduct(Points[1]-Points[0],Points[2]-Points[0]),Normal)>0;
                for(const auto& Point:Points){Vertices.Add(Point);Normals.Add(Normal);UVs.Add(FVector2D::ZeroVector);Colors.Add(Color);Tangents.Add(FProcMeshTangent((Points[1]-Points[0]).GetSafeNormal(),false));}
                for(int32 K=1;K<Points.Num()-1;K++){Indices.Add(Base);Indices.Add(Base+(Reverse?K+1:K));Indices.Add(Base+(Reverse?K:K+1));}
            };
            const int32 Count=FMath::Max(1,FMath::CeilToInt((End-Begin)*64));
            for(int32 K=0;K<Count;K++)
            {
                const float A=FMath::Lerp(Begin,End,float(K)/Count),B=FMath::Lerp(Begin,End,float(K+1)/Count),Mid=(A+B)*.5;
                const FVector Radial(-FMath::Cos(Mid),-FMath::Sin(Mid),0);
                for(int32 E=0;E<8;E++)
                {
                    const auto Q=Section[E],R=Section[(E+1)%8],D=R-Q;
                    Face({Position(A,Q),Position(B,Q),Position(B,R),Position(A,R)},Radial*(-D.Y)+FVector::UpVector*D.X,E%2==1);
                }
            }
            for(int32 Cut=0;Cut<2;Cut++)
            {
                const float A=Cut==0?Begin:End;TArray<FVector> Points;for(auto Q:Section)Points.Add(Position(A,Q));
                Face(Points,FVector(FMath::Sin(A),-FMath::Cos(A),0)*(Cut==0?-1:1),false);
            }
            Mesh->CreateMeshSection_LinearColor(J,V,I,N,UV,C,T,false);Mesh->SetMaterial(J,Surface);
        }
        Mesh->CreateMeshSection_LinearColor(4,EdgeVertices,EdgeIndices,EdgeNormals,EdgeUV,EdgeColors,EdgeTangents,false);Mesh->SetMaterial(4,RGBEdge);
    }
    Mesh->PrecachePSOs();
    return true;
}

bool FPondChartCapture::ExportRGBDiagnostic(uint32 Revision)
{
    if(!bDiagnostics)return true;
    // 显式诊断才同步读回；fence/资源就绪不能证明实际像素，本门槛只排除空白，不证明审美。-codex，2026-10-23
    TArray<FColor> Pixels;
    const bool bRead=Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
    uint32 Visible=0,Range=0,Changed=0,Bright=0;
    if(bRead&&Pixels.Num()==1920*1080)
    {for(const auto& P:Pixels){const uint8 Peak=FMath::Max3(P.R,P.G,P.B);if(Peak>12)Visible++;if(Peak>128)Bright++;
        if(Peak-FMath::Min3(P.R,P.G,P.B)>12)Range++;
        const auto& First=Pixels[0];if(FMath::Abs(int32(P.R)-First.R)+FMath::Abs(int32(P.G)-First.G)+FMath::Abs(int32(P.B)-First.B)>8)Changed++;}}
    const bool bValid=bRead&&Pixels.Num()==1920*1080&&Visible>10000&&Range>10000&&Changed>20000&&Bright>1000;
    const FString Filename=FPaths::Combine(DiagnosticDirectory,FString::Printf(TEXT("chartRGB-rev-%u-%s.png"),Revision,bRGBBackgroundOnly?TEXT("background"):TEXT("final")));
    TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(*Filename));bool bSaved=false;
    if(Archive){bSaved=FImageUtils::ExportRenderTarget2DAsPNG(Target,*Archive)&&!Archive->IsError();bSaved=Archive->Close()&&bSaved;}
    UE_LOG(LogTemp,Display,TEXT("PondChartRGB diagnostic revision=%u validNonblankOnly=%d visible=%u chromatic=%u changed=%u bright=%u saved=%d file=%s; ring/shader correctness requires visual inspection; SYNCHRONOUS READBACK, NOT performance sample"),Revision,bValid,Visible,Range,Changed,Bright,bSaved,*Filename);
    return bValid&&bSaved;
}

void FPondChartCapture::UpdateRGB(const FPondSnapshot& S,float TransitionTime)
{
    if(S.Revision!=RGBPendingRevision)
    {
        ReadyRevision=0;RGBPendingRevision=S.Revision;RGBCapturesForRevision=0;RGBWaitStarted=0;
        // 已提交旧命令不强制阻塞取消；等其fence后再改同一RT，BrushFor始终受revision约束。-codex，2026-10-23
    }
    if(bRGBWaitingFence)
    {if(!RGBFence.IsFenceComplete())return;bRGBWaitingFence=false;}
    if(TransitionTime<4||S.Revision==0||ReadyRevision==S.Revision||AttemptedRevision==S.Revision)return;
    if(S.Site!=INDEX_NONE||S.Harvest<=0||S.Varieties.Num()!=4||!S.Error.IsEmpty())return;
    const double Now=FPlatformTime::Seconds();
    if(RGBWaitStarted==0)
    {
        RGBWaitStarted=Now;
        if(!InitializeRGB()||!BuildRGB(S)){AttemptedRevision=S.Revision;return;}
        RGBStageTexture->SetForceMipLevelsToBeResident(60.f);
        UE_LOG(LogTemp,Display,TEXT("PondChartRGB pending revision=%u: resources/shaderMap/PSO -> 3 bounded captures -> RHI fence; no current brush"),S.Revision);
    }
    if(Now-RGBWaitStarted>30)
    {AttemptedRevision=S.Revision;UE_LOG(LogTemp,Warning,TEXT("PondChartRGB timeout revision=%u captures=%u; original UI retained"),S.Revision,RGBCapturesForRevision);return;}
    if(!RGBResourcesReady()||Now-RGBLastCaptureTime<.1)return;
    if(RGBCapturesForRevision<3)
    {
        Capture->CaptureScene();CaptureCount++;RGBCapturesForRevision++;RGBLastCaptureTime=Now;
        RGBFence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);bRGBWaitingFence=true;
        UE_LOG(LogTemp,Display,TEXT("PondChartRGB submit revision=%u bounded=%u/3 total=%u"),S.Revision,RGBCapturesForRevision,CaptureCount);return;
    }
    AttemptedRevision=S.Revision;
    if(!ExportRGBDiagnostic(S.Revision)){UE_LOG(LogTemp,Warning,TEXT("PondChartRGB invalid diagnostic; original UI retained"));return;}
    ReadyRevision=S.Revision;
    UE_LOG(LogTemp,Display,TEXT("PondChartRGB ready revision=%u after resources + boundedWarmup + RHI fence; readbackValidated=%d; static switch/pop unresolved; GPU peak unmeasured"),S.Revision,bDiagnostics);
}

bool FPondChartCapture::Initialize()
{
    if(bInitializationAttempted)return IsValid(Capture.Get())&&Composite;
    bInitializationAttempted=true;
    UWorld* ActiveWorld=World.Get();if(!ActiveWorld)return false;
    const bool bNormalExperiment=FParse::Param(FCommandLine::Get(),TEXT("PondChart3DExperiment"))
        &&FParse::Param(FCommandLine::Get(),TEXT("PondChartNormalExperiment"));
    Surface=LoadObject<UMaterialInterface>(nullptr,bNormalExperiment
        ?TEXT("/Game/Dashboard/M_PondChartNormalSurface.M_PondChartNormalSurface")
        :TEXT("/Game/Dashboard/M_PondChartSurface.M_PondChartSurface"));
    auto* CompositeBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Dashboard/M_PondChartComposite.M_PondChartComposite"));
    const bool bSurfaceCompatible=Surface&&!Surface->IsUIMaterial()&&(bNormalExperiment
        ?Surface->GetBlendMode()==BLEND_Opaque&&Surface->GetShadingModels().HasOnlyShadingModel(MSM_Unlit)
        :Surface->GetBlendMode()==BLEND_Translucent);
    if(!bSurfaceCompatible||!CompositeBase||!CompositeBase->IsUIMaterial()||CompositeBase->GetBlendMode()!=BLEND_Translucent)
    {UE_LOG(LogTemp,Warning,TEXT("PondChart3D assets unavailable/incompatible; retaining R2 purple sector"));return false;}
    if(bNormalExperiment)
    {
        // 仅初始化创建一次MID，并复用已有Surface的GC持有；均匀色先验证Opaque反向alpha合成。
        // 原Rig只有平移；材质减去固定RigOrigin后即局部坐标，未来旋转/缩放必须同步改坐标转换。-codex，2026-10-23
        auto* NormalMaterial=UMaterialInstanceDynamic::Create(Surface.Get(),GetTransientPackage());
        if(!NormalMaterial)return false;
        const bool bCoverageOnly=FParse::Param(FCommandLine::Get(),TEXT("PondChartCoverageOnly"));
        NormalMaterial->SetScalarParameterValue(TEXT("CoverageOnly"),bCoverageOnly?1.f:0.f);
        Surface=NormalMaterial;
        UE_LOG(LogTemp,Display,TEXT("PondChartNormal candidate ready: opaque unlit coverageOnly=%d; fixed rig translation (0,0,-1000000), original capture timing retained"),bCoverageOnly);
    }
    Target=NewObject<UTextureRenderTarget2D>(GetTransientPackage());
    Target->ClearColor=FLinearColor(0,0,0,1);Target->bAutoGenerateMips=false;
    Target->InitCustomFormat(512,512,PF_FloatRGBA,true);Target->UpdateResourceImmediate(true);
    Composite=UMaterialInstanceDynamic::Create(CompositeBase,GetTransientPackage());
    if(!Composite)return false;
    Composite->SetTextureParameterValue(TEXT("ChartCapture"),Target);
    ImageBrush.DrawAs=ESlateBrushDrawType::Image;ImageBrush.ImageSize=FVector2D(512,512);ImageBrush.SetResourceObject(Composite);
    FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Rig=ActiveWorld->SpawnActor<AActor>(AActor::StaticClass(),FTransform(RigOrigin),Spawn);if(!Rig)return false;
    Rig->SetActorEnableCollision(false);Rig->SetActorTickEnabled(false);
    Mesh=NewObject<UProceduralMeshComponent>(Rig);Rig->AddInstanceComponent(Mesh);Rig->SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);Mesh->SetAffectDynamicIndirectLighting(false);
    Mesh->SetAffectDistanceFieldLighting(false);Mesh->SetLightingChannels(false,false,true);Mesh->SetVisibleInSceneCaptureOnly(true);
    Mesh->RegisterComponent();Rig->SetActorLocation(RigOrigin);Mesh->SetMaterial(0,Surface);
    Capture=NewObject<USceneCaptureComponent2D>(Rig);Rig->AddInstanceComponent(Capture);Capture->SetupAttachment(Mesh);
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->bAlwaysPersistRenderingState=false;
    Capture->CaptureSource=SCS_SceneColorHDR;Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->TextureTarget=Target;Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->MaxViewDistanceOverride=3000;
    Capture->bAutoCalculateOrthoPlanes=false;Capture->bUpdateOrthoPlanes=false;Capture->bUseRayTracingIfEnabled=false;
    Capture->ViewLightingChannels.bViewChannel0=false;Capture->ViewLightingChannels.bViewChannel2=true;
    TArray<FEngineShowFlagsSetting> Flags;
    for(const TCHAR* Name:{TEXT("Atmosphere"),TEXT("Fog"),TEXT("SkyLighting"),TEXT("Bloom"),TEXT("MotionBlur"),TEXT("TemporalAA"),TEXT("ScreenSpaceReflections"),TEXT("DynamicShadows")})
    {FEngineShowFlagsSetting Flag;Flag.ShowFlagName=Name;Flag.Enabled=false;Flags.Add(Flag);}
    Capture->SetShowFlagSettings(Flags);Capture->RegisterComponent();Capture->ShowOnlyComponent(Mesh);
    // ShowOnly不能隔离灯：额外使用独占view/primitive通道与有限半径，避免影响A主视图。-codex，2026-10-22
    for(int32 LightIndex=0;LightIndex<2;LightIndex++)
    {
        auto* Light=NewObject<UPointLightComponent>(Rig);Rig->AddInstanceComponent(Light);Light->SetupAttachment(Mesh);
        Light->SetMobility(EComponentMobility::Movable);Light->SetCastShadows(false);Light->SetLightingChannels(false,false,true);
        Light->ViewLightingChannels.bViewChannel0=false;Light->ViewLightingChannels.bViewChannel2=true;
        Light->SetAttenuationRadius(1100);Light->SetIntensityUnits(ELightUnits::Lumens);Light->SetIntensity(LightIndex==0?1800.f:650.f);
        Light->SetSourceRadius(LightIndex==0?18.f:32.f);
        Light->SetLightColor(LightIndex==0?FLinearColor(.77f,.86f,1):FLinearColor(.4f,.6f,1),false);
        Light->SetRelativeLocation(LightIndex==0?FVector(160,-240,310):FVector(-260,100,160));Light->RegisterComponent();
    }
    return true;
}

void FPondChartCapture::Update(const FPondSnapshot& Snapshot,float TransitionTime)
{
    if(bRGB){UpdateRGB(Snapshot,TransitionTime);return;}
    if(bAfterPending&&Snapshot.Revision!=PendingRevision)bAfterPending=false;
    if(bAfterPending&&TransitionTime>=4.f&&Snapshot.Revision==PendingRevision&&World.IsValid()
        &&GFrameCounter>BeforeFrame+1&&World->GetTimeSeconds()>BeforeWorldTime)
    {
        // 两次之间必须经过完整引擎帧且世界时间推进；仅补这一回，不用持续捕获掩盖首帧问题。-codex，2026-10-22
        bAfterPending=false;
        if(IsValid(Capture.Get()))
        {
            Capture->CaptureScene();CaptureCount++;
            UE_LOG(LogTemp,Display,TEXT("PondChart3D diagnostic recapture revision=%u size=512x512 count=%u frame=%llu beforeFrame=%llu"),
                PendingRevision,CaptureCount,GFrameCounter,BeforeFrame);
            ExportDiagnostic(PendingRevision,TEXT("after"));
        }
    }
    if(TransitionTime<4.f||Snapshot.Revision==0||Snapshot.Revision==AttemptedRevision)return;
    AttemptedRevision=Snapshot.Revision;ReadyRevision=0;
    if(Snapshot.Site!=INDEX_NONE||Snapshot.Harvest<=0||Snapshot.Varieties.Num()<2||!Snapshot.Error.IsEmpty())return;
    const double Start=-PI*.5+2*PI*double(Snapshot.Varieties[0].Value)/Snapshot.Harvest;
    const double Span=2*PI*double(Snapshot.Varieties[1].Value)/Snapshot.Harvest;
    if(!FMath::IsFinite(Span)||Span<=.0001)return;
    const float Gap=FMath::Min(.025f,float(Span)*.12f),Begin=float(Start)+Gap,End=float(Start+Span)-Gap;
    if(End<=Begin||!Initialize())return;
    const float CosElevation=FMath::Sqrt(1-SinElevation*SinElevation),Depth=24/CosElevation;
    const FVector2D Section[]={{InnerRadius+Bevel,0},{Radius-Bevel,0},{Radius,-Bevel},{Radius,-Depth+Bevel},
        {Radius-Bevel,-Depth},{InnerRadius+Bevel,-Depth},{InnerRadius,-Depth+Bevel},{InnerRadius,-Bevel}};
    const int32 Segments=FMath::Clamp(FMath::CeilToInt((End-Begin)*48),1,302);
    TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UVs;TArray<FProcMeshTangent> Tangents;TArray<FLinearColor> Colors;
    FVector2D Minimum(DBL_MAX,DBL_MAX),Maximum(-DBL_MAX,-DBL_MAX);
    auto Position=[](float Angle,FVector2D Cross){return FVector(-Cross.X*FMath::Cos(Angle),-Cross.X*FMath::Sin(Angle),Cross.Y);};
    auto AddFace=[&](const TArray<FVector>& Face,FVector Expected)
    {
        const int32 Base=Vertices.Num();const FVector Normal=Expected.GetSafeNormal();
        // 与引擎GenerateBoxMesh一致：UE正面绕序的数学叉积背离外法线，不能照搬右手CCW。-codex，2026-10-22
        const bool Reverse=FVector::DotProduct(FVector::CrossProduct(Face[1]-Face[0],Face[2]-Face[0]),Normal)>0;
        for(const FVector& Vertex:Face)
        {
            Vertices.Add(Vertex);Normals.Add(Normal);UVs.Add(FVector2D::ZeroVector);Colors.Add(FLinearColor::White);
            Tangents.Add(FProcMeshTangent((Face[1]-Face[0]).GetSafeNormal(),false));
            const FVector2D Projected(-Vertex.X,-SinElevation*Vertex.Y-CosElevation*Vertex.Z);
            Minimum.X=FMath::Min(Minimum.X,Projected.X);Minimum.Y=FMath::Min(Minimum.Y,Projected.Y);
            Maximum.X=FMath::Max(Maximum.X,Projected.X);Maximum.Y=FMath::Max(Maximum.Y,Projected.Y);
        }
        for(int32 Triangle=1;Triangle<Face.Num()-1;Triangle++)
        {Indices.Add(Base);Indices.Add(Base+(Reverse?Triangle+1:Triangle));Indices.Add(Base+(Reverse?Triangle:Triangle+1));}
    };
    for(int32 Segment=0;Segment<Segments;Segment++)
    {
        const float Angle0=FMath::Lerp(Begin,End,float(Segment)/Segments),Angle1=FMath::Lerp(Begin,End,float(Segment+1)/Segments),Middle=(Angle0+Angle1)*.5f;
        const FVector Radial(-FMath::Cos(Middle),-FMath::Sin(Middle),0);
        for(int32 Edge=0;Edge<8;Edge++)
        {
            const FVector2D Cross0=Section[Edge],Cross1=Section[(Edge+1)%8],Delta=Cross1-Cross0;
            AddFace({Position(Angle0,Cross0),Position(Angle1,Cross0),Position(Angle1,Cross1),Position(Angle0,Cross1)},Radial*(-Delta.Y)+FVector::UpVector*Delta.X);
        }
    }
    for(int32 Cut=0;Cut<2;Cut++)
    {
        const float Angle=Cut==0?Begin:End;TArray<FVector> Face;
        for(const FVector2D Cross:Section)Face.Add(Position(Angle,Cross));
        AddFace(Face,FVector(FMath::Sin(Angle),-FMath::Cos(Angle),0)*(Cut==0?-1:1));
    }
    Mesh->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UVs,Colors,Tangents,false);
    const FVector2D Center=(Minimum+Maximum)*.5;const float Size=float(FMath::Max(Maximum.X-Minimum.X,Maximum.Y-Minimum.Y))+12.f;
    ImageRect=FVector4f(960+Center.X-Size*.5,432+Center.Y-Size*.5,Size,Size);
    const FVector Right(-1,0,0),Up(0,SinElevation,CosElevation),TowardCamera(0,-CosElevation,SinElevation);
    const FVector Aim=RigOrigin+Right*Center.X-Up*Center.Y;
    const FVector CameraPosition=Aim+TowardCamera*1400;
    Capture->OrthoWidth=Size;Capture->SetWorldLocationAndRotation(CameraPosition,(Aim-CameraPosition).Rotation());
    const double Before=FPlatformTime::Seconds();Capture->CaptureScene();
    ReadyRevision=Snapshot.Revision;CaptureCount++;
    // 此耗时是提交/调用CPU耗时，不能冒充GPU测量；计数可验证停稳后不再捕获。-codex，2026-10-22
    UE_LOG(LogTemp,Display,TEXT("PondChart3D capture revision=%u size=512x512 count=%u vertices=%d triangles=%d cpuSubmitMs=%.3f"),
        ReadyRevision,CaptureCount,Vertices.Num(),Indices.Num()/3,(FPlatformTime::Seconds()-Before)*1000);
    if(bDiagnostics)
    {
        PendingRevision=Snapshot.Revision;BeforeFrame=GFrameCounter;BeforeWorldTime=World->GetTimeSeconds();
        ExportDiagnostic(Snapshot.Revision,TEXT("before"));bAfterPending=true;
    }
}
