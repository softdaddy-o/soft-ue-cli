// Copyright soft-ue-expert. All Rights Reserved.

using UnrealBuildTool;

public class SoftUEBridge : ModuleRules
{
	public SoftUEBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Expose Private so the editor module can inherit from runtime tool classes
		PublicIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// HTTP Server
			"HTTP",
			"HTTPServer",

			// JSON
			"Json",
			"JsonUtilities",

			// Input simulation
			"InputCore",

			// Project info
			"Projects"
		});
	}
}
