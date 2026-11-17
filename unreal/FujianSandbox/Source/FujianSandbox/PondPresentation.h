// 本地模拟经营数据、同选择快照及第二窗口生命周期；二维界面只读快照，不另维护选塘状态。
// 金额以int64分保存，先汇总原值再换算，真实资料需按同ID及口径适配。-codex，2026-10-21
#pragma once
#include "CoreMinimal.h"
class AFujianController;
class AFujianMap;
class SWindow;
class SPondDashboard;

struct FPondMonth { int64 Harvest=0,Value=0; };
struct FPondRecord
{
    FString Id,Name,Kind,Species;
    int32 CityCode=0,Area=0;
    int64 Stock=0,Harvest=0,Value=0;
    TArray<FPondMonth> Months;
};
struct FPondMetric {FString Name,Id;int64 Value=0;};
struct FPondDot {FVector2D Position;FString Name;};
struct FPondSnapshot
{
    FString Title,Id,Species,Kind,Error,Version;
    int32 City=INDEX_NONE,Site=INDEX_NONE,Count=0,Area=0;
    int64 Harvest=0,Value=0,Stock=0;
    TArray<FPondMonth> Months;
    TArray<FPondMetric> Ranking,Varieties,Details;
    TArray<FPondDot> Dots;
    uint32 Revision=0;
};

class FPondPresentation : public TSharedFromThis<FPondPresentation>
{
public:
    explicit FPondPresentation(AFujianController* InOwner);
    bool Load(AFujianMap* Map);
    void Tick(float DeltaSeconds);
    void Show();
    void Shutdown();
    const FPondSnapshot& Snapshot() const {return Current;}
    float Reveal() const {return FMath::Clamp(Transition/.45f,0.f,1.f);}
    FString WindowStatus() const {return bOpened?(Window.IsValid()?TEXT("数据屏已连接"):TEXT("数据屏已关闭 · F2重开")):TEXT("数据屏准备中");}
private:
    bool LoadJson(AFujianMap* Map,const FString& Json);
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FPondOperationsTestAccess;
#endif
    void Refresh(AFujianMap* Map);
    TWeakObjectPtr<AFujianController> Owner;
    TMap<FString,FPondRecord> Records;
    FString DataError,DataVersion;
    FPondSnapshot Current;
    TSharedPtr<SWindow> Window;
    TSharedPtr<SPondDashboard> Dashboard;
    float Transition=1.f;
    bool bOpened=false,bShuttingDown=false;
};
