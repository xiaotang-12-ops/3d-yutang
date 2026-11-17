// 原生世界中的一次构建示意塘；固定种子保持岸线和石块稳定，Tick只更新整体姿态。
// 完整岸水底示意模型，非实测塘界或水深；本文件不得连接真实业务或修改第一版。-codex，2026-10-30
#include "MotionStage.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "IMediaPlayer.h"
#include "MediaTexture.h"
#include "ImgMediaSource.h"
#include "ImgMediaSettings.h"
#include "IMediaCache.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Framework/Application/SlateApplication.h"
#if WITH_EDITOR
#include "MaterialShared.h"
#include "RHIShaderPlatform.h"
#endif

namespace MotionStageGeometry
{
constexpr int32 Segments = 128;
constexpr float WaterZ = 28.f;
constexpr float BedZ = -89.f;

// 每个三角形拥有独立法线/切线，保留岩土折面；UV根据真实表面方向投影，避免竖壁纹理被压扁。-codex，2026-10-23
struct FSurface
{
    TArray<FVector> Vertices, Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    void Triangle(const FVector& A, const FVector& B, const FVector& C,
        const FLinearColor& Color, bool bNormalizedWaterUV = false)
    {
        const FVector N = FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        if (N.IsNearlyZero()) return;
        const bool bTop = FMath::Abs(N.Z) > .55;
        const bool bSideX = FMath::Abs(N.X) > FMath::Abs(N.Y);
        const FVector UAxis = bTop || !bSideX ? FVector::XAxisVector : FVector::YAxisVector;
        const FVector T = (UAxis - N * FVector::DotProduct(UAxis,N)).GetSafeNormal();
        const FVector VAxis = bTop ? FVector::YAxisVector : FVector::ZAxisVector;
        const bool bFlipTangentY = FVector::DotProduct(FVector::CrossProduct(N,T),VAxis)<0.;
        const int32 Base = Vertices.Num();
        for (const FVector& P : {A,B,C})
        {
            Vertices.Add(P); Normals.Add(N); Colors.Add(Color);
            Tangents.Add(FProcMeshTangent(T,bFlipTangentY));
            UVs.Add(bNormalizedWaterUV ? FVector2D(P.X/1500.+.5,P.Y/950.+.5) :
                FVector2D(FVector::DotProduct(P,UAxis)/200., (bTop ? P.Y : P.Z)/200.));
        }
        // UE前面绕序与几何法线相反；法线保持朝外，索引用反序。-codex，2026-10-23
        Indices.Append({Base,Base+2,Base+1});
    }
    void Quad(const FVector& A,const FVector& B,const FVector& C,const FVector& D,
        const FLinearColor& Color)
    { Triangle(A,B,C,Color); Triangle(A,C,D,Color); }
    void Submit(UProceduralMeshComponent* Component,int32 Section=0) const
    {
        Component->CreateMeshSection_LinearColor(Section,Vertices,Indices,Normals,UVs,Colors,Tangents,false,false);
    }
};

FVector Outline(int32 I,float Scale,float Z)
{
    const float A = 2.f*PI*static_cast<float>(I)/Segments;
    const float C = FMath::Cos(A), S = FMath::Sin(A);
    const float Warp = 1.f + .025f*FMath::Sin(3.f*A+.7f) + .018f*FMath::Sin(7.f*A-1.1f);
    // 超椭圆不是地图边界；低频扰动只产生轮廓不规则，固定于构建时。-codex，2026-10-23
    return FVector(740.f*FMath::Sign(C)*FMath::Pow(FMath::Abs(C),.53f)*Scale*Warp,
        460.f*FMath::Sign(S)*FMath::Pow(FMath::Abs(S),.58f)*Scale*Warp,Z);
}

void AddStone(FSurface& Surface, FRandomStream& Random, const FVector& Center, const FVector& Size)
{
    constexpr int32 Sides=9;
    FVector Lower[Sides],Upper[Sides],Crown[Sides];
    const float Phase=Random.FRandRange(0.f,2.f*PI);
    for(int32 I=0;I<Sides;++I)
    {
        const float A=Phase+2.f*PI*I/Sides;
        const float R=Random.FRandRange(.82f,1.12f);
        Lower[I]=Center+FVector(FMath::Cos(A)*Size.X*R,FMath::Sin(A)*Size.Y*R,-Size.Z*.55);
        Upper[I]=Center+FVector(FMath::Cos(A+.06f)*Size.X*R*.95,FMath::Sin(A+.06f)*Size.Y*R*.95,Size.Z*.23);
        Crown[I]=Center+FVector(FMath::Cos(A+.11f)*Size.X*R*.59,FMath::Sin(A+.11f)*Size.Y*R*.59,Size.Z*Random.FRandRange(.60f,.78f));
    }
    // 独立顶点收束会形成“小金字塔”；以削平冠环和低平顶替换，保留不规则石块肩部。-codex，2026-10-23
    const FVector Top=Center+FVector(Size.X*.05,0,Size.Z*.68);
    const FLinearColor Tone=FLinearColor(.84f,.83f,.75f)*Random.FRandRange(.73f,1.12f);
    for(int32 I=0;I<Sides;++I)
    {
        const int32 J=(I+1)%Sides;
        Surface.Quad(Lower[I],Lower[J],Upper[J],Upper[I],Tone);
        Surface.Quad(Upper[I],Upper[J],Crown[J],Crown[I],Tone);
        Surface.Triangle(Crown[I],Crown[J],Top,Tone);
    }
}
}

AMotionStage::AMotionStage()
{
    PrimaryActorTick.bCanEverTick=true;
    StageRoot=CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));
    SetRootComponent(StageRoot);
    PondRoot=CreateDefaultSubobject<USceneComponent>(TEXT("PondRoot"));
    PondRoot->SetupAttachment(StageRoot);
    auto Mesh=[this](const TCHAR* Name,USceneComponent* Parent)
    {
        auto* Component=CreateDefaultSubobject<UProceduralMeshComponent>(Name);
        Component->SetupAttachment(Parent);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->bUseAsyncCooking=false;
        return Component;
    };
    Bank=Mesh(TEXT("Bank"),PondRoot); Water=Mesh(TEXT("Water"),PondRoot);
    Bed=Mesh(TEXT("Bed"),PondRoot); Cut=Mesh(TEXT("WaterSection"),PondRoot);
    Plants=Mesh(TEXT("SparsePlants"),PondRoot); Floor=Mesh(TEXT("DarkFloor"),StageRoot);
    Environment=Mesh(TEXT("EnvironmentBackdrop"),StageRoot);
    Environment->SetCastShadow(false);
    Environment->SetVisibility(false);
    Water->SetCastShadow(false); Cut->SetCastShadow(false); Plants->SetCastShadow(false);
    // 七个复用槽承载26个逻辑主体；只在启动创建组件/MID，Tick只消费展示帧。-codex，2026-10-31
    CurrentCityIds.SetNum(MotionCarousel::PoolSize);
    for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot)
    {
        auto* Group=CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("CityPond%d"),Slot));
        Group->SetupAttachment(StageRoot);
        CityRoots.Add(Group);
        for(int32 Part=0;Part<5;++Part)
        {
            auto* Component=Mesh(*FString::Printf(TEXT("CityPond%dPart%d"),Slot,Part),Group);
            // 城市为投影槽位而非地理场景，缩略塘禁投影以免跨槽遮水；保留法线受光，单塘不变。-codex，2026-10-23
            Component->SetCastShadow(false);
            CityMeshes.Add(Component);
        }
        Group->SetVisibility(false,true);
    }
    Floor->SetVisibility(false);
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("StageCamera"));
    Camera->SetupAttachment(StageRoot);
    Camera->SetFieldOfView(51.f);
    Camera->bConstrainAspectRatio=false;
    Camera->PostProcessBlendWeight=1.f;
    auto& PP=Camera->PostProcessSettings;
    PP.bOverride_AutoExposureMethod=true; PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true; PP.AutoExposureBias=0.f;
    PP.bOverride_BloomIntensity=true; PP.BloomIntensity=.08f;
    PP.bOverride_VignetteIntensity=true; PP.VignetteIntensity=.28f;
    PP.bOverride_MotionBlurAmount=true; PP.MotionBlurAmount=0.f;
    KeyLight=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("WarmKey"));
    KeyLight->SetupAttachment(StageRoot); KeyLight->SetMobility(EComponentMobility::Movable);
    KeyLight->SetRelativeRotation(FRotator(-42.f,-32.f,0.f));
    KeyLight->SetIntensity(4.f); KeyLight->SetLightColor(FLinearColor(1.f,.87f,.69f));
    KeyLight->LightSourceAngle=2.5f;
    KeyLight->SetForwardShadingPriority(1);
    FillLight=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("CoolFill"));
    FillLight->SetupAttachment(StageRoot); FillLight->SetMobility(EComponentMobility::Movable);
    // 实录两切端几乎无受光；补光从摄影机一侧沿+Y入射，同时照到朝前的两端，主光阴影仍保留。-codex，2026-10-23
    FillLight->SetRelativeRotation(FRotator(-24.f,90.f,0.f));
    FillLight->SetIntensity(1.4f); FillLight->SetLightColor(FLinearColor(.72f,.82f,1.f));
    FillLight->SetForwardShadingPriority(0);
    FillLight->SetCastShadows(false);
    RimLight=CreateDefaultSubobject<UPointLightComponent>(TEXT("WaterGlint"));
    RimLight->SetupAttachment(StageRoot); RimLight->SetMobility(EComponentMobility::Movable);
    RimLight->SetRelativeLocation(FVector(180.f,380.f,850.f));
    RimLight->SetIntensityUnits(ELightUnits::Lumens); RimLight->SetIntensity(1800.f);
    RimLight->SetAttenuationRadius(1800.f); RimLight->SetSourceRadius(130.f);
    RimLight->SetLightColor(FLinearColor(.7f,.87f,1.f)); RimLight->SetCastShadows(false);
    // 使用引擎已有环境贴图补充暗部与反射，不创建天空球、不捕获其它世界，也不依赖Lumen。-codex，2026-10-23
    AmbientLight=CreateDefaultSubobject<USkyLightComponent>(TEXT("StageAmbient"));
    AmbientLight->SetupAttachment(StageRoot);
    AmbientLight->SetMobility(EComponentMobility::Movable);
    AmbientLight->SourceType=SLS_SpecifiedCubemap;
    AmbientLight->SetRealTimeCapture(false);
    AmbientLight->SetIntensity(.45f);
    AmbientLight->SetVisibility(false);
    PondRoot->SetVisibility(false,true);
    UpdatePose();
}

