// 同机双屏文件运输：每个角色持有OS独占句柄，完整快照原子替换，坏文件不改变内存目标。
// 锁文件存在不代表活跃；句柄随进程退出释放。接受与可见修订分开回执。-codex，2026-10-28
#pragma once
#include "XinluoSurveyData.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#if PLATFORM_WINDOWS
#pragma push_macro("RGB")
#include "Windows/WindowsHWrapper.h"
#pragma pop_macro("RGB")
#endif
namespace Xinluo
{
inline constexpr const TCHAR* HealthyReceipt=TEXT("操作屏状态已接受");
class FTransport
{
    void* LockHandle=nullptr;
public:
    FString Directory,Error;
    ~FTransport()
    {
#if PLATFORM_WINDOWS
        if(LockHandle)::CloseHandle(LockHandle);
#endif
    }
    FTransport()=default;FTransport(const FTransport&)=delete;FTransport& operator=(const FTransport&)=delete;
    bool Open(const TCHAR* Role)
    {
        FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("PondSyncDir="),Path))Path=FPaths::ProjectDir()/TEXT("../../unreal/FujianSandbox58/Saved/XinluoLink");
        return OpenAt(Path,Role);
    }
    bool OpenAt(const FString& Path,const TCHAR* Role)
    {
        if(LockHandle)return false;
        Directory=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Directory);FPaths::CollapseRelativeDirectories(Directory);
        if(!IFileManager::Get().MakeDirectory(*Directory,true)){Error=TEXT("同步目录不可创建");return false;}
#if PLATFORM_WINDOWS
        const FString Lock=Directory/(FString(Role)+TEXT(".lock"));
        // share=0由操作系统保证同一文件（包括路径别名）只有一个持有者；不删锁文件、不杀其他进程。
        auto Handle=::CreateFileW(*Lock,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(Handle==INVALID_HANDLE_VALUE){Error=TEXT("此同步目录已有同角色运行实例，或目录无写权限");return false;}
        LockHandle=Handle;Error.Empty();return true;
#else
        Error=TEXT("当前同步仅支持本机Windows双屏");return false;
#endif
    }
    bool Read(const TCHAR* Name,FString& Text)const
    {
        const FString Path=Directory/Name;const int64 Size=IFileManager::Get().FileSize(*Path);
        return LockHandle&&Size>0&&Size<=65536&&FFileHelper::LoadFileToString(Text,*Path);
    }
    bool Write(const TCHAR* Name,const FString& Text)
    {
        if(!LockHandle)return false;
        const FString Path=Directory/Name,Temp=Path+TEXT(".pending");
        if(!FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Error=TEXT("同步文件写入失败，等待重试");return false;}
#if PLATFORM_WINDOWS
        // 同目录NTFS替换，失败不触碰上一快照；上层保留dirty，下一次写的是最新目标而非旧成功值。
        if(!::MoveFileExW(*Temp,*Path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){Error=TEXT("同步快照替换失败，等待重试");return false;}
#else
        return false;
#endif
        Error.Empty();return true;
    }
};
struct FAck
{
    FString Session,Dataset,ReceiverSession,Health;int64 Accepted=0,Visible=0,Heartbeat=0;
    FString Serialize()const
    {
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("schemaVersion"),1);O->SetStringField(TEXT("sessionId"),Session);O->SetStringField(TEXT("datasetVersion"),Dataset);O->SetStringField(TEXT("receiverSession"),ReceiverSession);O->SetStringField(TEXT("health"),Health);O->SetNumberField(TEXT("acceptedRevision"),Accepted);O->SetNumberField(TEXT("visibleRevision"),Visible);O->SetNumberField(TEXT("heartbeat"),Heartbeat);return Json(O);
    }
    static bool Read(const FString& Text,FAck& Out)
    {
        TSharedPtr<FJsonObject> O;FAck A;int64 Schema=0;
        if(!Parse(Text,O)||!Integer(O,TEXT("schemaVersion"),Schema,1,1)||!String(O,TEXT("sessionId"),A.Session,true)||!String(O,TEXT("datasetVersion"),A.Dataset)||!String(O,TEXT("receiverSession"),A.ReceiverSession)||!String(O,TEXT("health"),A.Health)||!Integer(O,TEXT("acceptedRevision"),A.Accepted)||!Integer(O,TEXT("visibleRevision"),A.Visible)||!Integer(O,TEXT("heartbeat"),A.Heartbeat,1)||A.Visible>A.Accepted)return false;
        FGuid Guid;if(!FGuid::Parse(A.ReceiverSession,Guid)||!Guid.IsValid())return false;
        Out=MoveTemp(A);return true;
    }
};
// 接收门禁只更新已验证的pending，调用方确认目标已应用才提交Accepted，避免去重吞掉延迟目标。
struct FReceiver
{
    FState Seen,Accepted,Pending;bool HasSeen=false,HasPending=false,HasPulse=false;
    TSet<FString> RetiredSessions;double LastPulse=0;FString Error;
    bool Observe(const FState& S,double Now)
    {
        if(RetiredSessions.Contains(S.Session)){Error=TEXT("旧会话快照已拒绝");return false;}
        if(HasSeen&&S.Session==Seen.Session)
        {
            if(S.Revision<Seen.Revision||S.Heartbeat<Seen.Heartbeat){Error=TEXT("过期修订已拒绝");return false;}
            if(S.Revision==Seen.Revision&&S.Nav!=Seen.Nav){Error=TEXT("相同修订内容不一致");return false;}
            if(S.Heartbeat>Seen.Heartbeat){LastPulse=Now;HasPulse=true;}
        }
        else
        {
            if(HasSeen)RetiredSessions.Add(Seen.Session);
            LastPulse=Now;HasPulse=false;
        }
        Seen=S;HasSeen=true;Error.Empty();
        if(Accepted.Session!=S.Session||Accepted.Revision!=S.Revision){Pending=S;HasPending=true;}
        return true;
    }
    void Commit(){if(HasPending){Accepted=Pending;HasPending=false;}}
    FString Health(double Now)const
    {
        if(!Error.IsEmpty())return Error;
        if(!HasSeen||!HasPulse)return TEXT("等待操作屏连接");
        if(Now-LastPulse>3)return TEXT("操作屏连接中断");
        return HealthyReceipt;
    }
};
}
