// A/B共用调查契约与导航归约；稳定ID独立于旧模拟塘数组，面积精确到十分亩。
// 版本由规范化实际记录摘要生成；拒绝缺字段、错精度及漂移，不回退模拟经营数据。-codex，2026-10-28
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"

namespace Xinluo
{
constexpr int32 CityCode=350800,DistrictCode=350802,PageSize=6;
inline FString Digest(const FString& Value)
{
    FTCHARToUTF8 Bytes(*Value);uint8 Hash[20];FSHA1::HashBuffer(Bytes.Get(),Bytes.Length(),Hash);
    return BytesToHex(Hash,20).ToLower();
}
inline bool Integer(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,int64& Out,int64 Min=0,int64 Max=1000000000000LL)
{
    const auto* V=O.IsValid()?O->Values.Find(Key):nullptr;
    if(!V||!V->IsValid()||(*V)->Type!=EJson::Number)return false;
    const double N=(*V)->AsNumber();
    if(!FMath::IsFinite(N)||N<double(Min)||N>double(Max)||N!=FMath::FloorToDouble(N))return false;
    Out=int64(N);return true;
}
inline bool String(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FString& Out,bool AllowEmpty=false)
{
    const auto* V=O.IsValid()?O->Values.Find(Key):nullptr;
    if(!V||!V->IsValid()||(*V)->Type!=EJson::String)return false;
    Out=(*V)->AsString();return Out.Len()<=512&&(AllowEmpty||!Out.TrimStartAndEnd().IsEmpty());
}
inline bool Parse(const FString& Text,TSharedPtr<FJsonObject>& Out)
{return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)&&Out.IsValid();}
inline FString Json(const TSharedRef<FJsonObject>& O)
{FString Text;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Text));return Text;}
inline FString Area(int64 Tenths)
{
    FString Whole=FString::Printf(TEXT("%lld"),Tenths/10);
    for(int32 I=Whole.Len()-3;I>0;I-=3)Whole.InsertAt(I,TEXT(','));
    return Tenths%10?Whole+FString::Printf(TEXT(".%lld"),Tenths%10):Whole;
}
inline FString DataPath()
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("SurveyData="),Path))Path=FPaths::ProjectDir()/TEXT("../../shared/xinluo-survey.json");
    return FPaths::ConvertRelativePathToFull(Path);
}
inline bool Linked(){return FParse::Param(FCommandLine::Get(),TEXT("XinluoLinked"));}
struct FSubject
{
    FString Id,Name,Address,Species,Mode;int32 Ordinal=0;int64 AreaMuTenths=0;
    bool HasCoordinate=false;double Longitude=0,Latitude=0;
};
struct FDataset
{
    FString Version,Error;TArray<FSubject> Subjects;
    int32 Find(const FString& Id) const {return Subjects.IndexOfByPredicate([&](const FSubject& S){return S.Id==Id;});}
    int32 Pages() const {return (Subjects.Num()+PageSize-1)/PageSize;}
    FString Canonical() const
    {
        FString Text;
        for(const auto& S:Subjects)
        {
            if(!Text.IsEmpty())Text+=TEXT("\n");
            const TArray<FString> Fields={S.Id,FString::FromInt(S.Ordinal),FString::FromInt(CityCode),FString::FromInt(DistrictCode),S.Name,S.Address,S.Species,S.Mode,FString::Printf(TEXT("%lld"),S.AreaMuTenths),S.HasCoordinate?FString::Printf(TEXT("%.8f"),S.Longitude):TEXT("null"),S.HasCoordinate?FString::Printf(TEXT("%.8f"),S.Latitude):TEXT("null")};
            for(const FString& F:Fields){FTCHARToUTF8 Bytes(*F);Text+=FString::FromInt(Bytes.Length())+TEXT(":")+F;}
        }
        return Text;
    }
    bool Load(const FString& Path)
    {
        Subjects.Reset();Version.Empty();Error.Empty();FString Text;TSharedPtr<FJsonObject> Root;
        auto Fail=[&](const TCHAR* Message){Subjects.Reset();Version.Empty();Error=Message;return false;};
        if(!FFileHelper::LoadFileToString(Text,*Path)||Text.Len()>1024*1024||!Parse(Text,Root))return Fail(TEXT("调查数据不可读取"));
        int64 Schema=0;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(!Integer(Root,TEXT("schemaVersion"),Schema,1,1)||!String(Root,TEXT("datasetVersion"),Version)||!Root->TryGetArrayField(TEXT("subjects"),Rows)||Rows->Num()!=26)return Fail(TEXT("调查数据版本或26主体范围不符"));
        TSet<FString> Ids;
        for(const auto& V:*Rows)
        {
            if(!V.IsValid()||V->Type!=EJson::Object)return Fail(TEXT("主体记录格式无效"));
            auto O=V->AsObject();FSubject S;int64 Ordinal=0,City=0,District=0;
            if(!Integer(O,TEXT("ordinal"),Ordinal,1,26)||Ordinal!=Subjects.Num()+1||!Integer(O,TEXT("cityCode"),City,CityCode,CityCode)||!Integer(O,TEXT("districtCode"),District,DistrictCode,DistrictCode)||!Integer(O,TEXT("areaMuTenths"),S.AreaMuTenths)||!String(O,TEXT("id"),S.Id)||!String(O,TEXT("name"),S.Name)||!String(O,TEXT("address"),S.Address)||!String(O,TEXT("species"),S.Species)||!String(O,TEXT("mode"),S.Mode))return Fail(TEXT("主体字段缺失或无效"));
            S.Ordinal=int32(Ordinal);
            if(S.Id!=FString::Printf(TEXT("xinluo-%03d"),S.Ordinal)||Ids.Contains(S.Id))return Fail(TEXT("主体稳定ID重复或错序"));
            const TArray<FString> Species={TEXT("鳗鲡"),TEXT("草鱼"),TEXT("鲢鱼"),TEXT("淡水鲈鱼"),TEXT("乌鳢"),TEXT("鲟鱼"),TEXT("其他")};
            if(!Species.Contains(S.Species)||(S.Mode!=TEXT("池塘养殖")&&S.Mode!=TEXT("其他")))return Fail(TEXT("调查分类未在契约中定义"));
            const auto* Lon=O->Values.Find(TEXT("longitude"));const auto* Lat=O->Values.Find(TEXT("latitude"));
            if(!Lon||!Lat||!Lon->IsValid()||!Lat->IsValid())return Fail(TEXT("坐标缺失须显式为null"));
            if((*Lon)->Type==EJson::Null&&(*Lat)->Type==EJson::Null)S.HasCoordinate=false;
            else
            {
                if((*Lon)->Type!=EJson::Number||(*Lat)->Type!=EJson::Number)return Fail(TEXT("经纬度必须成对"));
                S.Longitude=(*Lon)->AsNumber();S.Latitude=(*Lat)->AsNumber();S.HasCoordinate=true;
                if(!FMath::IsFinite(S.Longitude)||!FMath::IsFinite(S.Latitude)||FMath::Abs(S.Longitude)>180||FMath::Abs(S.Latitude)>90)return Fail(TEXT("坐标范围无效"));
            }
            Ids.Add(S.Id);Subjects.Add(MoveTemp(S));
        }
        if(Version!=Digest(Canonical()))return Fail(TEXT("调查数据摘要不匹配"));
        return true;
    }
};
struct FNavigation
{
    int32 City=0,Group=0;FString Subject,View=TEXT("province"),ReturnView=TEXT("city");
    bool operator==(const FNavigation& R)const{return City==R.City&&Group==R.Group&&Subject==R.Subject&&View==R.View&&ReturnView==R.ReturnView;}
    bool operator!=(const FNavigation& R)const{return !(*this==R);}
    void SelectCity(int32 Code){City=Code;Group=0;Subject.Empty();View=Code==0?TEXT("province"):Code==CityCode?TEXT("city"):TEXT("empty");ReturnView=TEXT("city");}
    void SelectSubject(const FDataset& D,const FString& Id)
    {const int32 I=D.Find(Id);if(City!=CityCode||I<0)return;Subject=Id;Group=I/PageSize;if(View==TEXT("overview"))ReturnView=TEXT("subject");else View=TEXT("subject");}
    void ChangeGroup(const FDataset& D,int32 Delta)
    {if(City!=CityCode)return;const int32 Next=FMath::Clamp(Group+Delta,0,D.Pages()-1);if(Next==Group)return;Group=Next;Subject.Empty();ReturnView=TEXT("city");if(View!=TEXT("overview"))View=TEXT("city");}
    void Overview(){if(City==CityCode&&View!=TEXT("overview")){ReturnView=View;View=TEXT("overview");}}
    void Back(){if(View==TEXT("overview")){View=ReturnView;ReturnView=TEXT("city");}else if(View==TEXT("subject"))View=TEXT("city");else SelectCity(0);}
    bool Validate(const FDataset& D)const
    {
        const bool Known=City==0||(City>=350100&&City<=350900&&City%100==0);
        if(!Known||Group<0||Group>=D.Pages()||(ReturnView!=TEXT("city")&&ReturnView!=TEXT("subject")))return false;
        if(City!=CityCode)return Group==0&&Subject.IsEmpty()&&ReturnView==TEXT("city")&&View==(City==0?TEXT("province"):TEXT("empty"));
        if(View!=TEXT("city")&&View!=TEXT("subject")&&View!=TEXT("overview"))return false;
        const int32 I=D.Find(Subject);
        if(!Subject.IsEmpty()&&(I<0||I/PageSize!=Group))return false;
        if((View==TEXT("subject")||(View==TEXT("overview")&&ReturnView==TEXT("subject")))&&I<0)return false;
        return View==TEXT("overview")||ReturnView==TEXT("city");
    }
    FString Fingerprint()const{return FString::Printf(TEXT("%d|%d|%s|%s|%s"),City,Group,*Subject,*View,*ReturnView);}
};
struct FState
{
    FString Session,Dataset;int64 Revision=0,Heartbeat=0;FNavigation Nav;
    FString Serialize()const
    {
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("schemaVersion"),1);O->SetStringField(TEXT("datasetVersion"),Dataset);O->SetStringField(TEXT("sessionId"),Session);O->SetNumberField(TEXT("revision"),Revision);O->SetNumberField(TEXT("heartbeat"),Heartbeat);
        O->SetNumberField(TEXT("cityCode"),Nav.City);O->SetNumberField(TEXT("districtCode"),Nav.City==CityCode?DistrictCode:0);O->SetStringField(TEXT("siteId"),Nav.Subject);O->SetNumberField(TEXT("groupIndex"),Nav.Group);O->SetStringField(TEXT("view"),Nav.View);O->SetStringField(TEXT("returnView"),Nav.ReturnView);return Json(O);
    }
    static bool Read(const FString& Text,const FDataset& D,FState& Out,FString& Error)
    {
        TSharedPtr<FJsonObject> O;FState S;int64 Schema=0,City=0,District=0,Group=0;
        if(Text.Len()>65536||!Parse(Text,O)||!Integer(O,TEXT("schemaVersion"),Schema,1,1)||!String(O,TEXT("datasetVersion"),S.Dataset)||!String(O,TEXT("sessionId"),S.Session)||!Integer(O,TEXT("revision"),S.Revision,1)||!Integer(O,TEXT("heartbeat"),S.Heartbeat,1)||!Integer(O,TEXT("cityCode"),City,0,350900)||!Integer(O,TEXT("districtCode"),District,0,DistrictCode)||!Integer(O,TEXT("groupIndex"),Group,0,10000)||!String(O,TEXT("siteId"),S.Nav.Subject,true)||!String(O,TEXT("view"),S.Nav.View)||!String(O,TEXT("returnView"),S.Nav.ReturnView)){Error=TEXT("同步状态格式无效");return false;}
        if(S.Dataset!=D.Version){Error=TEXT("数据版本不兼容");return false;}
        FGuid Guid;if(!FGuid::Parse(S.Session,Guid)||!Guid.IsValid()){Error=TEXT("同步会话无效");return false;}
        S.Nav.City=int32(City);S.Nav.Group=int32(Group);
        if(District!=(City==CityCode?DistrictCode:0)||!S.Nav.Validate(D)){Error=TEXT("主体、城市、页组或返回状态不一致");return false;}
        Out=MoveTemp(S);Error.Empty();return true;
    }
};
}