UMaterialInterface* AMotionStage::LoadStageMaterial(const TCHAR* Name)
{
    const FString Path=FString::Printf(TEXT("/Game/Motion/%s.%s"),Name,Name);
    UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,*Path);
    if(!Material)
    {
        UE_LOG(LogTemp,Warning,TEXT("MotionStage missing material %s; engine default is diagnostics only, not visual acceptance"),*Path);
        Material=UMaterial::GetDefaultMaterial(MD_Surface);
    }
    LoadedMaterials.Add(Material);
    return Material;
}

void AMotionStage::BeginPlay()
{
    Super::BeginPlay();
    Bank->SetMaterial(0,LoadStageMaterial(TEXT("M_StageRock")));
    Water->SetMaterial(0,LoadStageMaterial(TEXT("M_StageWater")));
    Bed->SetMaterial(0,LoadStageMaterial(TEXT("M_StageBed")));
    Cut->SetMaterial(0,LoadStageMaterial(TEXT("M_StageCut")));
    Plants->SetMaterial(0,LoadStageMaterial(TEXT("M_StagePlant")));
    Floor->SetMaterial(0,LoadStageMaterial(TEXT("M_StageFloor")));
    PondEnvironmentMaterial=LoadStageMaterial(TEXT("M_PondEnvironment"));
    DataEnvironmentMaterial=LoadStageMaterial(TEXT("M_DataEnvironment"));
    const TCHAR* CarouselNames[]={TEXT("M_CarouselRock"),TEXT("M_CarouselWater"),TEXT("M_CarouselBed"),TEXT("M_CarouselCut"),TEXT("M_CarouselPlant")};
    TArray<UMaterialInterface*> CarouselBases;
    bCarouselMaterialsReady=true;
    for(const TCHAR* Name:CarouselNames)
    {
        auto* Material=LoadStageMaterial(Name);
        bCarouselMaterialsReady&=Material!=UMaterial::GetDefaultMaterial(MD_Surface);
        CarouselBases.Add(Material);
    }
    for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot)
        for(UMaterialInterface* Material:CarouselBases)
            CarouselMaterials.Add(UMaterialInstanceDynamic::Create(Material,this));
    if(!bCarouselMaterialsReady)
        UE_LOG(LogTemp,Error,TEXT("MotionCarousel material assets missing: City hidden until generated assets are available; no default-material visual fallback"));
    Environment->SetMaterial(0,PondEnvironmentMaterial);
    // 当前Editor -game会按需编译：包含两个环境材质，只在界面建立前等待，不能把等待放入Tick。
    // 调用返回不等于资源完整；逐材质回读当前平台状态，打包版本仍由烘焙负责。-codex，2026-10-23
#if WITH_EDITOR
    for(UMaterialInterface* Material:LoadedMaterials)
    {
        if(!Material) continue;
        FMaterialResource* Before=Material->GetMaterialResource(GMaxRHIShaderPlatform);
        const bool bBeforeComplete=Before&&Before->GetGameThreadShaderMap()&&Before->IsGameThreadShaderMapComplete();
        const double Start=FPlatformTime::Seconds();
        Material->EnsureIsComplete();
        const double ElapsedMs=(FPlatformTime::Seconds()-Start)*1000.;
        FMaterialResource* After=Material->GetMaterialResource(GMaxRHIShaderPlatform);
        const bool bHasMap=After&&After->GetGameThreadShaderMap();
        const bool bAfterComplete=bHasMap&&After->IsGameThreadShaderMapComplete();
        UE_LOG(LogTemp,Display,TEXT("MotionStage shader preparation call returned: material=%s platform=%d elapsedMs=%.2f beforeComplete=%d resource=%d shaderMap=%d afterComplete=%d (Editor-game only; not a global shader readiness claim)"),
            *Material->GetPathName(),int32(GMaxRHIShaderPlatform),ElapsedMs,bBeforeComplete,After!=nullptr,bHasMap,bAfterComplete);
        if(!bAfterComplete)
            UE_LOG(LogTemp,Warning,TEXT("MotionStage material remains incomplete after startup preparation: %s"),*Material->GetPathName());
    }
#else
    UE_LOG(LogTemp,Display,TEXT("MotionStage startup shader preparation not executed: non-editor build requires cooked shaders"));
#endif
    PondEnvironmentDynamic=UMaterialInstanceDynamic::Create(PondEnvironmentMaterial,this);
    DataEnvironmentDynamic=UMaterialInstanceDynamic::Create(DataEnvironmentMaterial,this);
    Environment->SetMaterial(0,PondEnvironmentDynamic);
    InitializeBackgroundVideo();
    const TCHAR* AmbientPath=TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap");
    AmbientCubemap=LoadObject<UTextureCube>(nullptr,AmbientPath);
    if(AmbientCubemap)
    {
        AmbientLight->SetCubemap(AmbientCubemap);
        AmbientLight->SetVisibility(true);
    }
    else
    {
        UE_LOG(LogTemp,Warning,TEXT("MotionStage ambient cubemap failed to load: %s; StageAmbient remains disabled, no asset substitution"),AmbientPath);
    }
    BuildGeometry();
    BuildCarouselGeometryCache();
    BuildCityGeometry();
    // 首槽依次为岸/水/底；实例只在启动建立，城市和单塘始终保留源材质。-codex，2026-10-26
    for(int32 Part=0;Part<3;++Part)
        DataWaterlightMaterials.Add(UMaterialInstanceDynamic::Create(CityMeshes[Part]->GetMaterial(0),this));
    // 局部+Y向右、+Z向上；UV原点在左上，避免沿用地面投影导致背景倒置。-codex，2026-10-24
    TArray<FVector> BackVertices={FVector(0,-1,1),FVector(0,1,1),FVector(0,1,-1),FVector(0,-1,-1)};
    TArray<int32> BackIndices={0,2,1,0,3,2};
    TArray<FVector> BackNormals; BackNormals.Init(FVector(-1,0,0),4);
    TArray<FVector2D> BackUVs={FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)};
    TArray<FLinearColor> BackColors; BackColors.Init(FLinearColor::White,4);
    TArray<FProcMeshTangent> BackTangents; BackTangents.Init(FProcMeshTangent(0,1,0),4);
    Environment->CreateMeshSection_LinearColor(0,BackVertices,BackIndices,BackNormals,BackUVs,BackColors,BackTangents,false,false);
    if(APlayerController* PC=GetWorld()->GetFirstPlayerController()) PC->SetViewTarget(this);
    else UE_LOG(LogTemp,Warning,TEXT("MotionStage has no local player controller; camera binding must be retried by owner"));
}

