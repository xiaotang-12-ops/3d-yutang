// 调用生产共享契约验证真实数据与恢复边界；运输故障只写全新GUID测试目录，不触碰正常链路。
// 这些自动测试不替代真实A点击、双屏原生帧或物理触摸验收。-codex，2026-10-28
#include "XinluoScreenLink.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXinluoContractTest,"Xinluo.ContractAndNavigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FXinluoContractTest::RunTest(const FString&)
{
    Xinluo::FDataset D;if(!TestTrue(TEXT("演示调查加载"),D.Load(Xinluo::DataPath())))return false;
    TestEqual(TEXT("26主体"),D.Subjects.Num(),26);TestEqual(TEXT("5页"),D.Pages(),5);
    int64 Sum=0;TMap<FString,int32> Species,Modes;int Missing=0;
    for(const auto& S:D.Subjects){Sum+=S.AreaMuTenths;Species.FindOrAdd(S.Species)++;Modes.FindOrAdd(S.Mode)++;if(!S.HasCoordinate)++Missing;}
    TestEqual(TEXT("演示面积合计"),Sum,int64(26*100+27*26/2));
    TestEqual(TEXT("缺坐标主体"),Missing,1);
    TestFalse(TEXT("24号坐标留空"),D.Subjects[23].HasCoordinate);
    TestEqual(TEXT("演示主体名称"),D.Subjects[0].Name,FString(TEXT("演示渔场01")));
    Xinluo::FNavigation N;N.SelectCity(Xinluo::CityCode);N.ChangeGroup(D,-1);TestEqual(TEXT("首页不能向前"),N.Group,0);
    for(int I=0;I<8;++I)N.ChangeGroup(D,1);TestEqual(TEXT("末页上界"),N.Group,4);
    N.SelectSubject(D,TEXT("xinluo-024"));TestEqual(TEXT("缺坐标主体可选"),N.Subject,FString(TEXT("xinluo-024")));TestEqual(TEXT("选择定位正确组"),N.Group,3);
    N.Overview();N.SelectSubject(D,TEXT("xinluo-018"));TestEqual(TEXT("总览中换主体保持总览"),N.View,FString(TEXT("overview")));TestEqual(TEXT("总览换组同步"),N.Group,2);
    N.Back();TestEqual(TEXT("总览返回主体"),N.View,FString(TEXT("subject")));N.Back();TestEqual(TEXT("返回主体列表保组"),N.Group,2);N.Back();TestEqual(TEXT("再返回省览"),N.City,0);
    Xinluo::FState S;S.Session=FGuid::NewGuid().ToString();S.Dataset=D.Version;S.Revision=S.Heartbeat=1;S.Nav.SelectCity(Xinluo::CityCode);
    FString Error;Xinluo::FState Parsed;TestTrue(TEXT("完整协议往返"),Xinluo::FState::Read(S.Serialize(),D,Parsed,Error));
    auto Reject=[&](const TCHAR* Label,const TCHAR* Key,TSharedPtr<FJsonValue> Value){TSharedPtr<FJsonObject> O;Xinluo::Parse(S.Serialize(),O);O->SetField(Key,Value);TestFalse(Label,Xinluo::FState::Read(Xinluo::Json(O.ToSharedRef()),D,Parsed,Error));};
    Reject(TEXT("拒绝空session"),TEXT("sessionId"),MakeShared<FJsonValueString>(TEXT("")));
    Reject(TEXT("拒绝小数revision"),TEXT("revision"),MakeShared<FJsonValueNumber>(1.5));Reject(TEXT("拒绝负页"),TEXT("groupIndex"),MakeShared<FJsonValueNumber>(-1));
    Reject(TEXT("拒绝未知ID"),TEXT("siteId"),MakeShared<FJsonValueString>(TEXT("xinluo-999")));Reject(TEXT("拒绝错组ID"),TEXT("siteId"),MakeShared<FJsonValueString>(TEXT("xinluo-026")));
    Reject(TEXT("拒绝不同数据版"),TEXT("datasetVersion"),MakeShared<FJsonValueString>(TEXT("wrong")));
    Xinluo::FReceiver R;TestTrue(TEXT("首次状态待应用"),R.Observe(S,0));TestTrue(TEXT("pending保留"),R.HasPending);
    ++S.Revision;++S.Heartbeat;S.Nav.ChangeGroup(D,1);R.Observe(S,1);TestEqual(TEXT("延后应用保留最新"),R.Pending.Revision,int64(2));R.Commit();TestEqual(TEXT("提交最新"),R.Accepted.Nav.Group,1);
    auto Invalid=S;Invalid.Nav.ChangeGroup(D,1);TestFalse(TEXT("同rev异内容拒绝"),R.Observe(Invalid,2));TestEqual(TEXT("坏状态不覆盖"),R.Accepted.Nav.Group,1);
    ++S.Revision;++S.Heartbeat;S.Nav.Overview();R.Observe(S,3);R.Commit();TestTrue(TEXT("有效心跳"),R.HasPulse);TestEqual(TEXT("断连提示"),R.Health(7),FString(TEXT("操作屏连接中断")));
    const FString Old=S.Session;S.Session=FGuid::NewGuid().ToString();S.Revision=S.Heartbeat=1;TestTrue(TEXT("新会话恢复"),R.Observe(S,8));R.Commit();TestFalse(TEXT("残留快照不算活连接"),R.HasPulse);
    S.Session=Old;TestFalse(TEXT("拒绝退休会话"),R.Observe(S,9));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXinluoTransportTest,"Xinluo.LocalTransport",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FXinluoTransportTest::RunTest(const FString&)
{
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("XinluoTests")/FGuid::NewGuid().ToString();
    {
        Xinluo::FTransport A,Duplicate,B;TestTrue(TEXT("A取锁"),A.OpenAt(Dir,TEXT("controller")));TestFalse(TEXT("重复A被OS拒绝"),Duplicate.OpenAt(Dir,TEXT("controller")));TestTrue(TEXT("B独立角色锁"),B.OpenAt(Dir,TEXT("display")));
        TestTrue(TEXT("发布初始快照"),A.Write(TEXT("state.json"),TEXT("old")));FString Read;TestTrue(TEXT("B读取"),B.Read(TEXT("state.json"),Read));TestEqual(TEXT("完整初值"),Read,FString(TEXT("old")));
#if PLATFORM_WINDOWS
        const FString Path=FPaths::ConvertRelativePathToFull(Dir/TEXT("state.json"));
        auto Block=::CreateFileW(*Path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(TestTrue(TEXT("人为占用测试文件"),Block!=INVALID_HANDLE_VALUE))
        {
            TestFalse(TEXT("替换被占用文件失败"),A.Write(TEXT("state.json"),TEXT("newest")));::CloseHandle(Block);
            B.Read(TEXT("state.json"),Read);TestEqual(TEXT("失败保留旧快照"),Read,FString(TEXT("old")));
            TestTrue(TEXT("权限恢复重试最新"),A.Write(TEXT("state.json"),TEXT("newest")));B.Read(TEXT("state.json"),Read);TestEqual(TEXT("最终完整最新值"),Read,FString(TEXT("newest")));
        }
#endif
    }
    Xinluo::FTransport Restart;TestTrue(TEXT("进程句柄释放后锁文件存在仍可重启"),Restart.OpenAt(Dir,TEXT("controller")));
    AddInfo(FString(TEXT("隔离运输测试目录："))+Dir);return true;
}
#endif
