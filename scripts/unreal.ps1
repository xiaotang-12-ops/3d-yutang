<#
职责：检查指定 UE5 安装，环境具备后编译、打开或打包本机原生工程。
默认只读检查；不下载引擎、不安装软件、不接受许可、不处理凭证。-codex，2026-10-16
#>
param(
    [ValidateSet('Check','Build','Run','Package','RunPackage')][string]$Action = 'Check',
    [string]$EngineRoot = '',
    [string]$OutputDirectory = '',
    [string]$ProjectDirectory = '',
    [switch]$Showcase,
    [switch]$ReviewMonitor,
    [switch]$ReviewFullHD
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if($ReviewMonitor -and -not $Showcase){throw 'ReviewMonitor 仅配合 Showcase 使用。'}
if($ReviewFullHD -and -not $ReviewMonitor){throw 'ReviewFullHD 仅配合 ReviewMonitor 使用。'}
$taskRoot = Split-Path -Parent $PSScriptRoot
# 新引擎在明确指定的工程副本中编译；默认仍指旧工程，避免原资产被静默升级。-codex，2026-10-22
if(-not $ProjectDirectory){$ProjectDirectory=Join-Path $taskRoot 'unreal\FujianSandbox'}
$projectPath = Join-Path ([System.IO.Path]::GetFullPath($ProjectDirectory)) 'FujianSandbox.uproject'
# 独立演示入口不依赖编辑器、VS或引擎安装；仍保留完整归档目录。-codex，2026-10-21
if($Action -eq 'RunPackage'){
    if(-not $OutputDirectory){$OutputDirectory=Join-Path $taskRoot 'artifacts\pond-windows'}
    $packageEntry=Join-Path ([System.IO.Path]::GetFullPath($OutputDirectory)) 'Windows\FujianSandbox.exe'
    if(-not (Test-Path -LiteralPath $packageEntry -PathType Leaf)){throw "未找到独立包入口：$packageEntry。请先打包，不能退回编辑器冒充独立运行。"}
    $packageArgs=@('-windowed','-ResX=1920','-ResY=1080','-nosplash')
    if($Showcase){$packageArgs+='-PondDemo'}
    if($ReviewMonitor){
        Add-Type -AssemblyName System.Windows.Forms
        $packageScreen=@([System.Windows.Forms.Screen]::AllScreens | Where-Object {-not $_.Primary}) | Select-Object -First 1
        if(-not $packageScreen){throw '未发现第二物理屏，取消启动ReviewMonitor。'}
        $packageArgs=@('-windowed','-nosplash','-PondDemo','-PondReviewMonitor',('-ResX={0}' -f [int][Math]::Floor($packageScreen.WorkingArea.Width*.49)),('-ResY={0}' -f [int][Math]::Floor([Math]::Min($packageScreen.WorkingArea.Height-80,$packageScreen.WorkingArea.Width*.49*9.0/16))),('-WinX={0}' -f ($packageScreen.WorkingArea.Left+4)),('-WinY={0}' -f ($packageScreen.WorkingArea.Top+60)))
        if($ReviewFullHD){
            # 同屏并排只供联动检查；清晰度检查使用既有双1080窗口，二者在副屏叠放。-codex，2026-10-23
            $packageArgs=@($packageArgs | Where-Object {$_ -notlike '-ResX=*' -and $_ -notlike '-ResY=*'})
            $packageArgs+=@('-PondReviewFullHD','-ResX=1920','-ResY=1080')
        }
    }
    $packageProcess=Start-Process -FilePath $packageEntry -ArgumentList $packageArgs -WindowStyle Normal -PassThru
    [pscustomobject]@{launchedProcessId=$packageProcess.Id;entry=$packageEntry;showcase=[bool]$Showcase;secondMonitorReview=[bool]$ReviewMonitor;runtimeVerified=$false} | ConvertTo-Json
    return
}
$candidateRoots = @()
if ($EngineRoot) { $candidateRoots += $EngineRoot }
else {
    $candidateRoots += @('D:\ue5.6.1\UE_5.6','C:\Program Files\Epic Games\UE_5.6','D:\Epic Games\UE_5.6','D:\UE_5.6')
    $installed = @(Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\*' -ErrorAction SilentlyContinue)
    foreach ($item in $installed) { if ($item.PSObject.Properties['InstalledDirectory']) { $candidateRoots += $item.InstalledDirectory } }
}
$foundRoot = ''
foreach ($candidate in $candidateRoots) {
    if (Test-Path -LiteralPath (Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe')) { $foundRoot = (Resolve-Path -LiteralPath $candidate).Path; break }
}
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$cppToolchain = @()
# UE5.6 使用的 14.38 是独立组件；只查“最新 C++ 工具”会把已安装的固定版本误判成缺失。
if (Test-Path -LiteralPath $vswhere) { $cppToolchain = @(& $vswhere -latest -products '*' -requiresAny -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.14.38.17.8.x86.x64 -property installationPath) }
$engineVersion = '未知版本'
if ($foundRoot) {
    $versionFile = Join-Path $foundRoot 'Engine\Build\Build.version'
    if (Test-Path -LiteralPath $versionFile) {
        $versionData = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
        $engineVersion = '{0}.{1}.{2}' -f $versionData.MajorVersion,$versionData.MinorVersion,$versionData.PatchVersion
    }
}
$result = [pscustomobject]@{
    project = $projectPath
    sourceTarget = ('Unreal Engine {0} / Windows x64' -f $engineVersion)
    engineFound = [bool]$foundRoot
    engineRoot = $foundRoot
    cppToolchainFound = ($cppToolchain.Count -gt 0)
    toolchainPaths = $cppToolchain
    runtimeVerified = $false
    note = '文件/前置条件检查不代表编译、运行或打包成功。'
}
$result | ConvertTo-Json -Depth 4
if ($Action -eq 'Check') { return }
if (-not $foundRoot) { throw 'UE5 未安装或路径未找到。请通过 -EngineRoot 指定已安装的对应引擎位置。' }
if ($cppToolchain.Count -eq 0) { throw '未发现 Visual Studio C++ 工具链。请安装当前引擎对应的 Visual Studio C++ 游戏开发组件及 Windows SDK。' }
if ($Action -eq 'Package') {
    # 使用引擎官方流水线，避免把编辑器 DLL 或网页产物误当作原生 EXE 交付。-codex，2026-10-16
    $uatFile = Join-Path $foundRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    if (-not (Test-Path -LiteralPath $uatFile)) { throw '未找到 RunUAT.bat；请先确认引擎完整安装，不能继续打包。' }
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskRoot 'artifacts\pond-windows' }
    $OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
    # UE5.8的Zen可能使用IPv6回环；只补本次子进程直连名单，结束恢复，不改系统代理。-codex，2026-10-22
    $previousNoProxy = $env:NO_PROXY
    try {
        $env:NO_PROXY = (@($previousNoProxy,'localhost','127.0.0.1','::1','[::1]') | Where-Object { $_ }) -join ','
        & $uatFile BuildCookRun "-project=$projectPath" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -package -pak -prereqs -nodebuginfo -archive "-archivedirectory=$OutputDirectory" -ubtargs=-NoHotReloadFromIDE -utf8output
        $packageExit = $LASTEXITCODE
    }
    finally { $env:NO_PROXY = $previousNoProxy }
    if ($packageExit -ne 0) { throw "UE5 打包未通过，退出码 $packageExit；不能声明 EXE 可用。" }
    $packageExecutables = @(Get-ChildItem -LiteralPath $OutputDirectory -Filter 'FujianSandbox.exe' -File -Recurse)
    if ($packageExecutables.Count -eq 0) { throw 'UAT 已结束但归档中未找到 FujianSandbox.exe，仍需核对产物。' }
    $packageExecutables | Select-Object FullName, Length
    Write-Output '已找到归档 EXE；仍需实际启动并验证地图、点位、资源与交互，不能据此声明运行通过。'
    return
}
$buildFile = Join-Path $foundRoot 'Engine\Build\BatchFiles\Build.bat'
& $buildFile FujianSandboxEditor Win64 Development "-Project=$projectPath" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "UE5 编译未通过，退出码 $LASTEXITCODE；不能继续声称原生工程可运行。" }
if ($Action -eq 'Run') {
    $editor = Join-Path $foundRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $argumentList = @(('"{0}"' -f $projectPath), '/Engine/Maps/Entry', '-game', '-windowed', '-ResX=1920', '-ResY=1080')
    if($Showcase){$argumentList += @('-PondDemo','-NoLiveCoding','-nosplash')}
    if($ReviewMonitor){
        # 仅只读枚举副屏并设置本程序初始位置，避免闪到正在被其他AI使用的主屏。-codex，2026-10-21
        Add-Type -AssemblyName System.Windows.Forms
        $reviewScreen = @([System.Windows.Forms.Screen]::AllScreens | Where-Object {-not $_.Primary}) | Select-Object -First 1
        if(-not $reviewScreen){throw '未发现第二物理屏，请先连接副屏后再启动ReviewMonitor。'}
        $argumentList = @($argumentList | Where-Object {$_ -notlike '-ResX=*' -and $_ -notlike '-ResY=*'})
        $initialWidth = [int][Math]::Floor($reviewScreen.WorkingArea.Width*.49)
        $initialHeight = [int][Math]::Floor([Math]::Min($reviewScreen.WorkingArea.Height-80,$initialWidth*9.0/16))
        $argumentList += @("-ResX=$initialWidth","-ResY=$initialHeight")
        $argumentList += @('-PondReviewMonitor',('-WinX={0}' -f ($reviewScreen.WorkingArea.Left+4)),('-WinY={0}' -f ($reviewScreen.WorkingArea.Top+60)))
        if($ReviewFullHD){
            $argumentList=@($argumentList | Where-Object {$_ -notlike '-ResX=*' -and $_ -notlike '-ResY=*'})
            $argumentList+=@('-PondReviewFullHD','-ResX=1920','-ResY=1080')
        }
    }
    # 这是用户要求查看的交互窗口；保留 PID 便于核对、验收和只关闭本次样机。-codex，2026-10-16
    $nativeProcess = Start-Process -FilePath $editor -ArgumentList $argumentList -WindowStyle Normal -PassThru
    [pscustomobject]@{ launchedProcessId=$nativeProcess.Id; showcase=[bool]$Showcase; secondMonitorReview=[bool]$ReviewMonitor; runtimeVerified=$false } | ConvertTo-Json
    Write-Output '启动请求已发送；需实际看到原生窗口并验证交互，才能将 runtimeVerified 改为通过。'
}
