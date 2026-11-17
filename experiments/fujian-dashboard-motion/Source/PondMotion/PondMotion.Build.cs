using UnrealBuildTool;
public class PondMotion:ModuleRules
{
 public PondMotion(ReadOnlyTargetRules Target):base(Target){PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;PublicIncludePaths.Add(System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory,"../../../../shared")));PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","InputCore","Slate","SlateCore","ApplicationCore","Json","RenderCore","RHI","ImageWrapper","ProceduralMeshComponent","MediaAssets","MediaUtils","ImgMedia","ImgMediaFactory"});}
}
