using UnrealBuildTool;
public class PondMotionTarget : TargetRules
{
 public PondMotionTarget(TargetInfo Target):base(Target){Type=TargetType.Game;DefaultBuildSettings=BuildSettingsVersion.V7;IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8;ExtraModuleNames.Add("PondMotion");}
}