void AMotionStage::BuildGeometry()
{
    using namespace MotionStageGeometry;
    FSurface RockMesh,WaterMesh,BedMesh,CutMesh,PlantMesh,FloorMesh;
    FRandomStream Random(48271);
    FVector Rings[6][Segments];
    for(int32 I=0;I<Segments;++I)
    {
        const float A=2.f*PI*I/Segments;
        const float H=6.f*FMath::Sin(A*9.f)+3.f*FMath::Sin(A*19.f);
        Rings[0][I]=Outline(I,1.10f,-108.f+H*.4f);
        Rings[1][I]=Outline(I,1.04f,-26.f+H);
        Rings[2][I]=Outline(I,.94f+.012f*FMath::Sin(13.f*A),92.f+H);
        Rings[3][I]=Outline(I,.865f+.012f*FMath::Sin(7.f*A+.8f),85.f+H);
        Rings[4][I]=Outline(I,.76f,WaterZ-1.f);
        Rings[5][I]=Outline(I,.68f,BedZ);
    }
    const FLinearColor LayerTones[]={FLinearColor(.51f,.48f,.41f),FLinearColor(.70f,.65f,.52f),
        FLinearColor(.95f,.89f,.71f),FLinearColor(.66f,.64f,.53f),FLinearColor(.31f,.39f,.33f)};
    for(int32 I=0;I<Segments;++I)
    {
        const int32 J=(I+1)%Segments;
        for(int32 R=0;R<5;++R)
            RockMesh.Quad(Rings[R][I],Rings[R][J],Rings[R+1][J],Rings[R+1][I],LayerTones[R]);
    }
    // 完整塘岸取代旧前剖切；水面沿水线闭环，塘底接内坡底环，避免直弦截断和重叠底面。-codex，2026-10-30
    for(int32 I=0;I<Segments;++I)
    {
        const int32 J=(I+1)%Segments;
        FVector A=Rings[4][I],B=Rings[4][J];
        A.Z=WaterZ;B.Z=WaterZ;
        WaterMesh.Triangle(FVector(0,0,WaterZ),A,B,FLinearColor::White,true);
        BedMesh.Triangle(FVector(0,0,BedZ),Rings[5][I],Rings[5][J],FLinearColor(.62f,.66f,.53f),true);
    }
    // 外裙和底盖沿原岸脚闭合，不再保留剖面前唇；Cut空节维持既有五部件复制链。-codex，2026-10-30
    for(int32 I=0;I<Segments;++I)
    {
        const int32 J=(I+1)%Segments;
        const FVector A=Rings[0][I],B=Rings[0][J];
        const FVector LowA(A.X,A.Y,-116.f),LowB(B.X,B.Y,-116.f);
        BedMesh.Quad(LowA,LowB,B,A,FLinearColor(.30f,.29f,.23f));
        BedMesh.Triangle(FVector(0,0,-116.f),LowB,LowA,FLinearColor(.30f,.29f,.23f));
    }
    // 岸石位置、体量、朝向以固定种子生成；不同塘只改变入场相位，避免伪造152份测绘几何。-codex，2026-10-23
    for(int32 I=0;I<Segments;++I)
    {
        for(int32 Row=0;Row<3;++Row)
        {
            const float A=2.f*PI*I/Segments;
            const float H=6.f*FMath::Sin(A*9.f)+3.f*FMath::Sin(A*19.f);
            const float Scale=Row==0 ? Random.FRandRange(.886f,.921f) :
                Row==1 ? Random.FRandRange(.985f,1.02f) : Random.FRandRange(.80f,.835f);
            const float Z=Row==0 ? 88.f+H : Row==1 ? Random.FRandRange(-3.f,25.f)+H : Random.FRandRange(47.f,63.f)+H*.5f;
            const FVector Size(Random.FRandRange(20.f,40.f),Random.FRandRange(14.f,29.f),Random.FRandRange(9.f,19.f));
            AddStone(RockMesh,Random,Outline(I,Scale,Z),Size);
        }
        if(I%3==0)
        {
            const FVector Root=Outline(I,.97f,60.f);
            for(int32 Blade=0;Blade<5;++Blade)
            {
                const FVector Base=Root+FVector(Random.FRandRange(-13.f,13.f),Random.FRandRange(-13.f,13.f),0);
                const FVector Tip=Base+FVector(Random.FRandRange(-12.f,12.f),Random.FRandRange(-12.f,12.f),Random.FRandRange(14.f,37.f));
                PlantMesh.Triangle(Base-FVector(2,0,0),Base+FVector(2,0,0),Tip,FLinearColor(.38f,.42f,.21f));
                PlantMesh.Triangle(Base+FVector(2,0,0),Base-FVector(2,0,0),Tip,FLinearColor(.38f,.42f,.21f));
            }
        }
    }
    FloorMesh.Quad(FVector(-6000,-6000,-118),FVector(6000,-6000,-118),FVector(6000,6000,-118),FVector(-6000,6000,-118),FLinearColor::White);
    RockMesh.Submit(Bank);WaterMesh.Submit(Water);BedMesh.Submit(Bed);CutMesh.Submit(Cut);PlantMesh.Submit(Plants);FloorMesh.Submit(Floor);
    UE_LOG(LogTemp,Display,TEXT("MotionStage built once: bank=%d water=%d bed=%d cut=%d plant=%d floor=%d triangles; seed=48271; illustrative geometry"),
        RockMesh.Indices.Num()/3,WaterMesh.Indices.Num()/3,BedMesh.Indices.Num()/3,CutMesh.Indices.Num()/3,PlantMesh.Indices.Num()/3,FloorMesh.Indices.Num()/3);
}

void AMotionStage::BuildCityGeometry(bool bCarousel)
{
    if(bCarousel)
    {
        for(const auto& Item:CarouselFrame.Items)
            if(Item.Slot>=0&&Item.Slot<MotionCarousel::PoolSize&&Item.Index>=0)
                BindCarouselStyle(Item.Slot,Item.Index);
    }
    else
    {
        UProceduralMeshComponent* Sources[]={Bank.Get(),Water.Get(),Bed.Get(),Cut.Get(),Plants.Get()};
        for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot)
        {
            CarouselSlotStyles[Slot]=INDEX_NONE;
            for(int32 Part=0;Part<5;++Part)
            {
                auto* Source=Sources[Part];auto* Destination=CityMeshes[Slot*5+Part].Get();
                if(const FProcMeshSection* Section=Source->GetProcMeshSection(0))
                {
                    FProcMeshSection Copy=*Section;Copy.bEnableCollision=false;
                    Destination->SetProcMeshSection(0,Copy);
                    Destination->SetMaterial(0,Source->GetMaterial(0));
                }
            }
        }
    }
    bCarouselGeometry=bCarousel;
}

void AMotionStage::BindCarouselStyle(int32 Slot,int32 SubjectIndex)
{
    const int32 Style=SubjectIndex%6;
    if(!CarouselShapes.IsValidIndex(Style)||CarouselSlotStyles[Slot]==Style)return;
    // 复用槽先隐藏再换节；每档只在启动构建，移动中的帧不重新生成网格。-codex，2026-10-31
    CityRoots[Slot]->SetVisibility(false,true);
    for(int32 Part=0;Part<5;++Part)
        CityMeshes[Slot*5+Part]->SetProcMeshSection(0,CarouselShapes[Style].Sections[Part]);
    CarouselSlotStyles[Slot]=Style;
}

