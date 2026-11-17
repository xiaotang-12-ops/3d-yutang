// 验证远景先亮、途中不淡、抵达后计时与取消；数学测试不冒充原生帧率。-codex，2026-11-02
#include "SurveyCameraFlight.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSurveyFlightStages,"Fujian.SurveyFlight.Stages",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSurveyFlightStages::RunTest(const FString&)
{
    const FPondCameraPose A{FVector(0,0,100),650,180,54},B{FVector(900,-400,120),650,180,54};
    const FVector Eye=FSurveyCameraFlight::Eye(A),Water=B.Focus+FVector(-1,0,0);
    FSurveyCameraFlight F;F.Start(A,B,Water,Eye);
    for(int32 I=0;I<78;++I){const auto P=F.Advance(1.f/60,false);TestTrue(TEXT("orient eye stays at far location"),FSurveyCameraFlight::Eye(P).Equals(Eye,.002));TestTrue(TEXT("light waits for submitted visibility"),F.BeaconAge()<0);}
    TestEqual(TEXT("no preliminary near framing"),F.GetStage(),FSurveyCameraFlight::EStage::AwaitVisible);
    for(int32 I=0;I<20;++I)F.Advance(1.f/60,false);
    TestTrue(TEXT("offscreen has no light"),F.BeaconAge()<0);
    F.Advance(1.f/60,true);TestEqual(TEXT("far visible starts beacon"),F.GetStage(),FSurveyCameraFlight::EStage::Beacon);
    for(int32 I=0;I<FMath::FloorToInt(FSurveyCameraFlight::BeaconDuration*60);++I){TestTrue(TEXT("far display holds before approach"),FSurveyCameraFlight::Eye(F.Advance(1.f/60,true)).Equals(Eye,.002));TestTrue(TEXT("no fading during far display"),F.ArrivalAge()<0);}
    for(int32 I=0;I<450;++I){F.Advance(1.f/60,true);if(F.IsActive())TestTrue(TEXT("no fade before complete"),F.ArrivalAge()<0);}
    const auto P=F.GetPose();TestFalse(TEXT("finite finish"),F.IsActive());TestTrue(TEXT("exact original final pose"),P.Focus==B.Focus&&P.Distance==B.Distance&&P.Yaw==B.Yaw&&P.Pitch==B.Pitch);
    TestTrue(TEXT("settled fade clock starts"),F.ArrivalAge()>=0);const float Age=F.ArrivalAge();F.Advance(.5f,true);TestTrue(TEXT("settled fade clock advances"),FMath::IsNearlyEqual(F.ArrivalAge(),Age+.5f));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSurveyFlightInterrupt,"Fujian.SurveyFlight.Interrupt",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSurveyFlightInterrupt::RunTest(const FString&)
{
    const FPondCameraPose A{FVector(0,0,100),650,179,34},B{FVector(30,100,120),650,-179,80};
    FSurveyCameraFlight F;F.Start(A,B,B.Focus,FSurveyCameraFlight::Eye(A)+FVector(0,0,200));
    for(int32 I=0;I<35;++I)F.Advance(1.f/60,false);
    const auto Mid=F.GetPose();F.Cancel();TestFalse(TEXT("cancel inactive"),F.IsActive());TestTrue(TEXT("cancel clears both effect clocks"),F.BeaconAge()<0&&F.ArrivalAge()<0);TestTrue(TEXT("cancel freezes"),F.Advance(2,true).Focus==Mid.Focus);
    F.Start(Mid,A,A.Focus,FSurveyCameraFlight::Eye(Mid));TestTrue(TEXT("retarget starts from actual pose"),F.GetPose().Focus==Mid.Focus);
    for(int32 I=0;I<200;++I)F.Advance(1.f/60,false);
    TestEqual(TEXT("visibility timeout is finite"),F.GetStage(),FSurveyCameraFlight::EStage::Failed);TestTrue(TEXT("timeout has no effect"),F.BeaconAge()<0&&F.ArrivalAge()<0);
    F.Start(A,B,B.Focus,FSurveyCameraFlight::Eye(A));
    for(int32 I=0;I<600;++I){const auto P=F.Advance(1.f/60,true);TestFalse(TEXT("angle and steep pitch stay finite"),P.Focus.ContainsNaN()||!FMath::IsFinite(P.Distance)||!FMath::IsFinite(P.Pitch)||!FMath::IsFinite(P.Yaw));}
    F.Cancel();TestTrue(TEXT("cancel after arrival clears fading effect"),F.BeaconAge()<0&&F.ArrivalAge()<0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSurveyFlightReverse,"Fujian.SurveyFlight.ReverseApproach",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSurveyFlightReverse::RunTest(const FString&)
{
    // 背向目标沿半径/方位弧飞近，不得直穿目标竖轴引起一帧翻转。-codex，2026-11-02
    const FPondCameraPose A{FVector(0,0,100),650,180,54},B{FVector(-1000,0,100),650,180,54};
    FSurveyCameraFlight F;F.Start(A,B,B.Focus,FSurveyCameraFlight::Eye(A));auto Previous=A;
    for(int32 I=0;I<600;++I)
    {
        const auto P=F.Advance(1.f/60,true);TestTrue(TEXT("no sudden horizontal flip"),FMath::Abs(FMath::FindDeltaAngleDegrees(Previous.Yaw,P.Yaw))<8.f);
        TestTrue(TEXT("eye above safe endpoints altitude"),FSurveyCameraFlight::Eye(P).Z>=FSurveyCameraFlight::Eye(A).Z-.01);Previous=P;
    }
    TestTrue(TEXT("original final view retained"),F.GetPose().Focus==B.Focus&&F.GetPose().Yaw==B.Yaw&&F.GetPose().Pitch==B.Pitch&&F.GetPose().Distance==B.Distance);
    const auto Frozen=F.GetPose();F.Reject(Frozen);TestEqual(TEXT("preflight rejection visible failure state"),F.GetStage(),FSurveyCameraFlight::EStage::Failed);
    TestTrue(TEXT("rejection freezes without clocks"),F.Advance(2,true).Focus==Frozen.Focus&&F.BeaconAge()<0&&F.ArrivalAge()<0&&!F.IsActive());
    F.Cancel();TestEqual(TEXT("failure clears on next input"),F.GetStage(),FSurveyCameraFlight::EStage::Idle);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSurveyFlightReadable,"Fujian.SurveyFlight.Readable",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSurveyFlightReadable::RunTest(const FString&)
{
    const FPondCameraPose A{FVector(0,0,100),5000,180,54},B{FVector(900,-400,165),650,180,54};const FVector FarEye=FSurveyCameraFlight::Eye(FPondCameraPose{B.Focus,1550,180,54});
    FSurveyCameraFlight F;F.Start(A,B,B.Focus,FarEye);bool SawApproach=false,SawComplete=false;float PreviousRange=0;
    for(int32 I=0;I<700;++I)
    {
        const auto Before=F.GetStage();const auto P=F.Advance(1.f/60,true);
        if(F.GetStage()==FSurveyCameraFlight::EStage::Beacon){TestTrue(TEXT("expanded pond beacon remains at established far eye"),FSurveyCameraFlight::Eye(P).Equals(FarEye,.002));TestTrue(TEXT("expanded pond no early fade"),F.ArrivalAge()<0);}
        if(F.GetStage()==FSurveyCameraFlight::EStage::Approach)
        {
            SawApproach=true;TestTrue(TEXT("full beacon already displayed before approach"),F.BeaconAge()>=FSurveyCameraFlight::BeaconDuration);TestTrue(TEXT("approach cannot start fade"),F.ArrivalAge()<0);
            if(Before==FSurveyCameraFlight::EStage::Approach)TestTrue(TEXT("zoom ratio avoids final rush"),PreviousRange/P.Distance<1.05f);
        }
        if(F.GetStage()==FSurveyCameraFlight::EStage::Complete){SawComplete=true;if(Before!=F.GetStage())TestEqual(TEXT("arrival clock starts exactly at stop"),F.ArrivalAge(),0.f);}
        PreviousRange=P.Distance;
    }
    TestTrue(TEXT("far beacon then approach then complete exercised"),SawApproach&&SawComplete);TestTrue(TEXT("expanded pond retains distant endpoint"),F.GetPose().Distance==B.Distance&&F.GetPose().Focus==B.Focus);return true;
}
#endif
