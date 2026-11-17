param([string]$EngineRoot='D:\ue5.8\UE_5.8')
$ErrorActionPreference='Stop'
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" PondMotionEditor Win64 Development "-Project=$PSScriptRoot\PondMotion.uproject" -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=3
if($LASTEXITCODE -ne 0){throw "PondMotion build failed: $LASTEXITCODE"}