void AMotionStage::BuildCarouselGeometryCache()
{
    using namespace MotionStageGeometry;
    if(!CarouselShapes.IsEmpty())return;
    UProceduralMeshComponent* Sources[]={Bank.Get(),Water.Get(),Bed.Get(),Cut.Get(),Plants.Get()};
    struct FStyle{double Power,X,Y;FVector Tint;int32 Dividers,Clumps;};
    // 圆润、圆角矩形、长条、近圆、不对称、分区圆角；统一低岸纹理，差别只属于展示示意。
    // 岸色已有VertexColor乘法可直接消费；Water原图不消费顶点色，不声称水色因缓存而变化。-codex，2026-10-31
    const FStyle Styles[]={
        {2.,1.,.84,FVector(1.05,1.06,1.08),0,3},
        {4.2,1.,.90,FVector(1.12,1.02,.79),1,4},
        {3.2,1.10,.62,FVector(.77,.85,.82),2,3},
        {2.,.82,1.21,FVector(.94,1.02,1.05),0,4},
        {2.3,1.,.87,FVector(.84,.79,.69),0,3},
        {3.7,1.,.88,FVector(1.10,1.07,.94),3,5}};
    const FVector Forward=FVector(0.f,2250.f,-1570.f).GetSafeNormal();
    uint64 Bytes=0;
    for(int32 StyleIndex=0;StyleIndex<6;++StyleIndex)
    {
        const auto& Style=Styles[StyleIndex];
        auto& Cache=CarouselShapes.AddDefaulted_GetRef();Cache.Sections.SetNum(5);
        Cache.Near=TNumericLimits<double>::Max();Cache.Far=-TNumericLimits<double>::Max();
        auto Map=[&](const FVector& P)
        {
            const double X=P.X/740.,Y=P.Y/460.,Angle=FMath::Atan2(Y,X);
            const double R=FMath::Pow(FMath::Pow(FMath::Abs(X),3.7)+FMath::Pow(FMath::Abs(Y),3.7),1./3.7);
            const double C=FMath::Cos(Angle),S=FMath::Sin(Angle);
            const double Denom=FMath::Pow(FMath::Pow(FMath::Abs(C),Style.Power)+FMath::Pow(FMath::Abs(S),Style.Power),1./Style.Power);
            const double Bend=StyleIndex==4?1.+.13*FMath::Sin(3.*Angle+.4)+.055*FMath::Cos(Angle-.7):1.;
            return FVector(740.*Style.X*R*C/Denom*Bend,460.*Style.Y*R*S/Denom*Bend,P.Z*.42);
        };
        auto Append=[](FProcMeshSection& Section,const FSurface& Surface)
        {
            const uint32 Base=Section.ProcVertexBuffer.Num();
            for(int32 I=0;I<Surface.Vertices.Num();++I)
            {
                FProcMeshVertex V;V.Position=Surface.Vertices[I];V.Normal=Surface.Normals[I];
                V.UV0=Surface.UVs[I];V.Color=Surface.Colors[I].ToFColor(false);V.Tangent=Surface.Tangents[I];
                Section.ProcVertexBuffer.Add(V);
            }
            for(int32 Index:Surface.Indices)Section.ProcIndexBuffer.Add(Base+Index);
        };
        for(int32 Part=0;Part<5;++Part)
        {
            auto& Section=Cache.Sections[Part];
            if(Part!=4)
                if(const FProcMeshSection* Source=Sources[Part]->GetProcMeshSection(0))Section=*Source;
            if(Part==0&&Style.Dividers>0)
            {
                FSurface Dikes;
                auto Dike=[&](bool AlongX,double Offset)
                {
                    // 底部落到原塘底，上脊高出水面约11cm；两端伸入内岸，不悬浮、不越过外岸。
                    // 8段跟随同一轮廓映射，水与岸使用相同映射，闭环关系保持。-codex，2026-10-31
                    const double HalfLength=AlongX?610.:375.;
                    auto Q=[&](const FVector& A,const FVector& B,const FVector& C,const FVector& D,const FLinearColor& Color)
                    {
                        if(AlongX)Dikes.Quad(A,B,C,D,Color);
                        else Dikes.Quad(D,C,B,A,Color);
                    };
                    for(int32 K=0;K<8;++K)
                    {
                        const double A=-HalfLength+2.*HalfLength*K/8.,B=-HalfLength+2.*HalfLength*(K+1)/8.;
                        auto P=[&](double L,double W,double Z){return AlongX?FVector(L,Offset+W,Z):FVector(Offset+W,L,Z);};
                        const FVector A0=P(A,-19,BedZ),A1=P(A,19,BedZ),B0=P(B,-19,BedZ),B1=P(B,19,BedZ);
                        const FVector A2=P(A,-19,WaterZ+26),A3=P(A,19,WaterZ+26),B2=P(B,-19,WaterZ+26),B3=P(B,19,WaterZ+26);
                        const FLinearColor Top(.72,.70,.57),Side(.43,.45,.36);
                        Q(A2,B2,B3,A3,Top);Q(A0,B0,B2,A2,Side);
                        Q(A3,B3,B1,A1,Side);
                        if(K==0)Q(A1,A0,A2,A3,Side);
                        if(K==7)Q(B0,B1,B3,B2,Side);
                    }
                };
                if(Style.Dividers==1)Dike(false,0.);
                if(Style.Dividers==2){Dike(false,-230.);Dike(false,230.);}
                if(Style.Dividers==3)Dike(true,0.);
                Append(Section,Dikes);
            }
            if(Part==4)
            {
                FSurface Foliage;FRandomStream Random(48271+StyleIndex*113);
                for(int32 Clump=0;Clump<Style.Clumps;++Clump)
                {
                    const double A=.45+Clump*(2.*PI/Style.Clumps)+StyleIndex*.23;
                    const int32 I=FMath::RoundToInt(A/(2.*PI)*Segments)%Segments;
                    const FVector Root=Map(Outline(I,.914f,89.f+6.f*FMath::Sin(A*9)+3.f*FMath::Sin(A*19)));
                    const int32 Blades=Clump%2==0?7:4;
                    for(int32 Blade=0;Blade<Blades;++Blade)
                    {
                        const FVector Foot=Root+FVector(Random.FRandRange(-12,12),Random.FRandRange(-9,9),-2.);
                        const double Height=Random.FRandRange(64,140),Width=Random.FRandRange(5,12);
                        const FVector Lean(Random.FRandRange(-22,22),Random.FRandRange(-18,18),0);
                        const FVector Tip=Foot+Lean+FVector(0,0,Height),Shoulder=Foot+Lean*.55+FVector(0,0,Height*.57);
                        const FVector Across=FVector(FMath::Cos(A+Blade*.8),FMath::Sin(A+Blade*.8),0)*Width;
                        const FLinearColor Green=Blade%3==0?FLinearColor(.84,.94,.48):FLinearColor(.56,.84,.40);
                        Foliage.Triangle(Foot-Across*.28,Shoulder-Across,Tip,Green);
                        Foliage.Triangle(Foot-Across*.28,Tip,Shoulder+Across,Green);
                        Foliage.Triangle(Foot-Across*.28,Shoulder+Across,Foot+Across*.28,Green);
                    }
                }
                Append(Section,Foliage);
            }
            Section.bEnableCollision=false;Section.SectionLocalBox.Init();
            for(auto& V:Section.ProcVertexBuffer)
            {
                if(Part!=4)V.Position=Map(V.Position);
                if(Part==0)
                {
                    V.Color.R=uint8(FMath::Clamp(V.Color.R*Style.Tint.X,0.,255.));
                    V.Color.G=uint8(FMath::Clamp(V.Color.G*Style.Tint.Y,0.,255.));
                    V.Color.B=uint8(FMath::Clamp(V.Color.B*Style.Tint.Z,0.,255.));
                }
                Section.SectionLocalBox+=V.Position;
                const double D=FVector::DotProduct(V.Position,Forward);
                Cache.Near=FMath::Min(Cache.Near,D);Cache.Far=FMath::Max(Cache.Far,D);
            }
            for(int32 I=0;I+2<Section.ProcIndexBuffer.Num();I+=3)
            {
                auto& A=Section.ProcVertexBuffer[Section.ProcIndexBuffer[I]];
                auto& B=Section.ProcVertexBuffer[Section.ProcIndexBuffer[I+1]];
                auto& C=Section.ProcVertexBuffer[Section.ProcIndexBuffer[I+2]];
                const FVector N=FVector::CrossProduct(C.Position-A.Position,B.Position-A.Position).GetSafeNormal();
                const FVector E1=B.Position-A.Position,E2=C.Position-A.Position;
                const FVector2D UV1=B.UV0-A.UV0,UV2=C.UV0-A.UV0;
                const double Determinant=UV1.X*UV2.Y-UV1.Y*UV2.X;
                FVector Tangent=FMath::Abs(Determinant)>1.e-10?(E1*UV2.Y-E2*UV1.Y)/Determinant:FVector::XAxisVector;
                const FVector Bitangent=FMath::Abs(Determinant)>1.e-10?(E2*UV1.X-E1*UV2.X)/Determinant:FVector::YAxisVector;
                Tangent-=N*FVector::DotProduct(Tangent,N);
                if(!Tangent.Normalize())Tangent=FVector::CrossProduct(FVector::YAxisVector,N).GetSafeNormal();
                const bool Flip=FVector::DotProduct(FVector::CrossProduct(N,Tangent),Bitangent)<0.;
                for(FProcMeshVertex* V:{&A,&B,&C})
                {
                    V->Normal=N;
                    V->Tangent=FProcMeshTangent(Tangent,Flip);
                }
            }
            Section.ProcVertexBuffer.Shrink();Section.ProcIndexBuffer.Shrink();
            Bytes+=Section.ProcVertexBuffer.GetAllocatedSize()+Section.ProcIndexBuffer.GetAllocatedSize();
        }
        Cache.Width=FMath::Max(1.,Cache.Sections[0].SectionLocalBox.GetSize().X);
        Bytes+=Cache.Sections.GetAllocatedSize();
        UE_LOG(LogTemp,Display,TEXT("MotionCarousel style=%d width=%.3f localNear=%.3f localFar=%.3f dividers=%d clumps=%d"),
            StyleIndex,Cache.Width,Cache.Near,Cache.Far,Style.Dividers,Style.Clumps);
    }
    Bytes+=CarouselShapes.GetAllocatedSize();
    UE_LOG(LogTemp,Display,TEXT("MotionCarousel CPU geometry cache: styles=6 bytes=%llu MiB=%.3f; 7 shared component slots, no per-frame geometry generation"),Bytes,double(Bytes)/(1024.*1024.));
}

void AMotionStage::SetCityCarouselFrame(const MotionCarousel::FFrame& Frame)
{
    // Dashboard是唯一轮转时钟；Actor Tick、SetPresentation和Slate共用这份帧，禁止再累计轮转时间。-codex，2026-10-31
    CarouselFrame=Frame;
}

