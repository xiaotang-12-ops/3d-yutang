// 操作屏唯一调查导航源，城市镜头复用Controller，快照发送与回执不拥有第二份选择。-codex，2026-10-28
#pragma once
#include "XinluoScreenLink.h"
class AFujianController;
class FXinluoSurveyControl
{
public:
    explicit FXinluoSurveyControl(AFujianController* InOwner):Owner(InOwner){}
    bool Initialize();void Tick();void CityChanged(int32 Code);void SelectSubject(const FString& Id);void ChangeGroup(int32 Delta);void Overview();void Back();void Home();
    const Xinluo::FNavigation& Navigation()const{return State.Nav;}
    const Xinluo::FState& Snapshot()const{return State;}
    const Xinluo::FDataset& Dataset()const{return Data;}
    FString Status()const;
private:
    AFujianController* Owner;Xinluo::FDataset Data;Xinluo::FState State;Xinluo::FTransport Transport;
    bool Restoring=false,Dirty=true,Ready=false,AckPulse=false,WindowPlaced=false;double NextWrite=0,NextRead=0,LastAck=0,WindowPlacementAt=0;
    Xinluo::FAck Ack;FString InitError,PublishError,ReceiveError,LastStatus;
    void ReadAck(double Now);
    void Changed(const Xinluo::FNavigation& Before);void RestoreCamera();
};
