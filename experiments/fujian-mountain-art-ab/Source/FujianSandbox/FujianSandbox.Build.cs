using UnrealBuildTool;
public class FujianSandbox : ModuleRules
{
    public FujianSandbox(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "Json", "ProceduralMeshComponent",
            "Slate", "SlateCore", "ApplicationCore"
        });
    }
}