void AMotionStage::SetPresentation(EMotionStageView View,float EntryProgress,int32 SiteSeed,const TArray<FString>& CityIds)
{
    Entry=FMath::IsFinite(EntryProgress)?FMath::Clamp(EntryProgress,0.f,1.f):0.f;
    const bool bViewChanged=CurrentView!=View;
    if(View==EMotionStageView::Pond&&(SiteSeed!=CurrentSiteSeed||bViewChanged))
    {
        CurrentSiteSeed=SiteSeed;
        SitePhase=static_cast<float>(static_cast<uint32>(SiteSeed)%997u)/997.f*2.f*PI;
        MotionTime=0.;
    }
    const FString EmptyId;
    for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot)
    {
        // 数据页只接收所选塘到首槽；即使调用方残留六个ID，也不泄漏成旧六塘构图。城市入口仍保留六槽。-codex，2026-10-25
        const bool bActiveSlot=View==EMotionStageView::City||(View==EMotionStageView::DataOverview&&Slot==0);
        const FString& Id=bActiveSlot&&CityIds.IsValidIndex(Slot)?CityIds[Slot]:EmptyId;
        if(CurrentCityIds[Slot]!=Id||bViewChanged)
        {
            CurrentCityIds[Slot]=Id;
            CityAges[Slot]=0.;
            CityPhases[Slot]=static_cast<float>(FCrc::StrCrc32(*Id)%10007u)/10007.f*2.f*PI;
        }
    }
    CurrentView=View;
    if(bViewChanged)
    {
        bCarouselFrameApplied=false;
        if(!bSpritePondsEnabled&&bCarouselGeometry!=(View==EMotionStageView::City))BuildCityGeometry(View==EMotionStageView::City);
        UProceduralMeshComponent* Sources[]={Bank.Get(),Water.Get(),Bed.Get(),Cut.Get(),Plants.Get()};
        for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot)
        for(int32 Part=0;Part<5;++Part)
        {
            UMaterialInstanceDynamic* Dynamic=DataWaterlightMaterials.IsValidIndex(Part)?DataWaterlightMaterials[Part].Get():nullptr;
            UMaterialInterface* Material=Sources[Part]->GetMaterial(0);
            if(View==EMotionStageView::City&&CarouselMaterials.IsValidIndex(Slot*5+Part))Material=CarouselMaterials[Slot*5+Part];
            else if(View==EMotionStageView::DataOverview&&Slot==0&&Dynamic)Material=Dynamic;
            CityMeshes[Slot*5+Part]->SetMaterial(0,Material);
            CityMeshes[Slot*5+Part]->SetTranslucentSortPriority(0);
        }
        PondRoot->SetVisibility(!bSpritePondsEnabled&&View==EMotionStageView::Pond,true);
        // 两页均由远处环境承接，旧蓝黑地板会遮住水下背景，保留组件仅供历史资产兼容。-codex，2026-10-24
        Floor->SetVisibility(false);
        Environment->SetVisibility(View!=EMotionStageView::Hidden);
        Environment->SetMaterial(0,View==EMotionStageView::DataOverview?DataEnvironmentDynamic.Get():PondEnvironmentDynamic.Get());
        RimLight->SetVisibility(View!=EMotionStageView::Hidden);
        // round3六塘等亮且无局部镜面：降低多塘均匀填光，以已有局部灯建立主次；单塘保留基础光并增强局部反光。
        // 固定手动曝光、DX11无GI路径下的起始调校，仍需同镜实帧核验，不能当物理水下照度。-codex，2026-10-24
        const bool bOverview=View==EMotionStageView::City||View==EMotionStageView::DataOverview;
        KeyLight->SetIntensity(bOverview?3.7f:4.f);
        FillLight->SetIntensity(bOverview?.8f:1.4f);
        AmbientLight->SetIntensity(bOverview?.42f:.45f);
        // round4表面镜面过曝为白色硬片：降低局部灯功率并收小发光面；柔化由水面粗糙度承担。-codex，2026-10-24
        RimLight->SetIntensity(bOverview?2200.f:4500.f);
        RimLight->SetAttenuationRadius(bOverview?900.f:1800.f);
        RimLight->SetSourceRadius(bOverview?70.f:90.f);
        RimLight->SetLightColor(FLinearColor(1.f,.85f,.62f));
        if(View!=EMotionStageView::City&&View!=EMotionStageView::DataOverview)
            for(auto& Group:CityRoots) Group->SetVisibility(false,true);
    }
    UpdatePose();
}

void AMotionStage::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateBackgroundVideo();
    if(CurrentView!=EMotionStageView::Hidden)
    {
        MotionTime+=FMath::Max(0.f,DeltaSeconds);
        if(CurrentView==EMotionStageView::City||CurrentView==EMotionStageView::DataOverview)
            for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot) CityAges[Slot]+=FMath::Max(0.f,DeltaSeconds);
        UpdatePose();
    }
}

void AMotionStage::InitializeBackgroundVideo()
{
    // 使用用户认可的五秒300帧修正版，60fps整段缓存；旧序列保留用于恢复。-codex，2026-10-28
    if(!PondEnvironmentDynamic) return;
    PondEnvironmentDynamic->SetScalarParameterValue(TEXT("VideoReady"),0.f);
    constexpr uint64 GiB=1024ull*1024ull*1024ull;
    if(FPlatformMemory::GetStats().AvailablePhysical<6*GiB)
    {
        FailBackgroundVideo(TEXT("less than 3 GiB headroom plus 3 GiB cache budget"));
        return;
    }
    const UImgMediaSettings* Settings=GetDefault<UImgMediaSettings>();
    if(Settings->UseGlobalCache||Settings->bEnablePrecachingTimeInterval||Settings->BandwidthThrottlingEnabled||
        !FMath::IsNearlyEqual(Settings->CacheSizeGB,3.f)||!FMath::IsNearlyZero(Settings->CacheBehindPercentage))
    {
        FailBackgroundVideo(TEXT("project ImgMedia cache settings do not match full-cache contract"));
        return;
    }
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Movies/pond-background-smooth-60")));
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files,*FPaths::Combine(Directory,TEXT("*")),true,false);
    if(Files.Num()!=300)
    {
        FailBackgroundVideo(TEXT("sequence directory must contain exactly 300 frame files"));
        return;
    }
    for(int32 Frame=0;Frame<300;++Frame)
    {
        if(!FPaths::FileExists(FPaths::Combine(Directory,FString::Printf(TEXT("frame_%04d.jpg"),Frame))))
        {
            FailBackgroundVideo(TEXT("sequence frame_0000.jpg through frame_0299.jpg is incomplete"));
            return;
        }
    }
    BackgroundPlayer=NewObject<UMediaPlayer>(this);
    BackgroundVideoTexture=NewObject<UMediaTexture>(this);
    BackgroundSequenceSource=NewObject<UImgMediaSource>(this);
    BackgroundSequenceSource->SetSequencePath(Directory);
    BackgroundSequenceSource->FrameRateOverride=FFrameRate(60,1);
    BackgroundSequenceSource->bFillGapsInSequence=false;
    // GenericImgMediaReader从JPEG获取sRGB元数据，MediaTexture据此线性化；保留默认源编码，
    // 继续LinearColor材质采样，禁止额外pow/gamma或把JPEG谎报为Linear。-codex，2026-10-26
    BackgroundPlayer->NativeAudioOut=false;
    BackgroundPlayer->PlayOnOpen=false;
    BackgroundPlayer->SetDesiredPlayerName(TEXT("ImgMedia"));
    BackgroundVideoTexture->NewStyleOutput=true;
    BackgroundVideoTexture->SRGB=false;
    BackgroundVideoTexture->AutoClear=false;
    BackgroundVideoTexture->AddressX=TA_Clamp;
    BackgroundVideoTexture->AddressY=TA_Clamp;
    BackgroundVideoTexture->SetMediaPlayer(BackgroundPlayer);
    BackgroundVideoTexture->UpdateResource();
    PondEnvironmentDynamic->SetTextureParameterValue(TEXT("VideoTexture"),BackgroundVideoTexture);
    FMediaPlayerOptions Options;
    Options.PlayOnOpen=EMediaPlayerOptionBooleanOverride::Disabled;
    Options.Loop=EMediaPlayerOptionBooleanOverride::Enabled;
    Options.Tracks.Audio=INDEX_NONE;
    Options.InternalCustomOptions.Add(MediaPlayerOptionValues::ImgMediaSmartCacheEnabled(),FVariant(false));
    BackgroundLoadStarted=FPlatformTime::Seconds();
    if(!BackgroundSequenceSource->Validate()||!BackgroundPlayer->OpenSourceWithOptions(BackgroundSequenceSource,Options))
    {
        FailBackgroundVideo(TEXT("ImgMedia sequence open rejected"));
        return;
    }
    UE_LOG(LogTemp,Display,TEXT("MotionBackgroundSequence opening frames=300 fps=60 cacheGiB=3 smartCache=0 globalCache=0 freeGiB=%.2f"),
        double(FPlatformMemory::GetStats().AvailablePhysical)/double(GiB));
}

bool AMotionStage::IsBackgroundFullyCached() const
{
    if(!BackgroundPlayer) return false;
    const auto NativePlayer=BackgroundPlayer->GetPlayerFacade()->GetPlayer();
    if(!NativePlayer.IsValid()) return false;
    TRangeSet<FTimespan> Loaded;
    if(!NativePlayer->GetCache().QueryCacheState(EMediaCacheState::Loaded,Loaded)) return false;
    // ImgMedia按每帧[start,nextStart)加入RangeSet，集合合并相邻区间。
    // Contains完整区间要求无洞覆盖，不能用min/max或总长度掩盖中间缺帧。-codex，2026-10-26
    return Loaded.Contains(TRange<FTimespan>(FTimespan::Zero(),FTimespan::FromSeconds(5.)));
}

void AMotionStage::FailBackgroundVideo(const TCHAR* Reason)
{
    if(BackgroundState==EBackgroundState::Failed) return;
    BackgroundState=EBackgroundState::Failed;
    if(PondEnvironmentDynamic) PondEnvironmentDynamic->SetScalarParameterValue(TEXT("VideoReady"),0.f);
    if(BackgroundPlayer) BackgroundPlayer->Close();
    UE_LOG(LogTemp,Error,TEXT("MotionBackgroundSequence failed reason=%s fallback=static no-WMF-fallback"),Reason);
}

