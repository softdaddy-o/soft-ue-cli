// Copyright soft-ue-expert. All Rights Reserved.

#include "SoftUEBridgeEditorModule.h"
#include "Tools/BridgeToolRegistry.h"
#include "UI/BridgeToolbarExtension.h"

// Analysis
#include "Tools/Analysis/ClassHierarchyTool.h"

// Asset
#include "Tools/Asset/QueryAssetTool.h"
#include "Tools/Asset/DeleteAssetTool.h"
#include "Tools/Asset/GetAssetDiffTool.h"
#include "Tools/Asset/GetAssetPreviewTool.h"
#include "Tools/Asset/OpenAssetTool.h"

// Blueprint
#include "Tools/Blueprint/QueryBlueprintTool.h"
#include "Tools/Blueprint/QueryBlueprintGraphTool.h"

// Build
#include "Tools/Build/BuildAndRelaunchTool.h"
#include "Tools/Build/TriggerLiveCodingTool.h"

// Editor
#include "Tools/Editor/CaptureScreenshotTool.h"

// Material
#include "Tools/Material/QueryMaterialTool.h"

// PIE
#include "Tools/PIE/PieSessionTool.h"
#include "Tools/PIE/PieInputTool.h"

// Performance
#include "Tools/Performance/InsightsCaptureTool.h"
#include "Tools/Performance/InsightsListTracesTool.h"
#include "Tools/Performance/InsightsAnalyzeTool.h"

// Project
#include "Tools/Project/ProjectInfoTool.h"

// References
#include "Tools/References/FindReferencesTool.h"

// Scripting
#include "Tools/Scripting/RunPythonScriptTool.h"

// StateTree
#include "Tools/StateTree/QueryStateTreeTool.h"
#include "Tools/StateTree/AddStateTreeStateTool.h"
#include "Tools/StateTree/AddStateTreeTaskTool.h"
#include "Tools/StateTree/AddStateTreeTransitionTool.h"
#include "Tools/StateTree/RemoveStateTreeStateTool.h"

// Widget
#include "Tools/Widget/WidgetBlueprintTool.h"

// Write
#include "Tools/Write/EditorSpawnActorTool.h"
#include "Tools/Write/EditorSetPropertyTool.h"
#include "Tools/Write/AddComponentTool.h"
#include "Tools/Write/AddWidgetTool.h"
#include "Tools/Write/AddDataTableRowTool.h"
#include "Tools/Write/AddGraphNodeTool.h"
#include "Tools/Write/RemoveGraphNodeTool.h"
#include "Tools/Write/ConnectGraphPinsTool.h"
#include "Tools/Write/DisconnectGraphPinTool.h"
#include "Tools/Write/SetNodePositionTool.h"
#include "Tools/Write/CreateAssetTool.h"

DEFINE_LOG_CATEGORY(LogSoftUEBridgeEditor);

void FSoftUEBridgeEditorModule::StartupModule()
{
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("SoftUEBridgeEditor module starting up"));

	FBridgeToolRegistry& Registry = FBridgeToolRegistry::Get();

	// Analysis
	Registry.RegisterToolClass<UClassHierarchyTool>();

	// Asset
	Registry.RegisterToolClass<UQueryAssetTool>();
	Registry.RegisterToolClass<UDeleteAssetTool>();
	Registry.RegisterToolClass<UGetAssetDiffTool>();
	Registry.RegisterToolClass<UGetAssetPreviewTool>();
	Registry.RegisterToolClass<UOpenAssetTool>();

	// Blueprint
	Registry.RegisterToolClass<UQueryBlueprintTool>();
	Registry.RegisterToolClass<UQueryBlueprintGraphTool>();

	// Build
	Registry.RegisterToolClass<UBuildAndRelaunchTool>();
	Registry.RegisterToolClass<UTriggerLiveCodingTool>();

	// Editor
	Registry.RegisterToolClass<UCaptureScreenshotTool>();

	// Material
	Registry.RegisterToolClass<UQueryMaterialTool>();

	// PIE
	Registry.RegisterToolClass<UPieSessionTool>();
	Registry.RegisterToolClass<UPieInputTool>();

	// Performance
	Registry.RegisterToolClass<UInsightsCaptureTool>();
	Registry.RegisterToolClass<UInsightsListTracesTool>();
	Registry.RegisterToolClass<UInsightsAnalyzeTool>();

	// Project
	Registry.RegisterToolClass<UProjectInfoTool>();

	// References
	Registry.RegisterToolClass<UFindReferencesTool>();

	// Scripting
	Registry.RegisterToolClass<URunPythonScriptTool>();

	// StateTree
	Registry.RegisterToolClass<UQueryStateTreeTool>();
	Registry.RegisterToolClass<UAddStateTreeStateTool>();
	Registry.RegisterToolClass<UAddStateTreeTaskTool>();
	Registry.RegisterToolClass<UAddStateTreeTransitionTool>();
	Registry.RegisterToolClass<URemoveStateTreeStateTool>();

	// Widget
	Registry.RegisterToolClass<UWidgetBlueprintTool>();

	// Write
	Registry.RegisterToolClass<UEditorSpawnActorTool>();
	Registry.RegisterToolClass<UEditorSetPropertyTool>();
	Registry.RegisterToolClass<UAddComponentTool>();
	Registry.RegisterToolClass<UAddWidgetTool>();
	Registry.RegisterToolClass<UAddDataTableRowTool>();
	Registry.RegisterToolClass<UAddGraphNodeTool>();
	Registry.RegisterToolClass<URemoveGraphNodeTool>();
	Registry.RegisterToolClass<UConnectGraphPinsTool>();
	Registry.RegisterToolClass<UDisconnectGraphPinTool>();
	Registry.RegisterToolClass<USetNodePositionTool>();
	Registry.RegisterToolClass<UCreateAssetTool>();

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Registered %d editor bridge tools"), Registry.GetToolCount());

	FBridgeToolbarExtension::Initialize();
}

void FSoftUEBridgeEditorModule::ShutdownModule()
{
	FBridgeToolbarExtension::Shutdown();
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("SoftUEBridgeEditor module shutting down"));
}

IMPLEMENT_MODULE(FSoftUEBridgeEditorModule, SoftUEBridgeEditor)
