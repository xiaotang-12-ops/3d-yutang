// 直接使用 UE 原生组件，不嵌入网页，也不依赖编辑器 Python。
// 所有点位/面积为模拟值；两端统计和网格统一读取 shared 生成的数据。-codex，2026-10-16
#include "FujianMap.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ProceduralMeshComponent.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "TerrainScene.h"
#include "PondPresentation.h"

namespace
{
    // shared 的 X=东、Y=北、Z=高，转成 UE 的 X=北、Y=东、Z=高，并乘 10 厘米。
    // UE 正面绕序与共享网格不同；XY 反射后保留索引，符合引擎 GenerateBoxMesh 的顶面绕序。
    // 法线同步交换 XY。初始相机从 -X 看向北，因此东在屏幕右。-codex，2026-10-16
    constexpr double UnitScale = 10.0;
    const FLinearColor TextColor(.72f, .86f, .87f);
    const FLinearColor Accent(.3f, .9f, .7f);
    TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& Value) { return Value.IsValid() ? Value->AsObject() : nullptr; }
    FVector ToUnreal(double East, double North, double Height) { return FVector(North,East,Height)*UnitScale; }
    // 复用引擎基础材质的实际向量参数，不创建第二套材质资源或依赖编辑器运行。-codex，2026-10-18
    UMaterialInterface* TintedMaterial(UMaterialInterface* Parent, UObject* Owner, const FLinearColor& Color)
    {
        if (!Parent) return nullptr;
        UMaterialInstanceDynamic* Material=UMaterialInstanceDynamic::Create(Parent,Owner);
        TArray<FMaterialParameterInfo> Parameters; TArray<FGuid> Ids;
        Parent->GetAllVectorParameterInfo(Parameters,Ids);
        for (const auto& Parameter:Parameters)
            if (Parameter.Name==TEXT("Color") || Parameter.Name==TEXT("BaseColor")) Material->SetVectorParameterValue(Parameter.Name,Color);
        return Material;
    }
}

AFujianMap::AFujianMap() { PrimaryActorTick.bCanEverTick = false; }