void AMotionStage::UpdateBackgroundVideo()
{
    if(!BackgroundPlayer||BackgroundState==EBackgroundState::Failed) return;
    const double Now=FPlatformTime::Seconds();
    if(BackgroundPlayer->HasError())
    {
        FailBackgroundVideo(TEXT("ImgMedia playback error"));
        return;
    }
    if(BackgroundState!=EBackgroundState::Playing&&Now-BackgroundLoadStarted>45.)
    {
        UE_LOG(LogTemp,Warning,TEXT("MotionBackgroundSequence timeout ready=%d cached=%d aspect=%.3f texture=%dx%d"),
            BackgroundPlayer->IsReady(),IsBackgroundFullyCached(),BackgroundVideoTexture->GetCurrentAspectRatio(),
            BackgroundVideoTexture->GetWidth(),BackgroundVideoTexture->GetHeight());
        FailBackgroundVideo(TEXT("full-cache or first-frame timeout after 45 seconds"));
        return;
    }
    if(BackgroundState==EBackgroundState::Loading)
    {
        if(!BackgroundPlayer->IsReady()) return;
        if(!bBackgroundMetadataChecked)
        {
            const FIntPoint Size=BackgroundPlayer->GetVideoTrackDimensions(INDEX_NONE,INDEX_NONE);
            const double Fps=BackgroundPlayer->GetVideoTrackFrameRate(INDEX_NONE,INDEX_NONE);
            const double Duration=BackgroundPlayer->GetDuration().GetTotalSeconds();
            if(Size!=FIntPoint(1920,1080)||FMath::Abs(Fps-60.)>.001||FMath::Abs(Duration-5.)>.001)
            {
                UE_LOG(LogTemp,Error,TEXT("MotionBackgroundSequence metadata size=%dx%d fps=%.4f duration=%.4f"),Size.X,Size.Y,Fps,Duration);
                FailBackgroundVideo(TEXT("expected 1920x1080 / 60fps / 5 seconds"));
                return;
            }
            bBackgroundMetadataChecked=true;
        }
        if(!IsBackgroundFullyCached()) return;
        if(FPlatformMemory::GetStats().AvailablePhysical<3ull*1024ull*1024ull*1024ull)
        {
            FailBackgroundVideo(TEXT("less than 3 GiB physical memory remaining after full cache"));
            return;
        }
        // 完整缓存后才正常Play，避免复用曾失败的暂停Seek首帧门控。静态图维持到纹理已呈交。-codex，2026-10-26
        if(!BackgroundPlayer->SetLooping(true)||!BackgroundPlayer->Play())
        {
            FailBackgroundVideo(TEXT("cached sequence Play rejected"));
            return;
        }
        BackgroundState=EBackgroundState::Starting;
        BackgroundPlayStarted=Now;
        UE_LOG(LogTemp,Display,TEXT("MotionBackgroundSequence full-cache coverage=[0,5) no-gaps=1 frames=300 elapsed=%.3f freeGiB=%.2f"),
            Now-BackgroundLoadStarted,double(FPlatformMemory::GetStats().AvailablePhysical)/(1024.*1024.*1024.));
    }
    if(BackgroundState==EBackgroundState::Starting)
    {
        const bool bTextureReady=BackgroundVideoTexture->GetCurrentAspectRatio()>0.f&&
            BackgroundVideoTexture->GetWidth()==1920&&BackgroundVideoTexture->GetHeight()==1080;
        if(bTextureReady&&!bBackgroundFencePending)
        {
            BackgroundFrameFence.BeginFence();
            bBackgroundFencePending=true;
        }
        else if(bTextureReady&&bBackgroundFencePending&&BackgroundFrameFence.IsFenceComplete())
        {
            PondEnvironmentDynamic->SetScalarParameterValue(TEXT("VideoReady"),1.f);
            BackgroundState=EBackgroundState::Playing;
            UE_LOG(LogTemp,Display,TEXT("MotionBackgroundSequence texture-ready first-display-time=%.4f fence=complete"),BackgroundPlayer->GetTime().GetTotalSeconds());
        }
    }
    if(BackgroundState==EBackgroundState::Playing)
    {
        if(!bBackgroundCacheRechecked&&Now-BackgroundPlayStarted>=6.)
        {
            bBackgroundCacheRechecked=true;
            if(!IsBackgroundFullyCached())
            {
                FailBackgroundVideo(TEXT("full cache coverage lost after playback"));
                return;
            }
            UE_LOG(LogTemp,Display,TEXT("MotionBackgroundSequence cache-recheck coverage=[0,5) no-gaps=1"));
        }
        const double Time=BackgroundPlayer->GetTime().GetTotalSeconds();
        // 显式诊断时逐帧记录已交付样本（不是屏幕呈交证明），两秒批量输出避免逐帧日志IO扰动。
        // 实际显示仍须与独立窗口录制对照；正常启动不启用。-codex，2026-10-30
        static const bool bTimingProbe=FParse::Param(FCommandLine::Get(),TEXT("MotionVideoTiming"));
        if(bTimingProbe)
        {
            const FMediaTimeStamp Sample=BackgroundPlayer->GetPlayerFacade()->GetTimeStamp();
            const double SampleSeconds=Sample.Time.GetTotalSeconds();
            BackgroundTimingRows+=FString::Printf(TEXT("wall=%.6f game=%.6f dt=%.6f media=%.7f sample=%.7f frame=%d loop=%d seq=%d rate=%.3f focus=%d\n"),
                Now,FApp::GetGameTime(),FApp::GetDeltaTime(),Time,SampleSeconds,
                FMath::RoundToInt(SampleSeconds*60.),Sample.GetLoopIndex(),Sample.GetSequenceIndex(),
                BackgroundPlayer->GetRate(),FSlateApplication::IsInitialized()&&FSlateApplication::Get().IsActive());
            ++BackgroundTimingSamples;
            if(Now-BackgroundTimingLastFlush>=2.)
            {
                UE_LOG(LogTemp,Display,TEXT("MotionVideoTiming samples=%d delivered-sample-not-present-proof\n%s"),BackgroundTimingSamples,*BackgroundTimingRows);
                BackgroundTimingRows.Reset();BackgroundTimingSamples=0;BackgroundTimingLastFlush=Now;
            }
        }
        // 每轮记录缓存覆盖和余量，供持续运行核验；媒体时钟日志不等于实际显示帧证明。-codex，2026-10-28
        if(BackgroundLastTime>Time+.5)
            UE_LOG(LogTemp,Display,TEXT("MotionBackgroundSequence loop=%d from=%.4f to=%.4f full-cache=%d freeGiB=%.2f"),
                ++BackgroundLoopLogs,BackgroundLastTime,Time,IsBackgroundFullyCached(),
                double(FPlatformMemory::GetStats().AvailablePhysical)/(1024.*1024.*1024.));
        BackgroundLastTime=Time;
    }
}

void AMotionStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if(!BackgroundTimingRows.IsEmpty())
        UE_LOG(LogTemp,Display,TEXT("MotionVideoTiming final samples=%d\n%s"),BackgroundTimingSamples,*BackgroundTimingRows);
    if(PondEnvironmentDynamic) PondEnvironmentDynamic->SetScalarParameterValue(TEXT("VideoReady"),0.f);
    if(BackgroundPlayer) BackgroundPlayer->Close();
    if(BackgroundVideoTexture) BackgroundVideoTexture->SetMediaPlayer(nullptr);
    Super::EndPlay(EndPlayReason);
}

void AMotionStage::UpdatePose()
{
    // 2D塘体由同一Slate帧绘制。保留旧几何供历史路径，但本模式三个视图均不混入石塘。-codex，2026-11-03
    if(bSpritePondsEnabled)
    {
        if(PondRoot->IsVisible())PondRoot->SetVisibility(false,true);
        for(auto& Root:CityRoots)if(Root->IsVisible())Root->SetVisibility(false,true);
        const FVector Location(0.f,-2250.f,1570.f);
        Camera->SetRelativeLocation(Location);Camera->SetRelativeRotation((-Location).Rotation());
        UpdateEnvironmentPose();return;
    }
    // Actor Tick与Dashboard SetPresentation可能消费同一份帧；相同Serial/Entry不重复传播35个网格变换。
    // 只约束City，数据页水光与单塘原时间路径照常更新。-codex，2026-10-31
    if(CurrentView==EMotionStageView::City&&bCarouselFrameApplied&&
       AppliedCarouselSerial==CarouselFrame.Serial&&AppliedCarouselEntry==Entry)return;
    if(CurrentView==EMotionStageView::City||CurrentView==EMotionStageView::DataOverview)
    {
        UpdateCityPose();
        UpdateEnvironmentPose();
        if(CurrentView==EMotionStageView::City)
        {
            bCarouselFrameApplied=true;
            AppliedCarouselSerial=CarouselFrame.Serial;
            AppliedCarouselEntry=Entry;
        }
        return;
    }
    const float Ease=Entry*Entry*(3.f-2.f*Entry);
    const float Yaw=(1.f-Ease)*-14.f+Ease*4.5f*FMath::Sin(static_cast<float>(MotionTime)*.23f+SitePhase);
    PondRoot->SetRelativeRotation(FRotator(0.f,Yaw,0.f));
    // 已批准V4单塘为左示意、右资料；仅改此取景位置/远近，City/DataOverview在上方独立返回。
    // 原左指标布局由真实主体详情取代；视频及环境跟随机制不变。-codex，2026-10-29
    const FVector Target(-450.f,0.f,30.f);
    const FVector Offset(180.f,-2600.f,1800.f);
    const FVector Location=Target+Offset*FMath::Lerp(1.12f,1.f,Ease);
    Camera->SetRelativeLocation(Location);
    Camera->SetRelativeRotation((Target-Location).Rotation());
    RimLight->SetRelativeLocation(FVector(-180.f,500.f,650.f));
    UpdateEnvironmentPose();
}

