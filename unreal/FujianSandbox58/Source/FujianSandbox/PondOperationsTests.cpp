// 对真实加载器做契约与聚合回归；夹具只读取正式JSON，坏数据仅在内存中修改。
// 临时世界不BeginPlay、不创建窗口、不改当前地图或正式台账。-codex，2026-10-21
#include "PondPresentation.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "FujianMap.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

struct FPondOperationsTestAccess
{
    static bool Load(FPondPresentation& P,AFujianMap* Map,const FString& Json){return P.LoadJson(Map,Json);}
    static void Refresh(FPondPresentation& P,AFujianMap* Map){P.Refresh(Map);}
};
namespace
{
    struct FOperationsFixture
    {
        UWorld* World=nullptr;
        AFujianMap* Map=nullptr;
        FString Operations;
        ~FOperationsFixture(){if(World)World->DestroyWorld(false);}
        bool Initialize()
        {
            FString Json;TSharedPtr<FJsonObject> Root;
            if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Data/map-data.json")))||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid()||
                !FFileHelper::LoadFileToString(Operations,*(FPaths::ProjectContentDir()/TEXT("Data/pond-operations.json"))))return false;
            UWorld::InitializationValues IV;
            IV.InitializeScenes(false).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&IV);
            if(!World)return false;
            Map=World->SpawnActor<AFujianMap>();if(!Map)return false;
            // 仅填地图加载器的业务输入，避开BeginPlay里的材质、地形和标记创建。-codex，2026-10-21
            for(const auto& CV:Root->GetArrayField(TEXT("cities")))
            {
                const auto C=CV->AsObject();FDemoCity City;City.Code=C->GetIntegerField(TEXT("code"));City.Name=C->GetStringField(TEXT("name"));
                for(const auto& SV:C->GetArrayField(TEXT("sites")))
                {
                    const auto S=SV->AsObject();FDemoSite Site;Site.Id=S->GetStringField(TEXT("id"));Site.Name=S->GetStringField(TEXT("name"));Site.Kind=S->GetStringField(TEXT("kind"));Site.AreaMu=S->GetIntegerField(TEXT("areaMu"));
                    City.Sites.Add(MoveTemp(Site));Map->Total++;
                }
                Map->Cities.Add(MoveTemp(City));
            }
            return true;
        }
        FString Changed(TFunctionRef<void(TSharedPtr<FJsonObject>)> Mutate) const
        {
            TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Operations),Root);
            Mutate(Root);FString Json;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));return Json;
        }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPondOperationsAggregationTest,"Fujian.Operations.RealLedgerAggregation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPondOperationsAggregationTest::RunTest(const FString&)
{
    FOperationsFixture F;if(!TestTrue(TEXT("real ledger and isolated world available"),F.Initialize()))return false;
    FPondPresentation P(nullptr);if(!TestTrue(TEXT("production file loader accepts ledger"),P.Load(F.Map)))return false;
    FPondOperationsTestAccess::Refresh(P,F.Map);
    TestEqual(TEXT("province ponds"),P.Snapshot().Count,152);TestEqual(TEXT("province area"),P.Snapshot().Area,5957);
    TestEqual(TEXT("province raw kg"),P.Snapshot().Harvest,int64(6270550));TestEqual(TEXT("province raw cents"),P.Snapshot().Value,int64(11063285000LL));
    const int32 City=F.Map->Cities.IndexOfByPredicate([](const FDemoCity& C){return C.Code==350100;});
    if(!TestTrue(TEXT("Fuzhou exists"),City!=INDEX_NONE))return false;
    F.Map->SelectedCity=City;FPondOperationsTestAccess::Refresh(P,F.Map);
    TestEqual(TEXT("Fuzhou ponds"),P.Snapshot().Count,24);TestEqual(TEXT("Fuzhou area"),P.Snapshot().Area,1026);
    TestEqual(TEXT("Fuzhou raw kg"),P.Snapshot().Harvest,int64(1089800));TestEqual(TEXT("Fuzhou raw cents"),P.Snapshot().Value,int64(1980820000));
    const int32 Site=F.Map->Cities[City].Sites.IndexOfByPredicate([](const FDemoSite& S){return S.Id==TEXT("350100-08");});
    if(!TestTrue(TEXT("target pond exists"),Site!=INDEX_NONE))return false;
    F.Map->SelectedSite=Site;FPondOperationsTestAccess::Refresh(P,F.Map);
    TestEqual(TEXT("pond area"),P.Snapshot().Area,50);TestEqual(TEXT("year-end stock"),P.Snapshot().Stock,int64(128000));
    TestEqual(TEXT("pond raw kg"),P.Snapshot().Harvest,int64(55000));TestEqual(TEXT("pond raw cents"),P.Snapshot().Value,int64(154000000));
    int64 DisplayHundredths=0;for(const auto& M:P.Snapshot().Months)DisplayHundredths+=(M.Harvest+5)/10;
    // 月份独立四舍五入后55.02吨，年合计必须继续用原始55000kg，不能累加显示值。-codex，2026-10-21
    TestEqual(TEXT("rounded monthly display differs from raw annual total"),DisplayHundredths,int64(5502));
    TestTrue(TEXT("reload succeeds for unchanged selection"),P.Load(F.Map));TestEqual(TEXT("reload invalidates revision"),P.Snapshot().Revision,uint32(0));
    FPondOperationsTestAccess::Refresh(P,F.Map);TestEqual(TEXT("same selected pond rebuilt"),P.Snapshot().Harvest,int64(55000));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPondOperationsFailureTest,"Fujian.Operations.InvalidLedgerVisibleFailure",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPondOperationsFailureTest::RunTest(const FString&)
{
    FOperationsFixture F;if(!TestTrue(TEXT("real ledger fixture available"),F.Initialize()))return false;
    FPondPresentation P(nullptr);
    AddExpectedErrorPlain(TEXT("经营资料不可用："),EAutomationExpectedErrorFlags::Contains,7);
    auto Reject=[this,&P,&F](const FString& Name,const FString& Json)
    {
        if(!TestTrue(TEXT("prime previous successful data"),P.Load(F.Map)))return;
        FPondOperationsTestAccess::Refresh(P,F.Map);
        TestFalse(*Name,FPondOperationsTestAccess::Load(P,F.Map,Json));
        TestEqual(TEXT("failed reload invalidates revision"),P.Snapshot().Revision,uint32(0));
        TestFalse(TEXT("failed load immediately exposes error"),P.Snapshot().Error.IsEmpty());
        FPondOperationsTestAccess::Refresh(P,F.Map);
        TestFalse(TEXT("selection snapshot preserves visible error"),P.Snapshot().Error.IsEmpty());
        TestTrue(TEXT("no stale annual values or ranking retained"),P.Snapshot().Harvest==0&&P.Snapshot().Value==0&&P.Snapshot().Ranking.IsEmpty());
    };
    Reject(TEXT("missing file payload rejected"),TEXT(""));
    Reject(TEXT("malformed JSON rejected"),TEXT("{broken"));
    Reject(TEXT("missing month rejected"),F.Changed([](auto R){auto A=R->GetArrayField(TEXT("sites"))[0]->AsObject();auto M=A->GetArrayField(TEXT("monthly"));M.Pop();A->SetArrayField(TEXT("monthly"),M);}));
    Reject(TEXT("duplicate ID rejected"),F.Changed([](auto R){const auto& A=R->GetArrayField(TEXT("sites"));A[1]->AsObject()->SetStringField(TEXT("id"),A[0]->AsObject()->GetStringField(TEXT("id")));}));
    Reject(TEXT("inconsistent output value rejected"),F.Changed([](auto R){auto M=R->GetArrayField(TEXT("sites"))[0]->AsObject()->GetArrayField(TEXT("monthly"))[0]->AsObject();M->SetNumberField(TEXT("outputValueCents"),1);}));
    Reject(TEXT("city label mismatch rejected"),F.Changed([](auto R){R->GetArrayField(TEXT("sites"))[0]->AsObject()->SetStringField(TEXT("cityName"),TEXT("错误城市"));}));
    TestFalse(TEXT("null map rejected"),P.Load(nullptr));TestFalse(TEXT("null map exposes error"),P.Snapshot().Error.IsEmpty());
    return true;
}
#endif
