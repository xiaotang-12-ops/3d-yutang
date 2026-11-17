using UnrealBuildTool;
public class MediaLoopBench : ModuleRules
{
    public MediaLoopBench(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "ApplicationCore", "RenderCore", "RHI", "MediaAssets", "MediaUtils", "ImgMedia", "ImgMediaFactory" });
    }
}
