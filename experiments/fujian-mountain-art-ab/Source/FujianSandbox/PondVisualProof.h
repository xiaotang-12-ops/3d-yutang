// 显式命令行启用的原生联动取证；程序化选择不代表真实指针/触摸验证。-codex，2026-10-22
#pragma once
#include "CoreMinimal.h"
class AFujianController;
class SWindow;
class FPondVisualProof
{
public:
    void Tick(float Delta,AFujianController* Owner,const TSharedPtr<SWindow>& DataWindow);
private:
    void Capture(const FString& Name,const TSharedPtr<SWindow>& DataWindow,bool bMotionFrame=false);
    void RecordMotion(const TSharedPtr<SWindow>& DataWindow);
    TWeakObjectPtr<AFujianController> ProofOwner;
    FString Directory;
    FString MotionRows;
    double Time=0;
    double MotionEpoch=0,NextMotionCapture=0;
    int32 Step=0,Fuzhou=INDEX_NONE;
    int32 MotionFrames=0,MotionFailures=0;
    bool Started=false;
    bool bRecordMotion=false,bMotionFinished=false;
};
