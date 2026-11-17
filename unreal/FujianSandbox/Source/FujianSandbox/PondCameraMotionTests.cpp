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
    for(int32 Phase=0;Phase<2;Phase++)for(int32 Repeat=0;Repeat<3;Repeat++)
    {
        M.Start(From,To);M.Advance(Phase?2.4f:.6f);const auto Frozen=M.Cancel();
        const auto After=M.Advance(20);TestTrue(TEXT("cancel freezes focus"),After.Focus.Equals(Frozen.Focus));
        TestEqual(TEXT("cancel freezes distance"),After.Distance,Frozen.Distance);TestEqual(TEXT("cancel freezes angle"),After.Yaw,Frozen.Yaw);
        FPondCameraPose New=To;New.Focus=FVector(-400,500,200);New.Yaw=150;
        M.Start(Frozen,New,12);const auto End=M.Advance(6);TestTrue(TEXT("latest target wins"),End.Focus.Equals(New.Focus));
        TestTrue(TEXT("finite orbit completes"),!M.IsActive());const auto Hold=M.Advance(60);
        TestTrue(TEXT("hold remains stable"),Hold.Focus.Equals(End.Focus)&&Hold.Distance==End.Distance&&Hold.Yaw==End.Yaw);
    }
    return true;
}
#endif
