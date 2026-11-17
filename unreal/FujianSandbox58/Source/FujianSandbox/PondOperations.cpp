// 严格验证同ID、12个月、整数单位和量价关系；坏资料不补假零或沿用上一对象。
// 全账只在加载/选择时处理；绘制阶段不解析或排序JSON。-codex，2026-10-21
#include "PondPresentation.h"
#include "FujianMap.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    constexpr int64 SafeInteger=9007199254740991LL;
    bool Integer(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,int64& Out)
    {
        double N=0;if(!O.IsValid()||!O->TryGetNumberField(Key,N)||!FMath::IsFinite(N)||N<0||N>SafeInteger||FMath::FloorToDouble(N)!=N)return false;
        Out=static_cast<int64>(N);return true;
    }
    bool Add(int64& To,int64 Value){if(Value>SafeInteger-To)return false;To+=Value;return true;}
    void SortMetrics(TArray<FPondMetric>& Rows){Rows.Sort([](const auto& A,const auto& B){return A.Value==B.Value?A.Id<B.Id:A.Value>B.Value;});}
}
bool FPondPresentation::Load(AFujianMap* Map)
{
    FString Json;
    // 文件失败也走同一个清空与错误路径，F5后下一帧必须重建当前选择快照。-codex，2026-10-21
    if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Data/pond-operations.json"))))Json.Empty();
    return LoadJson(Map,Json);
}
bool FPondPresentation::LoadJson(AFujianMap* Map,const FString& Json)
{
    Records.Empty();DataError.Empty();DataVersion.Empty();Current=FPondSnapshot();
    auto Fail=[this](const FString& Reason){Records.Empty();DataVersion.Empty();DataError=TEXT("经营资料不可用：")+Reason;Current.Error=DataError;UE_LOG(LogTemp,Error,TEXT("%s"),*DataError);return false;};
    TSharedPtr<FJsonObject> Root;
    if(!Map||!Map->Error.IsEmpty()||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid())return Fail(TEXT("无法读取本地演示台账或地图未就绪"));
    int64 Schema=0,Year=0;bool Demo=false;
    if(!Integer(Root,TEXT("schemaVersion"),Schema)||Schema!=1||!Integer(Root,TEXT("year"),Year)||Year!=2025||!Root->TryGetBoolField(TEXT("demo"),Demo)||!Demo||!Root->TryGetStringField(TEXT("dataVersion"),DataVersion)||DataVersion.IsEmpty())return Fail(TEXT("年度或演示契约不符"));
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("sites"),Rows)||Rows->Num()!=Map->Total)return Fail(TEXT("鱼塘数量不符"));
    TMap<FString,const FDemoSite*> Source;TMap<FString,int32> CityCodes;TMap<int32,FString> CityNames;
    for(const auto& C:Map->Cities){CityNames.Add(C.Code,C.Name);for(const auto& S:C.Sites){Source.Add(S.Id,&S);CityCodes.Add(S.Id,C.Code);}}
    if(Source.Num()!=Map->Total||Source.IsEmpty())return Fail(TEXT("地图鱼塘编号不完整"));
    int64 AllHarvest=0,AllValue=0,AllStock=0;
    for(const auto& V:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;
        if(!V.IsValid()||!V->TryGetObject(O)||!O||!O->IsValid())return Fail(TEXT("鱼塘记录不是对象"));
        FPondRecord R;int64 Code=0,Area=0;
        if(!(*O)->TryGetStringField(TEXT("id"),R.Id)||!Source.Contains(R.Id)||Records.Contains(R.Id))return Fail(TEXT("未知或重复鱼塘编号"));
        const auto* S=Source[R.Id];FString StockDate,CityName;bool RowDemo=false;
        if(!(*O)->TryGetStringField(TEXT("name"),R.Name)||R.Name!=S->Name||!(*O)->TryGetStringField(TEXT("kind"),R.Kind)||R.Kind!=S->Kind||!(*O)->TryGetStringField(TEXT("species"),R.Species)||R.Species.IsEmpty()||!(*O)->TryGetBoolField(TEXT("demo"),RowDemo)||!RowDemo||!(*O)->TryGetStringField(TEXT("stockAsOf"),StockDate)||StockDate!=TEXT("2025-12-31")||!Integer(*O,TEXT("cityCode"),Code)||Code!=CityCodes[R.Id]||!(*O)->TryGetStringField(TEXT("cityName"),CityName)||CityName!=CityNames[static_cast<int32>(Code)]||!Integer(*O,TEXT("areaMu"),Area)||Area!=S->AreaMu||!Integer(*O,TEXT("stockCount"),R.Stock))return Fail(R.Id+TEXT("基础资料与地图不一致"));
        R.Area=static_cast<int32>(Area);R.CityCode=static_cast<int32>(Code);
        const TArray<TSharedPtr<FJsonValue>>* Months=nullptr;
        if(!(*O)->TryGetArrayField(TEXT("monthly"),Months)||Months->Num()!=12)return Fail(R.Id+TEXT("缺少完整12个月"));
        for(int32 I=0;I<12;I++)
        {
            const TSharedPtr<FJsonObject>* M=nullptr;FString Month;FPondMonth D;int64 Price=0;
            if(!(*Months)[I].IsValid()||!(*Months)[I]->TryGetObject(M)||!M||!M->IsValid()||!(*M)->TryGetStringField(TEXT("month"),Month)||Month!=FString::Printf(TEXT("2025-%02d"),I+1)||!Integer(*M,TEXT("harvestKg"),D.Harvest)||!Integer(*M,TEXT("unitValueCentsPerKg"),Price)||!Integer(*M,TEXT("outputValueCents"),D.Value)||(Price>0&&D.Harvest>SafeInteger/Price)||D.Harvest*Price!=D.Value||!Add(R.Harvest,D.Harvest)||!Add(R.Value,D.Value))return Fail(R.Id+TEXT("月份、数值或量价关系错误"));
            R.Months.Add(D);
        }
        int64 AnnualH=0,AnnualV=0;
        if(!Integer(*O,TEXT("annualHarvestKg"),AnnualH)||AnnualH!=R.Harvest||!Integer(*O,TEXT("annualOutputValueCents"),AnnualV)||AnnualV!=R.Value||!Add(AllHarvest,R.Harvest)||!Add(AllValue,R.Value)||!Add(AllStock,R.Stock))return Fail(R.Id+TEXT("年度合计错误或超界"));
        Records.Add(R.Id,MoveTemp(R));
    }
    UE_LOG(LogTemp,Display,TEXT("PondOperations loaded=%d harvest=%lldkg value=%lldcents version=%s"),Records.Num(),AllHarvest,AllValue,*DataVersion);
    return true;
}
void FPondPresentation::Refresh(AFujianMap* Map)
{
    if(!Map){Current=FPondSnapshot();Current.Error=TEXT("经营资料不可用：地图未就绪");return;}
    FPondSnapshot Next;Next.City=Map->SelectedCity;Next.Site=Map->SelectedSite;Next.Version=DataVersion;Next.Revision=Current.Revision+1;Next.Title=TEXT("福建省");Next.Id=TEXT("FUJIAN");Next.Error=DataError;Next.Months.SetNum(12);
    TMap<FString,int64> Groups;TMap<int32,int64> RegionValues;
    for(int32 CI=0;CI<Map->Cities.Num();CI++)
    {
        const auto& C=Map->Cities[CI];if(Next.City!=INDEX_NONE&&Next.City!=CI)continue;
        if(Next.City==CI){Next.Title=C.Name;Next.Id=FString::FromInt(C.Code);}
        for(int32 SI=0;SI<C.Sites.Num();SI++)
        {
            const auto& S=C.Sites[SI];if(Next.Site!=INDEX_NONE&&Next.Site!=SI)continue;
            Next.Count++;Next.Area+=S.AreaMu;Next.Dots.Add({FVector2D(S.Position.X,S.Position.Y),S.Name});
            if(Next.Site==SI){Next.Title=S.Name;Next.Id=S.Id;Next.Kind=S.Kind;}
            const auto* R=Records.Find(S.Id);if(!R)continue;
            Next.Harvest+=R->Harvest;Next.Value+=R->Value;Next.Stock+=R->Stock;Next.Species=R->Species;
            for(int32 M=0;M<12;M++){Next.Months[M].Harvest+=R->Months[M].Harvest;Next.Months[M].Value+=R->Months[M].Value;}
            Groups.FindOrAdd(R->Species)+=R->Harvest;RegionValues.FindOrAdd(CI)+=R->Value;
            Next.Details.Add({S.Name,S.Id,R->Value});
        }
    }
    if(Next.City==INDEX_NONE)for(const auto& Item:RegionValues)Next.Ranking.Add({Map->Cities[Item.Key].Name,FString::FromInt(Map->Cities[Item.Key].Code),Item.Value});
    else Next.Ranking=Next.Details;
    for(const auto& G:Groups)Next.Varieties.Add({G.Key,G.Key,G.Value});
    SortMetrics(Next.Ranking);SortMetrics(Next.Details);SortMetrics(Next.Varieties);
    Current=MoveTemp(Next);Transition=0.f;
    UE_LOG(LogTemp,Display,TEXT("PondSnapshot rev=%u id=%s count=%d area=%d harvest=%lld value=%lld error=%s"),Current.Revision,*Current.Id,Current.Count,Current.Area,Current.Harvest,Current.Value,*Current.Error);
}