void AFujianMap::BeginPlay()
{
    Super::BeginPlay();
    FString Json;
    const FString DataPath = FPaths::ProjectContentDir() / TEXT("Data/map-data.json");
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *DataPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    {
        Error = TEXT("Cannot load Content/Data/map-data.json. Run npm run data.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
        return;
    }
    if (Root->GetIntegerField(TEXT("schemaVersion")) != 1 || !Root->GetBoolField(TEXT("demo")))
    {
        Error = TEXT("Unsupported demo data contract.");
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* CityValues = nullptr;
    if (!Root->TryGetArrayField(TEXT("cities"), CityValues) || CityValues->Num() != 9)
    {
        Error = TEXT("Expected all nine Fujian cities.");
        return;
    }
    UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Surface) { Error=TEXT("Missing cooked BasicShapeMaterial."); UE_LOG(LogTemp,Error,TEXT("%s"),*Error); return; }
    TArray<FMaterialParameterInfo> SurfaceParameters; TArray<FGuid> SurfaceIds;
    Surface->GetAllVectorParameterInfo(SurfaceParameters,SurfaceIds);
    for (const auto& Parameter:SurfaceParameters) UE_LOG(LogTemp,Display,TEXT("Surface vector parameter: %s"),*Parameter.Name.ToString());
    MarkerMaterial=TintedMaterial(Surface,this,FLinearColor(.12f,.8f,.48f));
    SelectedMarkerMaterial=TintedMaterial(Surface,this,FLinearColor(1.f,.55f,.06f));
    const FLinearColor CityColors[] = {
        {.035f,.30f,.37f},{.10f,.45f,.50f},{.12f,.35f,.49f},
        {.05f,.24f,.30f},{.035f,.42f,.39f},{.08f,.31f,.45f},
        {.04f,.36f,.31f},{.07f,.26f,.39f},{.10f,.40f,.45f}
    };
    for (const TSharedPtr<FJsonValue>& Value : *CityValues)
    {
        const TSharedPtr<FJsonObject> Object = AsObject(Value);
        if (!Object.IsValid()) { Error = TEXT("Invalid city object."); return; }
        FDemoCity City;
        City.Code = Object->GetIntegerField(TEXT("code"));
        City.Name = Object->GetStringField(TEXT("name"));
        City.English = Object->GetStringField(TEXT("english"));
        const auto& Center = Object->GetArrayField(TEXT("center"));
        City.Center = ToUnreal(Center[0]->AsNumber(),Center[1]->AsNumber(),9.0);
        const auto MeshObject = Object->GetObjectField(TEXT("mesh"));
        const auto& RawVertices = MeshObject->GetArrayField(TEXT("vertices"));
        const auto& RawNormals = MeshObject->GetArrayField(TEXT("normals"));
        const auto& RawTriangles = MeshObject->GetArrayField(TEXT("triangles"));
        if (RawVertices.Num() == 0 || RawVertices.Num()%3 != 0 || RawNormals.Num() != RawVertices.Num() || RawTriangles.Num()%3 != 0)
        { Error = TEXT("Invalid triangle mesh contract."); return; }
        TArray<FVector> Vertices, Normals;
        TArray<int32> Triangles;
        for (int32 I=0; I<RawVertices.Num(); I+=3)
        {
            FVector Vertex=ToUnreal(RawVertices[I]->AsNumber(),RawVertices[I+1]->AsNumber(),RawVertices[I+2]->AsNumber());
            Vertices.Add(Vertex); City.Bounds += Vertex;
            Normals.Add(FVector(RawNormals[I+1]->AsNumber(),RawNormals[I]->AsNumber(),RawNormals[I+2]->AsNumber()));
        }
        for (int32 I=0; I<RawTriangles.Num(); I+=3)
        {
            const int32 A=static_cast<int32>(RawTriangles[I]->AsNumber()), B=static_cast<int32>(RawTriangles[I+1]->AsNumber()), C=static_cast<int32>(RawTriangles[I+2]->AsNumber());
            if (!Vertices.IsValidIndex(A)||!Vertices.IsValidIndex(B)||!Vertices.IsValidIndex(C)) { Error=TEXT("Mesh index out of range.");return; }
            Triangles.Append({A,B,C});
        }
        AActor* Region = GetWorld()->SpawnActor<AActor>();
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Region);
        Region->SetRootComponent(Mesh); Region->AddInstanceComponent(Mesh); Mesh->RegisterComponent();
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToAllChannels(ECR_Ignore); Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Mesh->bUseComplexAsSimpleCollision = true;
        Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);
        Mesh->SetMaterial(0, TintedMaterial(Surface,Region,CityColors[Cities.Num()]));
        City.Region = Region;
        for (const TSharedPtr<FJsonValue>& SiteValue : Object->GetArrayField(TEXT("sites")))
        {
            const auto SiteObject=SiteValue->AsObject(); const auto& Position=SiteObject->GetArrayField(TEXT("position"));
            FDemoSite Site; Site.Id=SiteObject->GetStringField(TEXT("id")); Site.Name=SiteObject->GetStringField(TEXT("name")); Site.Kind=SiteObject->GetStringField(TEXT("kind")); Site.AreaMu=SiteObject->GetIntegerField(TEXT("areaMu"));
            Site.Position=ToUnreal(Position[0]->AsNumber(),Position[1]->AsNumber(),13.5); City.Sites.Add(Site);
        }
        Total += City.Sites.Num(); Cities.Add(MoveTemp(City));
    }
    if (Total!=Root->GetIntegerField(TEXT("total"))) { Error=TEXT("Demo point totals are inconsistent."); }
    if(!TerrainScene::Build(this,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return;}
    ADirectionalLight* Light = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,4000),FRotator(-65,-35,0));
    if (Light) { Light->GetLightComponent()->SetIntensity(2.6f); Light->SetActorRotation(FRotator(-38,-42,0)); CastChecked<UDirectionalLightComponent>(Light->GetLightComponent())->SetForwardShadingPriority(10); }
    ADirectionalLight* Fill = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,3000),FRotator(-35,140,0));
    if (Fill) { Fill->GetLightComponent()->SetIntensity(.9f); Fill->GetLightComponent()->SetLightColor(FLinearColor(.42f,.7f,1.f)); }
    UE_LOG(LogTemp, Display, TEXT("Fujian native demo loaded: %d cities, %d simulated sites."),Cities.Num(),Total);
}

