// 统一高程网格与表面纹理由 prepare-terrain.py 生成；运行时完全离线。
// 舞台只承担视觉，不参与拾取，城市地形继续使用原有 Actor 归属。-codex，2026-10-18
#include "TerrainScene.h"
#include "FujianMap.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    struct FTerrainGeometry
    {
        TArray<FVector> P,N;
        TArray<FVector2D> UV;
        TArray<FLinearColor> C;
        TArray<int32> I;
        void Quad(const FVector& A,const FVector& B,const FVector& D,const FVector& E,const FLinearColor& Color)
        {
            const int32 K=P.Num(); P.Append({A,B,D,E});
            for(int32 J=0;J<4;J++){N.Add(FVector::UpVector);C.Add(Color);UV.Add(FVector2D::ZeroVector);}
            I.Append({K,K+1,K+2,K,K+2,K+3});
        }
        void Line(FVector A,FVector B,float Width,FLinearColor Color)
        {
            const FVector Side=FVector::CrossProduct((B-A).GetSafeNormal(),FVector::UpVector).GetSafeNormal()*Width*.5;
            Quad(A-Side,A+Side,B+Side,B-Side,Color);
        }
        // 底轮廓有真实厚度，低角度下不再退化为亚像素水平薄片。-codex，2026-10-19
        void Tube(FVector A,FVector B,float Width,FLinearColor Color)
        {
            const FVector Direction=(B-A).GetSafeNormal();
            if(Direction.IsNearlyZero())return;
            const FVector Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal()*Width*.5;
            const FVector Up=FVector::CrossProduct(Side.GetSafeNormal(),Direction)*Width*.5;
            for(int32 J=0;J<6;J++)
            {
                const float T=2*PI*J/6,T2=2*PI*(J+1)/6;
                const FVector P1=Side*FMath::Cos(T)+Up*FMath::Sin(T),P2=Side*FMath::Cos(T2)+Up*FMath::Sin(T2);
                Quad(A+P1,A+P2,B+P2,B+P1,Color);
            }
        }
    };
    UProceduralMeshComponent* AddMesh(AActor* Owner,FTerrainGeometry& G,UMaterialInterface* Material,bool Collision=false)
    {
        auto* M=NewObject<UProceduralMeshComponent>(Owner);
        Owner->AddInstanceComponent(M);
        if(!Owner->GetRootComponent()) Owner->SetRootComponent(M);
        else M->SetupAttachment(Owner->GetRootComponent());
        M->RegisterComponent(); M->SetCollisionEnabled(Collision?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
        M->SetCollisionResponseToAllChannels(ECR_Ignore);
        if(Collision)M->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        M->bUseComplexAsSimpleCollision=true;
        M->CreateMeshSection_LinearColor(0,G.P,G.I,G.N,G.UV,G.C,TArray<FProcMeshTangent>(),Collision);
        M->SetMaterial(0,Material); return M;
    }
    FVector Point(const TSharedPtr<FJsonValue>& V)
    {
        const auto& A=V->AsArray();return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());
    }
}

