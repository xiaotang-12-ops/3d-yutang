param(
    [ValidateSet('original','concat','sequence')][string]$MediaCase = 'original',
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$RunLabel,
    [string]$EvidenceRoot = 'D:/3d-research/artifacts/fujian-dashboard-motion/media-compare-20261026',
    [ValidateRange(1,120)][double]$Duration = 18
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$screen = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
if (-not $screen) { throw 'Secondary monitor required; refusing to occupy primary monitor.' }
if ($screen.Bounds.Width -lt 1920 -or $screen.Bounds.Height -lt 1080) { throw 'Secondary monitor smaller than 1920x1080.' }
$project = Join-Path $PSScriptRoot 'MediaLoopBench.uproject'
if (-not (Test-Path (Join-Path $PSScriptRoot 'Binaries/Win64/UnrealEditor-MediaLoopBench.dll'))) { throw 'Build the isolated module first.' }
$existing = Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*MediaLoopBench.uproject*' }
if ($existing) { throw 'An isolated benchmark instance is already running.' }
if (Test-Path (Join-Path $EvidenceRoot $RunLabel)) { throw 'RunLabel already exists; choose a fresh label.' }
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$log = Join-Path $EvidenceRoot "$RunLabel-engine.log"
if (Test-Path $log) { throw 'Run log already exists; choose a fresh label.' }
$argsList = @(
    "`"$project`"", '/Engine/Maps/Entry', '-game', '-windowed', '-noborder', '-ResX=1920', '-ResY=1080',
    "-WinX=$($screen.Bounds.X)", "-WinY=$($screen.Bounds.Y)", '-NoSplash', '-NoSound', '-NoLiveCoding', '-dx11',
    "-MediaCase=$MediaCase", "-RunLabel=$RunLabel", "-EvidenceRoot=`"$EvidenceRoot`"", "-Duration=$Duration",
    "-abslog=`"$log`"", '-ExecCmds="r.VSync 1,t.MaxFPS 60"'
)
$process = Start-Process 'D:/ue5.8/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe' -ArgumentList $argsList -WindowStyle Normal -PassThru
Write-Output "PID=$($process.Id) case=$MediaCase label=$RunLabel log=$log"

