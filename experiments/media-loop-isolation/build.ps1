$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'MediaLoopBench.uproject'
& 'D:/ue5.8/UE_5.8/Engine/Build/BatchFiles/Build.bat' MediaLoopBenchEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
exit $LASTEXITCODE