void AFujianMap::ClearMarkers() { for (auto& Marker:Markers) if (Marker.IsValid()) Marker->Destroy(); Markers.Empty(); }
void AFujianMap::ShowProvince() { ClearMarkers(); SelectedCity=SelectedSite=INDEX_NONE; if(ProvinceDecor.IsValid())ProvinceDecor->SetActorHiddenInGame(false); for(auto& City:Cities) if(City.Region.IsValid()) {City.Region->SetActorHiddenInGame(false);City.Region->SetActorEnableCollision(true);if(City.Skirt.IsValid())City.Skirt->SetVisibility(false);} UE_LOG(LogTemp,Display,TEXT("Province view: %d cities, %d demo sites."),Cities.Num(),Total); }
void AFujianMap::ShowCity(int32 Index)
{
    if (!Cities.IsValidIndex(Index)) return;
    ClearMarkers();SelectedCity=Index;SelectedSite=INDEX_NONE;
    if(ProvinceDecor.IsValid())ProvinceDecor->SetActorHiddenInGame(true);
    for(int32 I=0;I<Cities.Num();I++) if(Cities[I].Region.IsValid()) {Cities[I].Region->SetActorHiddenInGame(I!=Index);Cities[I].Region->SetActorEnableCollision(I==Index);if(Cities[I].Skirt.IsValid())Cities[I].Skirt->SetVisibility(I==Index);}
    UStaticMesh* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for(const FDemoSite& Site:Cities[Index].Sites)
    {
        AActor* Marker=GetWorld()->SpawnActor<AActor>();
        UStaticMeshComponent* Mesh=NewObject<UStaticMeshComponent>(Marker); Marker->SetRootComponent(Mesh);Marker->AddInstanceComponent(Mesh);Mesh->RegisterComponent();
        Mesh->SetStaticMesh(Sphere);Mesh->SetWorldScale3D(FVector(.22));Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Marker->SetActorLocation(Site.Position);Markers.Add(Marker);
        Mesh->SetMaterial(0,MarkerMaterial);
    }
    UE_LOG(LogTemp,Display,TEXT("City view: %s (%d), %d demo sites."),*Cities[Index].English,Cities[Index].Code,Markers.Num());
}
void AFujianMap::Pick(AActor* Actor)
{
    if(SelectedCity==INDEX_NONE) {for(int32 I=0;I<Cities.Num();I++)if(Cities[I].Region.Get()==Actor){ShowCity(I);return;}}
    else {for(int32 I=0;I<Markers.Num();I++)if(Markers[I].Get()==Actor){SelectSite(I);UE_LOG(LogTemp,Display,TEXT("Ray-picked demo site: %s"),*Cities[SelectedCity].Sites[I].Id);return;}}
}
void AFujianMap::SelectSite(int32 Index)
{
    if (!Cities.IsValidIndex(SelectedCity) || !Cities[SelectedCity].Sites.IsValidIndex(Index)) return;
    SelectedSite=Index;
    for(int32 I=0;I<Markers.Num();I++) if(Markers[I].IsValid())
        if(auto* Mesh=Cast<UStaticMeshComponent>(Markers[I]->GetRootComponent()))
        {Mesh->SetMaterial(0,I==Index?SelectedMarkerMaterial:MarkerMaterial);Mesh->SetWorldScale3D(FVector(I==Index?.32f:.22f));}
    UE_LOG(LogTemp,Display,TEXT("Selected demo site: %s"),*Cities[SelectedCity].Sites[Index].Id);
}
FBox AFujianMap::VisibleBounds() const
{
    if(Cities.IsValidIndex(SelectedCity))return Cities[SelectedCity].Bounds;
    FBox Bounds(ForceInit);for(const auto& City:Cities)Bounds+=City.Bounds;return Bounds;
}

