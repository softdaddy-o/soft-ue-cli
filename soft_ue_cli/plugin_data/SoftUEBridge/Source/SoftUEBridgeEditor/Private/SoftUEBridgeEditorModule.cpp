// Copyright soft-ue-expert. All Rights Reserved.

#include "SoftUEBridgeEditorModule.h"
#include "Tools/BridgeToolRegistry.h"
#include "UI/BridgeToolbarExtension.h"
#include "Misc/CoreDelegates.h"

// Analysis
#include "Tools/Analysis/ClassHierarchyTool.h"
#include "Tools/Analysis/ValidateClassPathTool.h"

// Asset
#include "Tools/Asset/QueryAssetTool.h"
#include "Tools/Asset/QueryEnumTool.h"
#include "Tools/Asset/QueryStructTool.h"
#include "Tools/Asset/DeleteAssetTool.h"
#include "Tools/Asset/EditCustomizableObjectGraphTool.h"
#include "Tools/Asset/GetAssetDiffTool.h"
#include "Tools/Asset/GetAssetPreviewTool.h"
#include "Tools/Asset/InspectCustomizableObjectGraphTool.h"
#include "Tools/Asset/InspectMutableDiagnosticsTool.h"
#include "Tools/Asset/InspectMutableParametersTool.h"
#include "Tools/Asset/OpenAssetTool.h"
#include "Tools/Asset/ReleaseAssetLockTool.h"

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
#include "Tools/Material/CompileMaterialTool.h"
#include "Tools/Material/QueryMPCTool.h"

// PIE
#include "Tools/PIE/ExecConsoleCommandTool.h"
#include "Tools/PIE/PieSessionTool.h"
#include "Tools/PIE/PieTickTool.h"
#include "Tools/PIE/InspectPawnPossessionTool.h"

// Performance
#include "Tools/Performance/InsightsCaptureTool.h"
#include "Tools/Performance/InsightsListTracesTool.h"
#include "Tools/Performance/InsightsAnalyzeTool.h"

// Rewind
#include "Tools/Rewind/RewindStartTool.h"
#include "Tools/Rewind/RewindStopTool.h"
#include "Tools/Rewind/RewindStatusTool.h"
#include "Tools/Rewind/RewindListTracksTool.h"
#include "Tools/Rewind/RewindOverviewTool.h"
#include "Tools/Rewind/RewindSnapshotTool.h"
#include "Tools/Rewind/RewindSaveTool.h"

// Project
#include "Tools/Project/ProjectInfoTool.h"
#include "Tools/Project/ReloadGameplayTagsTool.h"
#include "Tools/Project/RequestGameplayTagTool.h"

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

// Animation
#include "Tools/Animation/AddAnimStateMachineTool.h"
#include "Tools/Animation/AddAnimStateTool.h"
#include "Tools/Animation/AddAnimTransitionTool.h"

// Widget
#include "Tools/Widget/WidgetBlueprintTool.h"
#include "Tools/Widget/InspectRuntimeWidgetsTool.h"

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
#include "Tools/Write/ModifyInterfaceTool.h"
#include "Tools/Write/SaveAssetTool.h"
#include "Tools/Write/CompileBlueprintTool.h"
#include "Tools/Write/InsertGraphNodeTool.h"
#include "Tools/Write/SetNodePropertyTool.h"
#include "Tools/Write/BatchSpawnActorTool.h"
#include "Tools/Write/BatchModifyActorTool.h"
#include "Tools/Write/BatchDeleteActorTool.h"
#include "Tools/Write/SetViewportCameraTool.h"

DEFINE_LOG_CATEGORY(LogSoftUEBridgeEditor);

