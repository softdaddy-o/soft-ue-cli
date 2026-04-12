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
			// HTTP Server & Sockets
			"HTTP",
			"HTTPServer",
			"Sockets",
			"Networking",

			// JSON
			"Json",
			"JsonUtilities",

			// Input simulation
			"InputCore",

			// Project info
			"Projects",

			// Image compression (viewport capture)
			"ImageWrapper",

			// Rendering (viewport ReadPixels)
			"RenderCore",
			"RHI",

			// AI navigation (trigger-input move-to)
			"AIModule",
			"NavigationSystem",

			// Animation state machine inspection
			"AnimGraphRuntime"
		});

		// Editor-only modules (viewport capture + foliage/landscape queries)
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("Foliage");
			PrivateDependencyModuleNames.Add("Landscape");
			PrivateDependencyModuleNames.Add("LandscapeEditor");
		}
	}
}