void AMotionStage::UpdateEnvironmentPose()
{
    // 不用屏幕UI覆盖3D：远平面始终在塘体后且跟随镜头，只有一次网格构建。-codex，2026-10-24
    if(!Environment||!Camera) return;
    constexpr double Depth=12000.;
    double Aspect=16./9.;
    if(GetWorld())
        if(APlayerController* PC=GetWorld()->GetFirstPlayerController())
        {
            int32 Width=0,Height=0; PC->GetViewportSize(Width,Height);
            if(Width>0&&Height>0) Aspect=double(Width)/Height;
        }
    const FRotator Rotation=Camera->GetRelativeRotation();
    Environment->SetRelativeLocation(Camera->GetRelativeLocation()+Rotation.Vector()*Depth);
    Environment->SetRelativeRotation(Rotation);
    const double HalfWidth=Depth*FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView*.5));
    // 保持16:9原图比例，以cover裁切填满非常规窗口，不能拉伸鱼群和光束。-codex，2026-10-24
    const double ImageHalfWidth=FMath::Max(HalfWidth,HalfWidth/Aspect*(16./9.))*1.005;
    Environment->SetRelativeScale3D(FVector(1.,ImageHalfWidth,ImageHalfWidth/(16./9.)));
    // 仅把实际塘底的投影位置传给背景，形成软接触暗部；不会烘焙模型，也不新建阴影资产。
    // 原图cover裁切时与背景平面共用投影尺寸，避免阴影相对塘体漂移。-codex，2026-10-24
    UMaterialInstanceDynamic* Dynamic=CurrentView==EMotionStageView::DataOverview?DataEnvironmentDynamic.Get():PondEnvironmentDynamic.Get();
    if(!Dynamic) return;
    if(CurrentView==EMotionStageView::City||bSpritePondsEnabled)
    {
        // 原背景只支持六个接触斑；轮转七槽不复用静态暗斑，防止塘已淡出却留下黑影。
        // 不扩写背景材质/播放器；城市阴影观感留待实帧验收。-codex，2026-10-31
        for(int32 Slot=0;Slot<6;++Slot)
            Dynamic->SetVectorParameterValue(*FString::Printf(TEXT("Contact%d"),Slot),FLinearColor(0,0,0,0));
        return;
    }
    const FVector Forward=Rotation.Vector();
    const FVector Right=Rotation.RotateVector(FVector::RightVector);
    const FVector Up=Rotation.RotateVector(FVector::UpVector);
    for(int32 Slot=0;Slot<6;++Slot)
    {
        USceneComponent* Group=CurrentView==EMotionStageView::Pond?(Slot==0?PondRoot.Get():nullptr):CityRoots[Slot].Get();
        FLinearColor Contact(0,0,0,0);
        if(Group&&Group->IsVisible())
        {
            const FVector Bottom=Group->GetRelativeTransform().TransformPosition(FVector(0,-200,-118));
            const FVector Relative=Bottom-Camera->GetRelativeLocation();
            const double PointDepth=FVector::DotProduct(Relative,Forward);
            if(PointDepth>1.)
            {
                const double Projection=Depth/PointDepth;
                Contact.R=.5+FVector::DotProduct(Relative,Right)*Projection/(2.*ImageHalfWidth);
                Contact.G=.5-FVector::DotProduct(Relative,Up)*Projection/(2.*ImageHalfWidth/(16./9.));
                Contact.B=900.*Group->GetRelativeScale3D().X*Projection/(2.*ImageHalfWidth);
            }
        }
        Dynamic->SetVectorParameterValue(*FString::Printf(TEXT("Contact%d"),Slot),Contact);
    }
}

void AMotionStage::UpdateCityPose()
{
    if(CurrentView==EMotionStageView::City){UpdateCarouselPose();return;}
    // 以相机自身+Y/+Z构造固定深度平面，避免再误用右手叉乘；逻辑1920×1080锚点与Slate命中区共用。
    // 城市镜头不消费Entry，不沿用单塘入场的距离变化；此处不解释成真实地理位置。-codex，2026-10-23
    const FVector CameraLocation(0.f,-2250.f,1570.f);
    const FRotator CameraRotation=(-CameraLocation).Rotation();
    Camera->SetRelativeLocation(CameraLocation);
    Camera->SetRelativeRotation(CameraRotation);
    const FVector Forward=CameraRotation.RotateVector(FVector::ForwardVector);
    const FVector Right=CameraRotation.RotateVector(FVector::RightVector);
    const FVector Up=CameraRotation.RotateVector(FVector::UpVector);
    const double Depth=CameraLocation.Size();
    const double WorldPerPixel=Depth*FMath::Tan(FMath::DegreesToRadians(51.f*.5f))/960.;
    constexpr float AnchorX[]={765.f,1135.f,1505.f};
    constexpr float AnchorY[]={320.f,570.f};
    const bool bData=CurrentView==EMotionStageView::DataOverview;
    // 单塘使用现有示意几何等比放大，置于左舞台中央给石沿和环带留白；不改变城市槽位或水纹资产。-codex，2026-10-25
    const FVector2D DataAnchor(680.f,520.f);
    for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot)
    {
        USceneComponent* Group=CityRoots[Slot];
        const bool bHasId=!CurrentCityIds[Slot].IsEmpty();
        const float Delay=.075f*Slot;
        const float Local=FMath::Clamp((static_cast<float>(CityAges[Slot])-Delay)/.70f,0.f,1.f);
        const float Progress=FMath::Min(Entry,Local);
        const float Ease=Progress*Progress*(3.f-2.f*Progress);
        const bool bVisible=bHasId&&Progress>0.f;
        if(Group->IsVisible()!=bVisible) Group->SetVisibility(bVisible,true);
        if(!bHasId) continue;
        const FVector2D Pixel=bData?DataAnchor:FVector2D(AnchorX[Slot%3],AnchorY[Slot/3]);
        const FVector Anchor=CameraLocation+Forward*Depth+
            Right*((Pixel.X-960.f)*WorldPerPixel)+Up*((540.f-Pixel.Y)*WorldPerPixel);
        Group->SetRelativeLocation(Anchor-Up*(28.f*(1.f-Ease)));
        const FVector Scale=bData?FVector(.64f):FVector(.21f);
        Group->SetRelativeScale3D(Scale*FMath::Lerp(.9f,1.f,Ease));
        const float Yaw=-6.f*(1.f-Ease)+Ease*2.3f*FMath::Sin(static_cast<float>(CityAges[Slot])*.31f+CityPhases[Slot]);
        Group->SetRelativeRotation(FRotator(0.f,Yaw+(bData?-5.f:0.f),0.f));
    }
    // 复用现有局部灯照亮右上/中央受光塘；方向光和天光只做较暗基础照明，避免六槽等亮。
    // 局部灯沿水面镜面反射所需的后上方布置，形成随真实法线移动的高光，而非绘制白色光点。-codex，2026-10-24
    const int32 FocusSlot=!bData&&CurrentCityIds.IsValidIndex(1)&&!CurrentCityIds[1].IsEmpty()?1:0;
    RimLight->SetRelativeLocation(CityRoots[FocusSlot]->GetRelativeLocation()+FVector(-100.f,400.f,430.f));
    if(bData) UpdateWaterlight();
}