void AFujianController::BeginPlay()
{
    Super::BeginPlay();bShowMouseCursor=true;
    bShowcase=FParse::Param(FCommandLine::Get(),TEXT("PondDemo"));
    if(bShowcase){Pitch=40.f;Yaw=135.f;}
    FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
    Map=GetWorld()->SpawnActor<AFujianMap>();MapCamera=GetWorld()->SpawnActor<ACameraActor>();
    MapCamera->GetCameraComponent()->SetFieldOfView(40.f);SetViewTarget(MapCamera);FitMap();Focus=WantedFocus;Distance=WantedDistance;
    LastActivity=GetWorld()->GetTimeSeconds();
    Motion.SetPose(CurrentPose());
    if(bShowcase)
    {
        FocusSelection(false,false);ApplyPose(Motion.Advance(10.f));
        Presentation=MakeShared<FPondPresentation>(this);Presentation->Load(Map);
        if(GEngine)GEngine->SetMaxFPS(60.f);
    }
}
void AFujianController::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Presentation.IsValid()){Presentation->Shutdown();Presentation.Reset();}
    Super::EndPlay(Reason);
}
void AFujianController::OpenData(){if(Presentation.IsValid())Presentation->Show();}
FString AFujianController::DataWindowStatus()const{return Presentation.IsValid()?Presentation->WindowStatus():TEXT("数据屏未启用");}
void AFujianController::FitMap(bool bTop)
{
    if(!Map||Map->Cities.IsEmpty())return;
    if(bShowcase){FocusSelection(bTop,false);return;}
    const FBox Box=Map->VisibleBounds();const FVector Center=Box.GetCenter();
    int32 Width=1920,Height=1080;GetViewportSize(Width,Height);
    // 首次 BeginPlay 可能早于窗口创建；0x0 时不能除法，也不能把 NaN 传入相机。-codex，2026-10-18
    if(Width<=0 || Height<=0) return;
    FittedViewport=FIntPoint(Width,Height);
    const float Aspect=FMath::Max(.5f,static_cast<float>(Width)/FMath::Max(Height,1));
    Pitch=bTop?89.f:38.f;Yaw=180.f;
    // UE 的 FOV 为水平视角；按八个包围盒角点投影拟合，给全宽地图与上下操作区留白。
    // 旧经验倍数忽略纵向 FOV，导致福州下钻与俯视把地图裁到窗口之外。-codex，2026-10-18
    const FRotationMatrix ViewRotation(FRotator(-Pitch,0,0));
    const FVector Forward=ViewRotation.GetUnitAxis(EAxis::X),Right=ViewRotation.GetUnitAxis(EAxis::Y),Up=ViewRotation.GetUnitAxis(EAxis::Z);
    const float TanH=FMath::Tan(FMath::DegreesToRadians(20.f)),TanV=TanH/Aspect;
    const float UsableX=.87f,UsableY=FMath::Max(.3f,(Height-190.f)/Height);
    WantedDistance=600.f;
    for(int32 I=0;I<8;I++)
    {
        const FVector Delta=FVector(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,I&4?Box.Max.Z:Box.Min.Z)-Center;
        const float Depth=FVector::DotProduct(Delta,Forward);
        WantedDistance=FMath::Max(WantedDistance,FMath::Max(FMath::Abs(FVector::DotProduct(Delta,Right))/(TanH*UsableX)-Depth,FMath::Abs(FVector::DotProduct(Delta,Up))/(TanV*UsableY)-Depth));
    }
    WantedFocus=Center+Up*(WantedDistance*TanV*12.f/Height);
    UE_LOG(LogTemp,Display,TEXT("Fit map: %s, distance %.0f, viewport %dx%d"),bTop?TEXT("top"):TEXT("perspective"),WantedDistance,Width,Height);
}
void AFujianController::Home(){if(Map){CancelCamera(TEXT("home"));Map->ShowProvince();FitMap();}}
void AFujianController::UpdatePointerDrag(FVector2D Position)
{
    const FVector2D Delta=Position-PreviousPosition;
    DragDistance+=Delta.Size();
    int32 Width=0,Height=0;GetViewportSize(Width,Height);
    if(PressPosition.Y>88 && PressPosition.Y<Height-65)
    {
        Yaw-=Delta.X*.2f;
        Pitch=FMath::Clamp(Pitch+Delta.Y*.2f,bShowcase?25.f:15.f,89.f);
    }
    PreviousPosition=Position;
    if(!Delta.IsNearlyZero()) LastActivity=GetWorld()->GetTimeSeconds();
}
bool AFujianController::InputKey(const FInputKeyEventArgs& Params)
{
    // 演示快捷键先消费，避免引擎F2/F5调试绑定同时切换渲染模式。-codex，2026-10-21
    if(bShowcase&&(Params.Key==EKeys::F2||Params.Key==EKeys::F5))
    {
        if(Params.Event==IE_Pressed)
        {
            if(Params.Key==EKeys::F2)OpenData();
            else if(Presentation.IsValid())Presentation->Load(Map);
        }
        return true;
    }
    const bool Handled=Super::InputKey(Params);
    if(!Map || Params.Key!=EKeys::LeftMouseButton) return Handled;
    float MX=0,MY=0;
    if(!GetMousePosition(MX,MY)) return Handled;
    const FVector2D Mouse(MX,MY);
    if(bShowcase)UE_LOG(LogTemp,Display,TEXT("PondPointer event=%d pos=%.1f,%.1f down=%d drag=%.1f"),static_cast<int32>(Params.Event),MX,MY,bPointerDown,DragDistance);
    // 保存输入事件的起终点，避免整个短拖动落在两帧之间而被逐帧轮询漏掉。
    // 松开时只补尚未处理的末段，防止与连续帧重复计算。-codex，2026-10-18
    if(Params.Event==IE_Pressed)
    {
        if(bShowcase)CancelCamera(TEXT("pointer"));
        bPointerDown=true;PressPosition=PreviousPosition=Mouse;DragDistance=0;
        LastActivity=GetWorld()->GetTimeSeconds();
    }
    else if(Params.Event==IE_Released && bPointerDown)
    {
        UpdatePointerDrag(Mouse);bPointerDown=false;
        if(DragDistance<6.f)
        {
            PickAt(Mouse);
        }
        else UE_LOG(LogTemp,Display,TEXT("Pointer drag: %.0f px, yaw %.1f, pitch %.1f"),DragDistance,Yaw,Pitch);
    }
    return true;
}
// 鼠标与单指触摸复用拾取/拖动，第二根手指不抢占；物理触屏仍需目标设备验收。-codex，2026-10-21
void AFujianController::PickAt(FVector2D Position)
{
    auto* HUD=Cast<AFujianHUD>(GetHUD());const bool HUDHit=HUD&&HUD->ClickAt(Position);
    if(bShowcase)UE_LOG(LogTemp,Display,TEXT("PondPointer pick=%.1f,%.1f hud=%d city=%d site=%d"),Position.X,Position.Y,HUDHit,Map->SelectedCity,Map->SelectedSite);
    if(HUDHit)return;
    FHitResult Hit;if(!GetHitResultAtScreenPosition(Position,ECC_Visibility,true,Hit))return;
    const int32 Previous=Map->SelectedCity;Map->Pick(Hit.GetActor());
    if(!bShowcase&&Previous!=Map->SelectedCity)FitMap();
}
bool AFujianController::InputTouch(FInputDeviceId DeviceId,uint32 Handle,ETouchType::Type Type,const FVector2D& Location,float Force,uint32 TouchpadIndex,uint64 Timestamp)
{
    if(!bShowcase)return Super::InputTouch(DeviceId,Handle,Type,Location,Force,TouchpadIndex,Timestamp);
    if(Type==ETouchType::Began&&!bTouchDown)
    {
        CancelCamera(TEXT("touch"));bTouchDown=true;ActiveTouchHandle=Handle;
        PressPosition=PreviousPosition=Location;DragDistance=0;LastActivity=GetWorld()->GetTimeSeconds();return true;
    }
    if(!bTouchDown||Handle!=ActiveTouchHandle)return true;
    if(Type==ETouchType::Moved||Type==ETouchType::Ended)UpdatePointerDrag(Location);
    if(Type==ETouchType::Ended){bTouchDown=false;ActiveTouchHandle=MAX_uint32;if(DragDistance<6.f&&Map)PickAt(Location);}
    return true;
}
void AFujianController::FlushPressedKeys()
{
    if(bPointerDown||bTouchDown)CancelCamera(TEXT("focus-lost"));
    bPointerDown=bTouchDown=false;ActiveTouchHandle=MAX_uint32;Super::FlushPressedKeys();
}
void AFujianController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);if(!Map||!MapCamera)return;
    FIntPoint Viewport;GetViewportSize(Viewport.X,Viewport.Y);
    if(Viewport.X>0 && Viewport.Y>0 && Viewport!=FittedViewport)
    {
        const bool FirstFit=FittedViewport==FIntPoint::ZeroValue;
        if(bShowcase){CancelCamera(TEXT("resize"));FocusSelection(Pitch>=75.f,false,true);}else FitMap(Pitch>88.f);
        if(FirstFit){Focus=WantedFocus;Distance=WantedDistance;}
    }
    float MX=0,MY=0;GetMousePosition(MX,MY);const FVector2D Mouse(MX,MY);
    if(bPointerDown&&IsInputKeyDown(EKeys::LeftMouseButton)) UpdatePointerDrag(Mouse);
    if(WasInputKeyJustPressed(EKeys::MouseScrollUp)){if(bShowcase)CancelCamera(TEXT("wheel"));WantedDistance=FMath::Max(bShowcase?400.f:200.f,WantedDistance*.85f);if(bShowcase)Distance=WantedDistance;LastActivity=GetWorld()->GetTimeSeconds();}
    if(WasInputKeyJustPressed(EKeys::MouseScrollDown)){if(bShowcase)CancelCamera(TEXT("wheel"));WantedDistance=FMath::Min(22000.f,WantedDistance*1.15f);if(bShowcase)Distance=WantedDistance;LastActivity=GetWorld()->GetTimeSeconds();}
    if(WasInputKeyJustPressed(EKeys::Escape)){if(bShowcase)Back();else Home();LastActivity=GetWorld()->GetTimeSeconds();}
    if(WasInputKeyJustPressed(EKeys::Home)){Home();LastActivity=GetWorld()->GetTimeSeconds();}
    if(!bShowcase&&Map->SelectedCity!=INDEX_NONE&&GetWorld()->GetTimeSeconds()-LastActivity>60.0){UE_LOG(LogTemp,Display,TEXT("Idle timeout: returning to province after 60 seconds."));Home();}
    if(bShowcase)
    {
        if(FramedCity!=Map->SelectedCity||FramedSite!=Map->SelectedSite)FocusSelection();
        if(Motion.IsActive())ApplyPose(Motion.Advance(DeltaSeconds));else Motion.SetPose(CurrentPose());
        if(Presentation.IsValid())Presentation->Tick(DeltaSeconds);
        if(GetWorld()->GetTimeSeconds()>NextMotionLog)
        {
            UE_LOG(LogTemp,Display,TEXT("PondCamera phase=%s city=%d site=%d focus=%s distance=%.2f yaw=%.2f pitch=%.2f"),Motion.Phase(),Map->SelectedCity,Map->SelectedSite,*Focus.ToString(),Distance,Yaw,Pitch);
            NextMotionLog=GetWorld()->GetTimeSeconds()+1;
        }
    }
    else {Focus=FMath::VInterpTo(Focus,WantedFocus,DeltaSeconds,5.f);Distance=FMath::FInterpTo(Distance,WantedDistance,DeltaSeconds,5.f);}
    const float YawRadians=FMath::DegreesToRadians(Yaw),PitchRadians=FMath::DegreesToRadians(Pitch);
    const FVector Offset(Distance*FMath::Cos(PitchRadians)*FMath::Cos(YawRadians),Distance*FMath::Cos(PitchRadians)*FMath::Sin(YawRadians),Distance*FMath::Sin(PitchRadians));
    const FVector CameraLocation=Focus+Offset;MapCamera->SetActorLocationAndRotation(CameraLocation,(Focus-CameraLocation).Rotation());
}