void FSoftUEBridgeEditorModule::RegisterAnimationTools()
{
	FBridgeToolRegistry& Registry = FBridgeToolRegistry::Get();

	if (!Registry.HasTool(TEXT("add-anim-state-machine")))
	{
		Registry.RegisterToolClass<UAddAnimStateMachineTool>();
	}
	if (!Registry.HasTool(TEXT("add-anim-state")))
	{
		Registry.RegisterToolClass<UAddAnimStateTool>();
	}
	if (!Registry.HasTool(TEXT("add-anim-transition")))
	{
		Registry.RegisterToolClass<UAddAnimTransitionTool>();
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Registered deferred animation bridge tools; total tools: %d"), Registry.GetToolCount());
}

bool FSoftUEBridgeEditorModule::RegisterAnimationToolsOnTicker(float /*DeltaTime*/)
{
	RegisterAnimationTools();
	DeferredAnimationRegistrationHandle.Reset();
	return false;
}

void FSoftUEBridgeEditorModule::StartupModule()
{
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("SoftUEBridgeEditor module starting up"));

	FBridgeToolRegistry& Registry = FBridgeToolRegistry::Get();

	// Analysis
	Registry.RegisterToolClass<UClassHierarchyTool>();
	Registry.RegisterToolClass<UValidateClassPathTool>();

	// Asset
	Registry.RegisterToolClass<UQueryAssetTool>();
	Registry.RegisterToolClass<UQueryEnumTool>();
	Registry.RegisterToolClass<UQueryStructTool>();
	Registry.RegisterToolClass<UDeleteAssetTool>();
	Registry.RegisterToolClass<UAddCustomizableObjectNodeTool>();
	Registry.RegisterToolClass<USetCustomizableObjectNodePropertyTool>();
	Registry.RegisterToolClass<UConnectCustomizableObjectPinsTool>();
	Registry.RegisterToolClass<URegenerateCustomizableObjectNodePinsTool>();
	Registry.RegisterToolClass<UCompileCustomizableObjectTool>();
	Registry.RegisterToolClass<UCreateCustomizableObjectFromSpecTool>();
	Registry.RegisterToolClass<URemoveCustomizableObjectNodeTool>();
	Registry.RegisterToolClass<UWireCustomizableObjectSlotFromTableTool>();
	Registry.RegisterToolClass<UGetAssetDiffTool>();
	Registry.RegisterToolClass<UGetAssetPreviewTool>();
	Registry.RegisterToolClass<UInspectCustomizableObjectGraphTool>();
	Registry.RegisterToolClass<UInspectMutableDiagnosticsTool>();
	Registry.RegisterToolClass<UInspectMutableParametersTool>();
	Registry.RegisterToolClass<UOpenAssetTool>();
	Registry.RegisterToolClass<UReleaseAssetLockTool>();

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
	Registry.RegisterToolClass<UCompileMaterialTool>();
	Registry.RegisterToolClass<UQueryMPCTool>();

	// PIE
	Registry.RegisterToolClass<UExecConsoleCommandTool>();
	Registry.RegisterToolClass<UPieSessionTool>();
	Registry.RegisterToolClass<UPieTickTool>();
	Registry.RegisterToolClass<UInspectPawnPossessionTool>();

	// Performance
	Registry.RegisterToolClass<UInsightsCaptureTool>();
	Registry.RegisterToolClass<UInsightsListTracesTool>();
	Registry.RegisterToolClass<UInsightsAnalyzeTool>();

	// Rewind
	Registry.RegisterToolClass<URewindStartTool>();
	Registry.RegisterToolClass<URewindStopTool>();
	Registry.RegisterToolClass<URewindStatusTool>();
	Registry.RegisterToolClass<URewindListTracksTool>();
	Registry.RegisterToolClass<URewindOverviewTool>();
	Registry.RegisterToolClass<URewindSnapshotTool>();
	Registry.RegisterToolClass<URewindSaveTool>();

	// Project
	Registry.RegisterToolClass<UProjectInfoTool>();
	Registry.RegisterToolClass<UReloadGameplayTagsTool>();
	Registry.RegisterToolClass<URequestGameplayTagTool>();

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

	// Animation tools are newly added UCLASSes and may not have valid StaticClass()
	// pointers at module startup in freshly rebuilt editor sessions.
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(
		this,
		&FSoftUEBridgeEditorModule::RegisterAnimationTools);
	DeferredAnimationRegistrationHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FSoftUEBridgeEditorModule::RegisterAnimationToolsOnTicker));

	// Widget
	Registry.RegisterToolClass<UWidgetBlueprintTool>();
	Registry.RegisterToolClass<UInspectRuntimeWidgetsTool>();

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
	Registry.RegisterToolClass<UModifyInterfaceTool>();
	Registry.RegisterToolClass<USaveAssetTool>();
	Registry.RegisterToolClass<UCompileBlueprintTool>();
	Registry.RegisterToolClass<UInsertGraphNodeTool>();
	Registry.RegisterToolClass<USetNodePropertyTool>();
	Registry.RegisterToolClass<UBatchSpawnActorTool>();
	Registry.RegisterToolClass<UBatchModifyActorTool>();
	Registry.RegisterToolClass<UBatchDeleteActorTool>();
	Registry.RegisterToolClass<USetViewportCameraTool>();

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Registered %d editor bridge tools"), Registry.GetToolCount());

	FBridgeToolbarExtension::Initialize();
}

void FSoftUEBridgeEditorModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (DeferredAnimationRegistrationHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredAnimationRegistrationHandle);
		DeferredAnimationRegistrationHandle.Reset();
	}

	FBridgeToolbarExtension::Shutdown();
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("SoftUEBridgeEditor module shutting down"));
}

IMPLEMENT_MODULE(FSoftUEBridgeEditorModule, SoftUEBridgeEditor)