void AMotionStage::UpdateCarouselPose()
{
    const FVector CameraLocation(0.f,-2250.f,1570.f);
    const FRotator CameraRotation=(-CameraLocation).Rotation();
    Camera->SetRelativeLocation(CameraLocation);
    Camera->SetRelativeRotation(CameraRotation);
    const FVector Forward=CameraRotation.RotateVector(FVector::ForwardVector);
    const FVector Right=CameraRotation.RotateVector(FVector::RightVector);
    const FVector Up=CameraRotation.RotateVector(FVector::UpVector);
    const double BaseDepth=CameraLocation.Size();
    const double TanHalfFov=FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView*.5));
    bool Used[MotionCarousel::PoolSize]={};
    double PreviousFar=0.,MinGap=TNumericLimits<double>::Max();
    double MinLocal=0.,MaxLocal=0.,MinWidth=TNumericLimits<double>::Max();
    int32 Layer=0;
    // Items为远→近的绘制顺序；逆序从最近层开始，交接时只交换层次，保持原Anchor/Scale。
    // 每层整塘Forward区间与上一层至少隔50cm，因此不会出现岸墙切入另一水面。-codex，2026-10-31
    for(int32 ItemNumber=CarouselFrame.Items.Num()-1;ItemNumber>=0;--ItemNumber)
    {
        const auto& Item=CarouselFrame.Items[ItemNumber];
        if(Item.Slot<0||Item.Slot>=MotionCarousel::PoolSize||Used[Item.Slot]||Item.Index<0||
           !FMath::IsFinite(Item.Theta)||!FMath::IsFinite(Item.Depth)||!FMath::IsFinite(Item.Scale)||!FMath::IsFinite(Item.Opacity)||
           !FMath::IsFinite(Item.Anchor.X)||!FMath::IsFinite(Item.Anchor.Y))continue;
        Used[Item.Slot]=true;
        BindCarouselStyle(Item.Slot,Item.Index);
        const auto& Shape=CarouselShapes[CarouselSlotStyles[Item.Slot]];
        const float Alpha=FMath::Clamp(Item.Opacity*Entry,0.f,1.f);
        auto* Root=CityRoots[Item.Slot].Get();
        const bool bVisible=bCarouselMaterialsReady&&!CurrentCityIds[Item.Slot].IsEmpty()&&Alpha>.001f;
        if(Root->IsVisible()!=bVisible)Root->SetVisibility(bVisible,true);
        if(!bVisible)continue;
        // Scale=c*Depth，整塘相机区间为[Depth*(1+c*LocalNear),Depth*(1+c*LocalFar)]。
        // 闭式解下一层Near=前层Far+50；这与两个theta是否同cos无关。-codex，2026-10-31
        const double C=740.*TanHalfFov/(960.*Shape.Width)*FMath::Clamp(double(Item.Scale),.01,2.);
        const double NearFactor=1.+C*Shape.Near,FarFactor=1.+C*Shape.Far;
        const double Depth=NearFactor>0.?FMath::Max(BaseDepth,(PreviousFar+50.)/NearFactor):12000.;
        const double NearFace=Depth*NearFactor,FarFace=Depth*FarFactor;
        if(NearFactor<=0.||FarFace>=11900.)
        {
            // 背景在12000cm，不允许为防穿插把塘推穿背景；超规格输入拒绝显示并明确报错。
            // 正常七槽Scale<=1.01的数学扫描边界由主任务验收。-codex，2026-10-31
            Root->SetVisibility(false,true);
            UE_LOG(LogTemp,Error,TEXT("MotionCarousel layer rejected: serial=%llu slot=%d near=%.2f far=%.2f environment=12000"),CarouselFrame.Serial,Item.Slot,NearFace,FarFace);
            continue;
        }
        if(Layer>0)MinGap=FMath::Min(MinGap,NearFace-PreviousFar);
        PreviousFar=FarFace;
        MinLocal=FMath::Min(MinLocal,Shape.Near);MaxLocal=FMath::Max(MaxLocal,Shape.Far);
        MinWidth=FMath::Min(MinWidth,Shape.Width);
        const double WorldPerPixel=Depth*TanHalfFov/960.;
        const FVector Position=CameraLocation+Forward*Depth+
            Right*((Item.Anchor.X-960.)*WorldPerPixel)+Up*((540.-Item.Anchor.Y)*WorldPerPixel);
        Root->SetRelativeLocation(Position);
        const double Scale=C*Depth;
        Root->SetRelativeScale3D(FVector(Scale));
        Root->SetRelativeRotation(FRotator::ZeroRotator);
        for(int32 Part=0;Part<5;++Part)
        {
            const int32 M=Item.Slot*5+Part;
            CarouselMaterials[M]->SetScalarParameterValue(TEXT("CarouselOpacity"),Alpha);
            // 水/侧面透明排序补充实际深度测试；不透明岸/底仍由真实深度缓冲遮挡。-codex，2026-10-31
            // UE SetTranslucentSortPriority值变化会MarkRenderStateDirty；用稳定名次而非每帧变化的cos数值。
            // 层级交接才重建排序，避免每帧重建最多35个ProceduralMesh scene proxy。-codex，2026-10-31
            const int32 Priority=(MotionCarousel::PoolSize-Layer)*4+(Part==1?2:1);
            CityMeshes[M]->SetTranslucentSortPriority(Priority);
        }
        // 已有Waterlight图只在当前前景渐近Theta=0时建立暖金上岸，不染整条轮转。
        // 继续使用现有MID参数，不修改原图；强度/暖色是否恰当须由原生实帧判断。-codex，2026-10-31
        const float Gain=FMath::Exp(-Item.Theta*Item.Theta/(2.f*.20f*.20f));
        const FTransform Transform=Root->GetComponentTransform();
        const FVector Center=Transform.TransformPosition(FVector(0,0,25.));
        const float WorldScale=float(Transform.GetScale3D().X);
        for(int32 Part=0;Part<3;++Part)
        {
            auto* Material=CarouselMaterials[Item.Slot*5+Part].Get();
            Material->SetScalarParameterValue(TEXT("WaterlightStrength"),Gain*(Part==0?.8f:Part==1?.12f:.16f));
            Material->SetVectorParameterValue(TEXT("WaterlightCenter"),FLinearColor(Center.X,Center.Y,Center.Z,0));
            Material->SetVectorParameterValue(TEXT("WaterlightRadius"),FLinearColor(1400.f*WorldScale,1000.f*WorldScale,80.f*WorldScale,0));
        }
        ++Layer;
    }
    for(int32 Slot=0;Slot<MotionCarousel::PoolSize;++Slot)
        if(!Used[Slot]&&CityRoots[Slot]->IsVisible())CityRoots[Slot]->SetVisibility(false,true);
    if(CarouselFrame.FocusSlot>=0&&CarouselFrame.FocusSlot<MotionCarousel::PoolSize&&Used[CarouselFrame.FocusSlot])
        RimLight->SetRelativeLocation(CityRoots[CarouselFrame.FocusSlot]->GetRelativeLocation()+FVector(-100.f,400.f,430.f));
    if(CarouselLayerLogs<12&&CarouselLoggedFocus!=CarouselFrame.FocusIndex&&Layer>0)
    {
        CarouselLoggedFocus=CarouselFrame.FocusIndex;++CarouselLayerLogs;
        UE_LOG(LogTemp,Display,TEXT("MotionCarousel separated layers: serial=%llu count=%d localNear=%.3f localFar=%.3f width=%.3f minGap=%.3f farthest=%.3f environment=12000"),
            CarouselFrame.Serial,Layer,MinLocal,MaxLocal,MinWidth,Layer>1?MinGap:0.,PreviousFar);
    }
}

void AMotionStage::UpdateWaterlight()
{
    // 入场三秒从塘内掠向后岸，随后5.2秒闭合往返；位置、速度在接缝连续。
    // 灯与材质共用中心，局部增亮不改曝光、不改几何；以下为展示调校而非物理焦散。-codex，2026-10-26
    const double Age=CityAges[0];
    const float Intro=FMath::Clamp(static_cast<float>(Age/3.),0.f,1.f);
    const float Ease=Intro*Intro*(3.f-2.f*Intro);
    const float Phase=static_cast<float>(FMath::Fmod(FMath::Max(0.,Age-3.),5.2)/5.2)*2.f*PI;
    const FVector LocalCenter=Age<3.?
        FVector(FMath::Lerp(420.f,-430.f,Ease),FMath::Lerp(70.f,300.f,Ease),28.f):
        FVector(-430.f*FMath::Cos(Phase),180.f+120.f*FMath::Cos(Phase),28.f);
    const FTransform Transform=CityRoots[0]->GetComponentTransform();
    const FVector Center=Transform.TransformPosition(LocalCenter);
    const float Scale=static_cast<float>(Transform.GetScale3D().X);
    const float Fade=FMath::Clamp(static_cast<float>(Age/.45),0.f,1.f);
    const float Strength=Fade*FMath::Lerp(1.f,.72f,Ease);
    for(UMaterialInstanceDynamic* Dynamic:DataWaterlightMaterials)
    {
        if(!Dynamic) continue;
        Dynamic->SetScalarParameterValue(TEXT("WaterlightStrength"),Strength);
        Dynamic->SetScalarParameterValue(TEXT("WaterlightPhase"),Phase);
        Dynamic->SetVectorParameterValue(TEXT("WaterlightCenter"),FLinearColor(Center.X,Center.Y,Center.Z,0.f));
        Dynamic->SetVectorParameterValue(TEXT("WaterlightRadius"),FLinearColor(230.f*Scale,170.f*Scale,100.f*Scale,0.f));
    }
    RimLight->SetWorldLocation(Center+Transform.TransformVector(FVector(0.f,65.f,260.f)));
    // 首轮实帧左水面和石岸泛白；保留运动轨迹，将局部灯降至约三成并缩小镜面发光面。-codex，2026-10-26
    RimLight->SetIntensity(500.f+250.f*Strength);
    RimLight->SetAttenuationRadius(500.f);
    RimLight->SetSourceRadius(24.f);
}