// 本阶段只观察沙盘；数量和详情面板暂不展示，省市拾取仍复用原生状态。-codex，2026-10-18
void AFujianHUD::DrawHUD()
{
    Super::DrawHUD();
    if(!Canvas)return;
    auto* PC=Cast<AFujianController>(GetOwningPlayerController());
    if(!PC||!PC->Map)return;
    if(PC->IsShowcase()){DrawShowcase();return;}
    auto* Map=PC->Map.Get();
    const float W=Canvas->ClipX,H=Canvas->ClipY;
    const FLinearColor Cyan(.15f,.78f,.9f);
    DrawText(TEXT("FUJIAN  /  DIGITAL TERRAIN"),FLinearColor(.28f,.62f,.69f),38,23,GEngine->GetSmallFont(),.95f);
    const bool CityView=Map->Cities.IsValidIndex(Map->SelectedCity);
    DrawText(CityView?Map->Cities[Map->SelectedCity].Name:TEXT("福建 · 山海之间"),FLinearColor(.75f,.91f,.91f),36,47,GEngine->GetLargeFont(),1.7f);
    DrawLine(38,93,225,93,Cyan,1.5f);
    DrawText(FString::Printf(TEXT("公开高程沙盘 / 地形纵向展示放大 %.0f×"),Map->HeightExaggeration),FLinearColor(.25f,.48f,.53f),38,109,GEngine->GetSmallFont(),.9f);
    if(!Map->Error.IsEmpty())DrawText(Map->Error,FLinearColor::Red,40,150,GEngine->GetMediumFont(),1.f);
    auto Button=[this,H](const FString& Text,float X,float Width)
    {
        DrawRect(FLinearColor(.006f,.03f,.045f,.88f),X,H-64,Width,34);
        DrawLine(X,H-64,X+Width,H-64,FLinearColor(.02f,.24f,.3f),1.f);
        DrawText(Text,FLinearColor(.4f,.7f,.74f),X+13,H-56,GEngine->GetSmallFont(),.95f);
        return FBox2D(FVector2D(X,H-64),FVector2D(X+Width,H-30));
    };
    HomeButton=Button(TEXT("全省"),38,82);ResetButton=Button(TEXT("重置视角"),130,110);TopButton=Button(TEXT("俯视"),250,82);
    CityButtons.Empty();SiteButtons.Empty();
    if(!CityView)for(const auto& City:Map->Cities)
    {
        FVector2D P;
        if(PC->ProjectWorldLocationToScreen(City.Center+FVector(0,0,70),P))
        {
            DrawRect(FLinearColor(.002f,.016f,.025f,.72f),P.X-30,P.Y-3,65,23);
            DrawLine(P.X-34,P.Y+8,P.X-29,P.Y+8,Cyan,2.f);
            DrawText(City.Name,FLinearColor(.55f,.83f,.83f),P.X-24,P.Y,GEngine->GetSmallFont(),1.f);
        }
    }
    DrawText(TEXT("拖动旋转   /   滚轮缩放   /   点击城市   /   ESC 返回"),FLinearColor(.24f,.42f,.49f),W-490,H-54,GEngine->GetSmallFont(),.9f);
    DrawText(TEXT("公开开发版 · 示意地形，使用自备数据时请核对来源"),FLinearColor(.16f,.3f,.36f),38,H-19,GEngine->GetSmallFont(),.7f);
}
bool AFujianHUD::ClickAt(FVector2D Position)
{
    auto* PC=Cast<AFujianController>(GetOwningPlayerController());if(!PC||!PC->Map)return false;
    if(PC->IsShowcase())return ClickShowcase(Position);
    if(HomeButton.IsInside(Position)){PC->Home();return true;}
    if(ResetButton.IsInside(Position)){PC->FitMap();return true;}
    if(TopButton.IsInside(Position)){PC->FitMap(true);return true;}
    return Canvas&&(Position.Y<88||Position.Y>Canvas->ClipY-26);
}
AFujianSandboxGameMode::AFujianSandboxGameMode(){PlayerControllerClass=AFujianController::StaticClass();HUDClass=AFujianHUD::StaticClass();DefaultPawnClass=nullptr;}
