// Copyright soft-ue-expert. All Rights Reserved.

using UnrealBuildTool;

public class SoftUEBridgeEditor : ModuleRules
{
	public SoftUEBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SoftUEBridge"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor Framework
			"UnrealEd",
			"EditorSubsystem",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"StatusBar",

			// Blueprint/Kismet
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",

			// Animation Blueprint
			"AnimGraph",

			// Asset Management
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",

			// Level Editing
			"LevelEditor",
			"EditorScriptingUtilities",

			// UI/Widgets
			"UMG",
			"UMGEditor",
			"PropertyEditor",
			"PropertyPath",

			// Animation/Sequencer (for UMG animations)
			"MovieScene",

			// HTTP (required by SoftUEBridge public headers via BridgeServer.h)
			"HTTP",
			"HTTPServer",

			// JSON
			"Json",
			"JsonUtilities",

			// Input
			"InputCore",

			// AI/Navigation (for PIE input tools)
			"AIModule",
			"NavigationSystem",

			// Project Settings
			"GameplayTags",
			"EngineSettings",
			"Projects",

			// StateTree
			"StateTreeModule",
			"StateTreeEditorModule",

			// Python Scripting
			"PythonScriptPlugin",

			// Live Coding
			"LiveCoding",

			// Source Control (for SCM diff tool)
			"SourceControl",

			// Image Processing (for asset preview tool)
			"ImageWrapper"
		});
	}
}
