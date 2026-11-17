// 验证真正容易出错的中断/换目标/跨180度/停稳语义；不替代实际镜头构图和设备交互验收。-codex，2026-10-21
#include "PondCameraMotion.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPondMotionTest,"Fujian.Camera.FiniteAndInterruptible",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPondMotionTest::RunTest(const FString&)
{
    FPondCameraPose From,To;From.Yaw=179;To.Yaw=-179;To.Distance=3000;To.Focus=FVector(100,200,300);
    FPondCameraMotion M;M.Start(From,To,24);
    const auto Mid=M.Advance(.75f);TestTrue(TEXT("angle follows shortest arc"),FMath::Abs(Mid.Yaw-180)<.1f);
    for(float CancelAt:{.6f,1.2f,2.4f})for(int32 Repeat=0;Repeat<3;Repeat++)
    {
        M.Start(From,To);M.Advance(CancelAt);const auto Frozen=M.Cancel();
        const auto After=M.Advance(20);TestTrue(TEXT("cancel freezes focus"),After.Focus.Equals(Frozen.Focus));
        TestEqual(TEXT("cancel freezes distance"),After.Distance,Frozen.Distance);TestEqual(TEXT("cancel freezes angle"),After.Yaw,Frozen.Yaw);
        FPondCameraPose New=To;New.Focus=FVector(-400,500,200);New.Yaw=150;
        M.Start(Frozen,New,12);const auto End=M.Advance(6);TestTrue(TEXT("latest target wins"),End.Focus.Equals(New.Focus));
        TestTrue(TEXT("finite orbit completes"),!M.IsActive());const auto Hold=M.Advance(60);
        TestTrue(TEXT("hold remains stable"),Hold.Focus.Equals(End.Focus)&&Hold.Distance==End.Distance&&Hold.Yaw==End.Yaw);
    }
    // 采样真实Advance输出而非重写曲线；定位结束时应仍在转，整体时间和终点保持。-codex，2026-10-23
    auto YawAt=[](float Seconds)
    {
        FPondCameraMotion Sample;FPondCameraPose A,B;A.Yaw=B.Yaw=0;
        Sample.Start(A,B,24);return Sample.Advance(Seconds).Yaw;
    };
    const float JunctionSpeed=(YawAt(1.51f)-YawAt(1.49f))/.02f;
    TestTrue(TEXT("orbit already moves across approach endpoint"),JunctionSpeed>.5f);
    FPondCameraPose Straight=From;Straight.Yaw=From.Yaw;
    M.Start(From,Straight,24);M.Advance(4.69f);TestTrue(TEXT("total duration not shortened"),M.IsActive());
    const auto Final=M.Advance(.02f);TestTrue(TEXT("finite duration preserved"),!M.IsActive());
    TestTrue(TEXT("exact target plus orbit"),FMath::Abs(Final.Yaw-(From.Yaw+24))<.0001f);
    M.Start(From,To,0);const auto NoOrbit=M.Advance(1.5f);
    TestTrue(TEXT("no orbit finishes after approach"),!M.IsActive()&&NoOrbit.Focus.Equals(To.Focus));
    M.Start(From,To,24);const auto Zero=M.Advance(0);const auto Negative=M.Advance(-1);
    TestTrue(TEXT("nonpositive delta does not advance"),Zero.Focus.Equals(From.Focus)&&Negative.Yaw==Zero.Yaw);
    const auto BeforeReplace=M.Advance(1.2f);M.Start(BeforeReplace,Straight,24);
    const auto Restart=M.Advance(0);TestTrue(TEXT("restart begins at current pose"),Restart.Focus.Equals(BeforeReplace.Focus)&&Restart.Yaw==BeforeReplace.Yaw);
    const auto Huge=M.Advance(10000);const auto Hold=M.Advance(10000);
    TestTrue(TEXT("large delta ends once and holds"),!M.IsActive()&&Huge.Yaw==Hold.Yaw&&Huge.Focus.Equals(Hold.Focus));
    return true;
}
#endif
