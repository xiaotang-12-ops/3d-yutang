param([string]$EngineRoot='D:\ue5.8\UE_5.8',[switch]$Standalone,[switch]$FlickerProbe,[switch]$NoRHIThread,[switch]$SyncPresent=$true,[ValidateRange(0,240)][int]$FrameLimit=60)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
$screen=@([System.Windows.Forms.Screen]::AllScreens | Where-Object {-not $_.Primary}) | Select-Object -First 1
if(-not $screen){throw '没有副屏，停止启动。'}
$existing=@(Get-CimInstance Win32_Process -Filter "name='UnrealEditor.exe'" | Where-Object {$_.CommandLine -like '*fujian-dashboard-motion*PondMotion.uproject*'})
if($existing.Count){throw '实验已经运行；先关闭本实验窗口再启动。'}
# 复用既有入口启动本机A/B；不隐式编译、不杀旧实例、不修改投屏配置。-codex，2026-10-28
$workspaceRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$controlProject=Join-Path $workspaceRoot 'unreal\FujianSandbox58\FujianSandbox.uproject'
$surveyPath=Join-Path $workspaceRoot 'shared\xinluo-survey.json'
$syncPath=Join-Path $workspaceRoot 'unreal\FujianSandbox58\Saved\XinluoLink'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if(-not (Test-Path -LiteralPath $surveyPath -PathType Leaf)){throw "缺少真实调研数据：$surveyPath"}
if(-not (Test-Path -LiteralPath $editor -PathType Leaf)){throw "引擎入口不存在：$editor"}
if(-not $Standalone){
    $controlExisting=@(Get-CimInstance Win32_Process -Filter "name='UnrealEditor.exe'" | Where-Object {$_.CommandLine -like '*FujianSandbox58*FujianSandbox.uproject*'})
    if($controlExisting.Count){throw '操作屏工程已运行，请正常关闭该窗口后再使用双屏入口。'}
    if(-not (Test-Path -LiteralPath $controlProject -PathType Leaf)){throw "操作屏工程不存在：$controlProject"}
}
$args=@(('"{0}\PondMotion.uproject"' -f $PSScriptRoot),'/Engine/Maps/Entry','-game','-windowed','-noborder','-ResX=1920','-ResY=1080',('-WinX={0}' -f $screen.Bounds.X),('-WinY={0}' -f $screen.Bounds.Y),'-NoSplash','-NoSound','-NoLiveCoding','-dx11',('-abslog="{0}\Saved\motion-runtime.log"' -f $PSScriptRoot))
if($FlickerProbe){$args+='-MotionFlickerProbe'}
if($NoRHIThread){$args+='-norhithread'}
$args+=('-SurveyData="{0}"' -f $surveyPath)
if(-not $Standalone){$args+=@('-XinluoLinked',('-PondSyncDir="{0}"' -f $syncPath))}
# 桌面实录复现无同步/不限帧时UI整层闪失；启动后执行，避免Saved用户设置的0帧上限覆盖配置。-codex，2026-10-24
$diagnosticCommands=@(('r.VSync {0}' -f [int][bool]$SyncPresent),('t.MaxFPS {0}' -f $FrameLimit))
if($diagnosticCommands.Count){$args+=('-ExecCmds="{0}"' -f ($diagnosticCommands -join ','))}
$p=Start-Process -FilePath $editor -ArgumentList $args -WindowStyle Normal -PassThru
if(-not $Standalone){
    $primary=[System.Windows.Forms.Screen]::PrimaryScreen
    $controlArgs=@(('"{0}"' -f $controlProject),'/Engine/Maps/Entry','-game','-windowed','-ResX=1920','-ResY=1080',('-WinX={0}' -f $primary.Bounds.X),('-WinY={0}' -f $primary.Bounds.Y),'-PondDemo','-ProvinceFX','-XinluoLinked','-NoSplash','-NoLiveCoding','-dx11',('-SurveyData="{0}"' -f $surveyPath),('-PondSyncDir="{0}"' -f $syncPath),('-abslog="{0}\unreal\FujianSandbox58\Saved\xinluo-control.log"' -f $workspaceRoot))
    try{$control=Start-Process -FilePath $editor -ArgumentList $controlArgs -WindowStyle Normal -PassThru}
    catch{throw "展示屏已启动 PID=$($p.Id)，操作屏启动失败；展示屏将等待连接。$($_.Exception.Message)"}
    Write-Output ('Xinluo control PID={0}, display PID={1}; data={2}; sync={3}' -f $control.Id,$p.Id,$surveyPath,$syncPath)
}
# 只检查本次新进程；失败保留另一屏的等待/断连状态，不终止已有实例。
Start-Sleep -Seconds 3
$p.Refresh()
if($p.HasExited){throw "展示屏启动后退出，exit=$($p.ExitCode)；查看 $PSScriptRoot\Saved\motion-runtime.log。其他窗口保持原状。"}
if(-not $Standalone){$control.Refresh();if($control.HasExited){throw "操作屏启动后退出，exit=$($control.ExitCode)；查看 $workspaceRoot\unreal\FujianSandbox58\Saved\xinluo-control.log。展示屏 PID=$($p.Id) 等待连接。"}}
Write-Output ('PondMotion PID={0}, secondary monitor {1}; close with Alt+F4 from this experiment.' -f $p.Id,$screen.Bounds)
