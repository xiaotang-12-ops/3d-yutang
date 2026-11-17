// 恢复先取OS锁，再校验上一目标并经城市入口恢复镜头；写失败持续重试最新内存目标。
// accepted回执只表示B已接收目标，visible修订才表示B完成画面交接，真实同步仍需原生帧验收。-codex，2026-10-28
#include "XinluoSurveyControl.h"
#include "FujianMap.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericApplication.h"
bool FXinluoSurveyControl::Initialize()
{
    if(!Transport.Open(TEXT("controller"))){InitError=Transport.Error;UE_LOG(LogTemp,Error,TEXT("XinluoHealth %s"),*InitError);return false;}
    if(!Data.Load(Xinluo::DataPath())){InitError=Data.Error;UE_LOG(LogTemp,Error,TEXT("XinluoHealth %s"),*InitError);return false;}
    // 地形局部修整必须先于RestoreCamera和HUD首次高程缓存；失败保留原地图，资料和B同步继续。-codex，2026-10-30
    FString FarmError;
    if(!Owner||!Owner->Map||!Owner->Map->BuildSurveyFarms(Data,FarmError))
        UE_LOG(LogTemp,Warning,TEXT("SurveyFarms unavailable; navigation retained: %s"),*FarmError);
    FString Text,ReadError;Xinluo::FState Old;
    if(Transport.Read(TEXT("state.json"),Text)&&Xinluo::FState::Read(Text,Data,Old,ReadError))State.Nav=Old.Nav;
    // 总览版不恢复旧单场详情/分组，避免重启复活已取消的选择。-codex，2026-11-03
    State.Nav.SelectCity(State.Nav.City);
    State.Dataset=Data.Version;State.Session=FGuid::NewGuid().ToString();State.Revision=1;State.Heartbeat=0;
    // 仅恢复城市分布和全体数据，不进入单场。-codex，2026-11-03
    Ready=true;WindowPlacementAt=FPlatformTime::Seconds()+2.;RestoreCamera();
    Tick();return true;
}
void FXinluoSurveyControl::RestoreCamera()
{
    if(!Owner||!Owner->Map)return;
    const int32 Index=Owner->Map->Cities.IndexOfByPredicate([&](const FDemoCity& C){return C.Code==State.Nav.City;});
    if(State.Nav.City!=0&&Index<0)State.Nav.SelectCity(0);
    // 切省/切城前撤销调查轨迹，避免旧Motion在同帧覆盖新城市飞行的首帧。-codex，2026-10-30
    Owner->CancelCamera(TEXT("survey-navigation"));
    Restoring=true;Owner->SelectProvinceCity(Index);Restoring=false;
}
void FXinluoSurveyControl::Changed(const Xinluo::FNavigation& Before)
{
    if(!Ready||Before==State.Nav)return;
    ++State.Revision;Dirty=true;NextWrite=0;
    if(Before.City!=State.Nav.City)RestoreCamera();
    else if(Before.View==TEXT("subject")&&State.Nav.View==TEXT("city")&&Owner)Owner->ReframeSurveyCity();
    UE_LOG(LogTemp,Display,TEXT("XinluoTarget session=%s revision=%lld city=%d group=%d id=%s view=%s"),*State.Session,State.Revision,State.Nav.City,State.Nav.Group,*State.Nav.Subject,*State.Nav.View);
    Tick();
}
void FXinluoSurveyControl::CityChanged(int32 Code)
{if(Restoring||!Ready||Code==State.Nav.City)return;const auto Before=State.Nav;State.Nav.SelectCity(Code);Changed(Before);}
// 点选恢复为唯一主体导航入口，列表和地图复用同一状态，重启仍回城市分布。-codex，2026-11-03
void FXinluoSurveyControl::SelectSubject(const FString& Id){if(State.Nav.City!=Xinluo::CityCode||Data.Find(Id)<0)return;const auto Before=State.Nav;State.Nav.View=TEXT("city");State.Nav.ReturnView=TEXT("city");State.Nav.SelectSubject(Data,Id);Changed(Before);}
void FXinluoSurveyControl::ChangeGroup(int32 Delta){const auto Before=State.Nav;State.Nav.ChangeGroup(Data,Delta);if(Before.Group!=State.Nav.Group){State.Nav.View=TEXT("city");State.Nav.ReturnView=TEXT("city");}Changed(Before);}
void FXinluoSurveyControl::Overview(){const auto Before=State.Nav;State.Nav.Overview();Changed(Before);}
void FXinluoSurveyControl::Back(){const auto Before=State.Nav;State.Nav.Back();Changed(Before);}
void FXinluoSurveyControl::Home(){const auto Before=State.Nav;State.Nav.SelectCity(0);Changed(Before);}
void FXinluoSurveyControl::Tick()
{
    if(!Ready)return;const double Now=FPlatformTime::Seconds();
    // UE5.8保存的DisplayIndex可覆盖启动位置；只纠正本次联动窗口，不写配置或反复夺回位置。
    if(!WindowPlaced&&Now>=WindowPlacementAt&&GEngine&&GEngine->GameViewport)
    {
        const auto Window=GEngine->GameViewport->GetWindow();
        if(Window.IsValid())
        {
            FDisplayMetrics Metrics;FDisplayMetrics::RebuildDisplayMetrics(Metrics);
            for(const auto& Monitor:Metrics.MonitorInfo)if(Monitor.bIsPrimary)
            {
                const auto& R=Monitor.WorkArea;
                const FVector2D Position(R.Left+(R.Right-R.Left>2000?40:0),R.Top+(R.Bottom-R.Top>1160?40:0));
                Window->MoveWindowTo(Position);WindowPlaced=true;
                UE_LOG(LogTemp,Display,TEXT("XinluoControlWindow primary position=%.0f,%.0f client=%.0fx%.0f"),Position.X,Position.Y,Window->GetClientSizeInScreen().X,Window->GetClientSizeInScreen().Y);
                break;
            }
        }
    }
    if(Now>=NextWrite)
    {
        ++State.Heartbeat;
        if(Transport.Write(TEXT("state.json"),State.Serialize())){Dirty=false;PublishError.Empty();NextWrite=Now+1.;}
        else {Dirty=true;PublishError=Transport.Error;NextWrite=Now+.25;}
    }
    ReadAck(Now);
    const FString CurrentStatus=Status();
    if(CurrentStatus!=LastStatus){LastStatus=CurrentStatus;UE_LOG(LogTemp,Display,TEXT("XinluoHealth session=%s revision=%lld status=%s"),*State.Session,State.Revision,*CurrentStatus);}
}
void FXinluoSurveyControl::ReadAck(double Now)
{
    if(Now<NextRead)return;NextRead=Now+.1;
    FString Text;Xinluo::FAck Incoming;
    if(!Transport.Read(TEXT("ack.json"),Text))return;
    if(!Xinluo::FAck::Read(Text,Incoming)){ReceiveError=TEXT("展示屏回执格式无效");return;}
    if(Incoming.Dataset!=State.Dataset){ReceiveError=TEXT("展示屏数据版本不兼容");return;}
    if(Incoming.Session!=State.Session||Incoming.Accepted>State.Revision||Incoming.Visible>Incoming.Accepted)return;
    if(Ack.ReceiverSession!=Incoming.ReceiverSession){AckPulse=false;LastAck=Now;}
    else if(Incoming.Heartbeat>Ack.Heartbeat){AckPulse=true;LastAck=Now;}
    else if(Incoming.Heartbeat<Ack.Heartbeat)return;
    Ack=Incoming;ReceiveError.Empty();
}
FString FXinluoSurveyControl::Status()const
{
    if(!InitError.IsEmpty())return InitError;
    if(!PublishError.IsEmpty())return PublishError;
    if(!ReceiveError.IsEmpty())return ReceiveError;
    if(!Ready||!AckPulse)return TEXT("等待展示屏连接");
    if(FPlatformTime::Seconds()-LastAck>3)return TEXT("展示屏连接中断");
    // 回执在更新只证明B还活着；B接收不到A时不能把旧visibleRevision误报成正常同步。
    if(Ack.Health!=Xinluo::HealthyReceipt)return FString(TEXT("联动异常 · "))+Ack.Health;
    if(Dirty)return TEXT("发送中，等待重试");
    if(Ack.Accepted!=State.Revision)return TEXT("等待展示屏接受目标");
    return Ack.Visible==State.Revision?TEXT("展示屏已完成画面交接"):TEXT("展示屏已接受，画面切换中");
}