bool TerrainScene::Build(AFujianMap* Map,FString& Error)
{
    const double Started=FPlatformTime::Seconds();
    const FString Folder=FPaths::ProjectContentDir()/TEXT("Data");
    TArray<uint8> Data; FString Json;
    TSharedPtr<FJsonObject> Meta;
    if(!FFileHelper::LoadFileToArray(Data,*(Folder/TEXT("terrain.bin"))) || !FFileHelper::LoadFileToString(Json,*(Folder/TEXT("terrain.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Meta) || !Meta)
    {Error=TEXT("Missing terrain data. Run python scripts/prepare-terrain.py.");return false;}
    const bool ProvinceOnly=FParse::Param(FCommandLine::Get(),TEXT("PondDemo"))&&!FParse::Param(FCommandLine::Get(),TEXT("PondFullControls"));
    auto* Terrain=LoadObject<UMaterialInterface>(nullptr,ProvinceOnly?TEXT("/Game/Terrain/M_ProvinceTerrain.M_ProvinceTerrain"):TEXT("/Game/Terrain/M_Terrain.M_Terrain"));
    auto* Glow=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Terrain/M_Glow.M_Glow"));
    auto* Stage=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Terrain/M_Stage.M_Stage"));
    auto* Context=ProvinceOnly?LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Terrain/M_ProvinceContext.M_ProvinceContext")):Stage;
    if(!Terrain||!Glow||!Stage||!Context){Error=TEXT("Missing cooked terrain materials. Build terrain assets before packaging.");return false;}
    Map->HeightExaggeration=Meta->GetNumberField(TEXT("verticalExaggeration"));
    // 仅缩短基准面以下的视觉裙边，山地切面仍沿原高程；旧数据缺省 140，不把地表压平。
    // 拦截异常基准元数据；展示海面随同一基准抬升，底轮廓会被海面遮住，改约定时需同步复核取景。-codex，2026-10-22
    double TerrainBase=140;
    double MetadataBase=0;
    if(Meta->TryGetNumberField(TEXT("base"),MetadataBase)&&FMath::IsFinite(MetadataBase)&&MetadataBase>=55&&MetadataBase<=1000)
        TerrainBase=MetadataBase;
    else
        UE_LOG(LogTemp,Warning,TEXT("Terrain base missing or outside [55, 1000]; using legacy 140."));
    const float SkirtBottom=static_cast<float>(TerrainBase-35);
    if(Data.Num()<12){Error=TEXT("Truncated terrain header.");return false;}
    FMemoryReader Reader(Data,true);uint32 Magic=0,Version=0,Count=0;Reader<<Magic<<Version<<Count;
    if(Magic!=0x52544A46||Version!=1||Count!=9){Error=TEXT("Invalid terrain header.");return false;}
    const TArray<TSharedPtr<FJsonValue>>* MetaCities=nullptr;
    if(!Meta->TryGetArrayField(TEXT("cities"),MetaCities)||MetaCities->Num()!=9){Error=TEXT("Invalid terrain metadata city list.");return false;}
    int32 TotalVertices=0,TotalTriangles=0,PlacedSites=0;
    for(uint32 CityIndex=0;CityIndex<Count;CityIndex++)
    {
        if(Reader.TotalSize()-Reader.Tell()<12){Error=TEXT("Truncated terrain city header.");return false;}
        uint32 Code=0,VertexCount=0,IndexCount=0;Reader<<Code<<VertexCount<<IndexCount;
        const int64 Bytes=static_cast<int64>(VertexCount)*32+static_cast<int64>(IndexCount)*4;
        if(VertexCount==0||VertexCount>1000000||IndexCount%3||Bytes>Reader.TotalSize()-Reader.Tell())
        {Error=TEXT("Truncated or oversized terrain mesh.");return false;}
        FDemoCity* City=Map->Cities.FindByPredicate([Code](const FDemoCity& C){return C.Code==static_cast<int32>(Code);});
        if(!City||!City->Region.IsValid()){Error=TEXT("Terrain city does not match map dataset.");return false;}
        FTerrainGeometry G;G.P.Reserve(VertexCount);G.N.Reserve(VertexCount);G.UV.Reserve(VertexCount);
        City->Bounds=FBox(ForceInit);
        for(uint32 J=0;J<VertexCount;J++)
        {
            float X,Y,Z,NX,NY,NZ,U,V; Reader<<X<<Y<<Z<<NX<<NY<<NZ<<U<<V;
            if(!FMath::IsFinite(X)||!FMath::IsFinite(Y)||!FMath::IsFinite(Z)||!FMath::IsFinite(NX)||!FMath::IsFinite(NY)||!FMath::IsFinite(NZ)||!FMath::IsFinite(U)||!FMath::IsFinite(V)){Error=TEXT("Nonfinite terrain vertex.");return false;}
            G.P.Add(FVector(X,Y,Z));G.N.Add(FVector(NX,NY,NZ));G.UV.Add(FVector2D(U,V));City->Bounds+=G.P.Last();
        }
        for(uint32 J=0;J<IndexCount;J++){uint32 Index;Reader<<Index;if(Index>=VertexCount){Error=TEXT("Invalid terrain triangle.");return false;}G.I.Add(Index);}
        auto* Old=Cast<UProceduralMeshComponent>(City->Region->GetRootComponent());
        if(Old){Old->SetVisibility(false);Old->SetCollisionEnabled(ECollisionEnabled::NoCollision);}
        auto* Visual=AddMesh(City->Region.Get(),G,Terrain,true);
        // 每市独立实例只调整地表与边界增益，避免暗化邻区时修改共用材质；组件持有实例供状态切换恢复。-codex，2026-10-22
        auto* CityMaterial=UMaterialInstanceDynamic::Create(Terrain,Visual);
        if(!CityMaterial){Error=TEXT("Cannot create city terrain material instance.");return false;}
        CityMaterial->SetScalarParameterValue(TEXT("SurfaceGain"),1.f);
        CityMaterial->SetScalarParameterValue(TEXT("BoundaryGain"),1.f);
        Visual->SetMaterial(0,CityMaterial);
        City->Terrain=Visual;
        // 以地形三角面获取演示点高度，与同一城市网格一致，不保留旧的固定球体高度。
        for(auto& Site:City->Sites)
        {
            FHitResult Hit;const FVector A(Site.Position.X,Site.Position.Y,5000),B(Site.Position.X,Site.Position.Y,-100);
            if(Visual->LineTraceComponent(Hit,A,B,FCollisionQueryParams(SCENE_QUERY_STAT(TerrainSite),true))){Site.Position.Z=Hit.ImpactPoint.Z+25;PlacedSites++;}
            else {Error=FString::Printf(TEXT("Demo site does not intersect terrain: %s"),*Site.Id);return false;}
        }
        const auto& CityMeta=(*MetaCities)[CityIndex]->AsObject();
        if(CityMeta->GetIntegerField(TEXT("code"))!=City->Code){Error=TEXT("Terrain metadata mismatch.");return false;}
        City->Center.Z=CityMeta->GetNumberField(TEXT("centerHeight"))+35;
        FTerrainGeometry CitySides;
        for(const auto& R:CityMeta->GetArrayField(TEXT("rings")))
        {
            const auto& P=R->AsArray();
            for(int32 J=1;J<P.Num();J++)
            {
                const FVector A=Point(P[J-1])-FVector(0,0,5),B=Point(P[J])-FVector(0,0,5);
                CitySides.Quad(FVector(A.X,A.Y,SkirtBottom),FVector(B.X,B.Y,SkirtBottom),B,A,FLinearColor(.0018,.009,.0123));
            }
        }
        City->Skirt=AddMesh(City->Region.Get(),CitySides,Stage);
        // 省视图只保留省墙；城市墙仅在单市视图显示，避免整片共面深度冲突。
        // 顶部描边由同 UV 材质承担，不再单独生成 DEM 高度的悬浮线。-codex，2026-10-19
        City->Skirt->SetVisibility(false);
        TotalVertices+=VertexCount;TotalTriangles+=IndexCount/3;
    }
    if(Reader.Tell()!=Reader.TotalSize()){Error=TEXT("Unexpected terrain trailing bytes.");return false;}
    AActor* Display=Map->GetWorld()->SpawnActor<AActor>();
    FTerrainGeometry Walls,Edge,Floor,Grid;
    const auto& ProvinceRings=Meta->GetArrayField(TEXT("provinceRings"));
    for(const auto& R:ProvinceRings)
    {
        const auto& P=R->AsArray();
        for(int32 J=1;J<P.Num();J++)
        {
            const FVector A=Point(P[J-1]),B=Point(P[J]);
            Walls.Quad(FVector(A.X,A.Y,SkirtBottom),FVector(B.X,B.Y,SkirtBottom),B-FVector(0,0,5),A-FVector(0,0,5),FLinearColor(.0024,.0135,.0195));
            Edge.Tube(FVector(A.X,A.Y,TerrainBase-40),FVector(B.X,B.Y,TerrainBase-40),4,FLinearColor(.01,.28,.4));
            Edge.Tube(FVector(A.X,A.Y,TerrainBase-55),FVector(B.X,B.Y,TerrainBase-55),2,FLinearColor(.005,.07,.13));
        }
    }
    AddMesh(Display,Walls,Stage);AddMesh(Display,Edge,Glow);
    Map->ProvinceDecor=Display;
    AActor* Ground=Map->GetWorld()->SpawnActor<AActor>();
    // 复用无碰撞地台作展示海背景，贴近地表基准以遮住悬浮裙边；高度不代表真实海深。
    // 保留 Ground 两组件且不参与相机拟合，省装饰仍恰好为墙/描边两组件。-codex，2026-10-22
    const float SeaHeight=static_cast<float>(TerrainBase-8);
    const float GridHeight=static_cast<float>(TerrainBase-6);
    const float RingHeight=static_cast<float>(TerrainBase-4);
    auto FloorColor=[](float X,float Y){const float T=FMath::Exp(-(X*X+Y*Y)/16000000.f);return FLinearColor(.0012+T*.0024,.0024+T*.0072,.009+T*.022);};
    for(int32 X=-20;X<20;X++)for(int32 Y=-20;Y<20;Y++)
    {
        const float A=X*500,B=Y*500;
        const int32 K=Floor.C.Num();
        Floor.Quad(FVector(A,B,SeaHeight),FVector(A+500,B,SeaHeight),FVector(A+500,B+500,SeaHeight),FVector(A,B+500,SeaHeight),FLinearColor::Black);
        Floor.C[K]=FloorColor(A,B);Floor.C[K+1]=FloorColor(A+500,B);Floor.C[K+2]=FloorColor(A+500,B+500);Floor.C[K+3]=FloorColor(A,B+500);
    }
    for(int32 J=-12;J<=12;J++)
    {
        const float Fade=FMath::Max(.04f,1-FMath::Abs(J)/13.f);
        Grid.Line(FVector(-6000,J*500,GridHeight),FVector(6000,J*500,GridHeight),.7,FLinearColor(.0006,.004,.007)*Fade);
        Grid.Line(FVector(J*500,-6000,GridHeight),FVector(J*500,6000,GridHeight),.7,FLinearColor(.0006,.004,.007)*Fade);
    }
    for(float Radius:{2800.f,3200.f,3600.f})for(int32 J=0;J<360;J++)
    {
        const float A=FMath::DegreesToRadians(static_cast<float>(J)),B=FMath::DegreesToRadians(static_cast<float>(J+1));
        Grid.Line(FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,RingHeight),FVector(FMath::Cos(B)*Radius,FMath::Sin(B)*Radius,RingHeight),2.5,FLinearColor(.0008,.0054,.008));
    }
    // 背景沿用无碰撞地台，材质按同投影的世界坐标采样周边海陆，不生成虚构山体。-codex，2026-10-27
    AddMesh(Ground,Floor,Context);
    if(!ProvinceOnly)AddMesh(Ground,Grid,Glow);
    UE_LOG(LogTemp,Display,TEXT("Province surface=%s context=%s original_geometry=1"),*Terrain->GetPathName(),*Context->GetPathName());
    UE_LOG(LogTemp,Display,TEXT("Terrain ready: %d vertices, %d triangles, %.0fx height, %d grounded sites, %.2f s; materials cooked, offline data loaded."),TotalVertices,TotalTriangles,Map->HeightExaggeration,PlacedSites,FPlatformTime::Seconds()-Started);
    UE_LOG(LogTemp,Display,TEXT("Stable boundaries: surface mask, constant emission, province/city skirts exclusive; %d base-edge triangles."),Edge.I.Num()/3);
    return true;
}
