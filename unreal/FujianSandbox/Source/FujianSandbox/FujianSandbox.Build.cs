using UnrealBuildTool;
public class FujianSandbox : ModuleRules
{
    public FujianSandbox(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "Json", "ProceduralMeshComponent",
            "Slate", "SlateCore", "ApplicationCore"
        });
    }
}
