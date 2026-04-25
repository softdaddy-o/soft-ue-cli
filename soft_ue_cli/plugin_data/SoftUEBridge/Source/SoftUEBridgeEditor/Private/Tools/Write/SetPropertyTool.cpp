// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/EditorSetPropertyTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

FString UEditorSetPropertyTool::GetToolDescription() const
{
	return TEXT("Set any property on any asset using UE reflection. Supports nested paths (e.g., 'Stats.MaxHealth'), "
		"array indices (e.g., 'Items[0]'), TArray, TMap (as JSON object), TSet (as JSON array), "
		"object references (as asset paths), structs, and vectors/rotators/colors. "
		"For Blueprint components, use 'component_name' to target a specific component's properties. "
		"Use 'clear_override' to revert a component property to its default value.");
}

TMap<FString, FBridgeSchemaProperty> UEditorSetPropertyTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path (e.g., '/Game/Blueprints/BP_Player')");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty PropertyPath;
	PropertyPath.Type = TEXT("string");
	PropertyPath.Description = TEXT("Dot-separated property path (e.g., 'Health', 'Stats.MaxHealth', 'Items[0].Value')");
	PropertyPath.bRequired = true;
	Schema.Add(TEXT("property_path"), PropertyPath);

	FBridgeSchemaProperty Value;
	Value.Type = TEXT("any");
	Value.Description = TEXT("Value to set (number, string, boolean, array, or object depending on property type). Not required if clear_override is true.");
	Value.bRequired = false;
	Schema.Add(TEXT("value"), Value);

	FBridgeSchemaProperty ComponentName;
	ComponentName.Type = TEXT("string");
	ComponentName.Description = TEXT("Name of the component to target (for Blueprint component overrides). If specified, property_path is relative to the component.");
	ComponentName.bRequired = false;
	Schema.Add(TEXT("component_name"), ComponentName);

	FBridgeSchemaProperty ClearOverride;
	ClearOverride.Type = TEXT("boolean");
	ClearOverride.Description = TEXT("If true, clears the override and reverts to the default value (component class default for SCS, parent value for inherited). Mutually exclusive with 'value'.");
	ClearOverride.bRequired = false;
	Schema.Add(TEXT("clear_override"), ClearOverride);

	return Schema;
}

TArray<FString> UEditorSetPropertyTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("property_path") };
}

FBridgeToolResult UEditorSetPropertyTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	// Get parameters
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString PropertyPath = GetStringArgOrDefault(Arguments, TEXT("property_path"));
	FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"), TEXT(""));
	bool bClearOverride = GetBoolArgOrDefault(Arguments, TEXT("clear_override"), false);

	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	if (PropertyPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("property_path is required"));
	}

	// Get the value (can be any JSON type) - not required if clear_override is true
	TSharedPtr<FJsonValue> Value = Arguments->TryGetField(TEXT("value"));
	if (!Value.IsValid() && !bClearOverride)
	{
		return FBridgeToolResult::Error(TEXT("Either 'value' or 'clear_override' must be provided"));
	}

	if (Value.IsValid() && bClearOverride)
	{
		return FBridgeToolResult::Error(TEXT("Cannot specify both 'value' and 'clear_override'"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-property: %s.%s (component=%s, clear=%s)"),
		*AssetPath, *PropertyPath, *ComponentName, bClearOverride ? TEXT("true") : TEXT("false"));

	// Load the asset
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// For Blueprints, we need to handle component targeting
	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	UObject* TargetObject = Object;
	UActorComponent* ComponentTemplate = nullptr;
	UActorComponent* DefaultComponent = nullptr;  // For clear_override comparison
	bool bIsInheritedComponent = false;

	if (Blueprint)
	{
		if (!Blueprint->GeneratedClass)
		{
			return FBridgeToolResult::Error(TEXT("Blueprint has no generated class - compile it first"));
		}

		if (!ComponentName.IsEmpty())
		{
			// Component targeting mode - find the component template
			bool bFoundComponent = false;

			// Try SCS first (components added in this Blueprint)
			if (Blueprint->SimpleConstructionScript)
			{
				TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
				for (USCS_Node* Node : AllNodes)
				{
					if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString() == ComponentName)
					{
						ComponentTemplate = Node->ComponentTemplate;
						TargetObject = ComponentTemplate;
						// For SCS components, the default is the component class CDO
						DefaultComponent = ComponentTemplate->GetClass()->GetDefaultObject<UActorComponent>();
						bFoundComponent = true;
						bIsInheritedComponent = false;
						break;
					}
				}
			}

			// Try InheritableComponentHandler (inherited component overrides)
			if (!bFoundComponent)
			{
				UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(true);
				if (ICH)
				{
					TArray<UActorComponent*> OverrideTemplates;
					ICH->GetAllTemplates(OverrideTemplates);

					for (UActorComponent* ExistingTemplate : OverrideTemplates)
					{
						if (!ExistingTemplate)
						{
							continue;
						}
						FString KeyName = ExistingTemplate->GetName();

						if (KeyName == ComponentName || (ExistingTemplate && ExistingTemplate->GetName() == ComponentName))
						{
							ComponentTemplate = ExistingTemplate;
							TargetObject = ComponentTemplate;
							bIsInheritedComponent = true;
							bFoundComponent = true;

							// For inherited components, find the parent component for comparison
							UClass* ParentClass = Blueprint->ParentClass;
							if (ParentClass)
							{
								if (AActor* ParentCDO = Cast<AActor>(ParentClass->GetDefaultObject()))
								{
									TArray<UActorComponent*> ParentComponents;
									ParentCDO->GetComponents(ParentComponents);
									for (UActorComponent* Comp : ParentComponents)
									{
										if (Comp && Comp->GetName() == ComponentTemplate->GetName())
										{
											DefaultComponent = Comp;
											break;
										}
									}
								}
							}
							break;
						}
					}
				}

				// If not found in ICH, try to create an override for an inherited component
				if (!bFoundComponent)
				{
					// Look for the component in parent class CDO
					UClass* ParentClass = Blueprint->ParentClass;
					if (ParentClass)
					{
						if (AActor* ParentCDO = Cast<AActor>(ParentClass->GetDefaultObject()))
						{
							TArray<UActorComponent*> ParentComponents;
							ParentCDO->GetComponents(ParentComponents);
							for (UActorComponent* Comp : ParentComponents)
							{
								if (Comp && Comp->GetName() == ComponentName)
								{
									// Found in parent - we need to create an override
									// This is a complex operation that requires the InheritableComponentHandler
									UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-property: Component '%s' found in parent, creating override"), *ComponentName);

									UInheritableComponentHandler* ICH2 = Blueprint->GetInheritableComponentHandler(true);
									if (ICH2)
									{
										// We need to find the correct FComponentKey for this component
										// This is tricky as the component may be from an SCS node in a parent Blueprint
										// For now, return an error indicating manual override creation is needed
										return FBridgeToolResult::Error(FString::Printf(
											TEXT("Component '%s' exists in parent class but has no override yet. "
												"Please manually override the property in the editor first, then use this tool to modify it."),
											*ComponentName));
									}
								}
							}
						}
					}
				}
			}

			if (!bFoundComponent)
			{
				return FBridgeToolResult::Error(FString::Printf(TEXT("Component '%s' not found in Blueprint"), *ComponentName));
			}
		}
		else
		{
			// No component specified - target the CDO
			TargetObject = Blueprint->GeneratedClass->GetDefaultObject();
			if (!TargetObject)
			{
				return FBridgeToolResult::Error(TEXT("Failed to get Blueprint CDO"));
			}
		}
	}

	// Find the property
	FProperty* Property = nullptr;
	void* Container = nullptr;
	FString FindError;

	if (!FBridgeAssetModifier::FindPropertyByPath(TargetObject, PropertyPath, Property, Container, FindError))
	{
		return FBridgeToolResult::Error(FindError);
	}

	// Begin transaction for undo support
	FString TransactionDesc = bClearOverride
		? FString::Printf(TEXT("Clear override %s.%s"), *AssetPath, *PropertyPath)
		: FString::Printf(TEXT("Set %s.%s"), *AssetPath, *PropertyPath);

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TransactionDesc));

	// Mark objects as modified
	FBridgeAssetModifier::MarkModified(TargetObject);
	if (Blueprint)
	{
		FBridgeAssetModifier::MarkModified(Blueprint);
	}

	if (bClearOverride)
	{
		// Clear the override - copy default value to the template
		if (!DefaultComponent)
		{
			// Fall back to class CDO
			DefaultComponent = TargetObject->GetClass()->GetDefaultObject<UActorComponent>();
		}

		if (DefaultComponent)
		{
			void* DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultComponent);
			void* TargetValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

			if (DefaultValuePtr && TargetValuePtr)
			{
				Property->CopyCompleteValue(TargetValuePtr, DefaultValuePtr);
				UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-property: Cleared override for %s.%s"), *AssetPath, *PropertyPath);
			}
			else
			{
				return FBridgeToolResult::Error(TEXT("Failed to get property value pointers for clear operation"));
			}
		}
		else
		{
			return FBridgeToolResult::Error(TEXT("Cannot determine default value for clear_override - no default component available"));
		}
	}
	else
	{
		// Set the property value
		FString SetError;
		if (!FBridgeAssetModifier::SetPropertyFromJson(Property, Container, Value, SetError))
		{
			return FBridgeToolResult::Error(SetError);
		}
	}

	// Mark package as dirty
	FBridgeAssetModifier::MarkPackageDirty(Object);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("property"), PropertyPath);

	if (!ComponentName.IsEmpty())
	{
		Result->SetStringField(TEXT("component"), ComponentName);
		Result->SetBoolField(TEXT("is_inherited_component"), bIsInheritedComponent);
	}

	if (bClearOverride)
	{
		Result->SetBoolField(TEXT("override_cleared"), true);
	}

	Result->SetBoolField(TEXT("needs_save"), true);

	if (Blueprint)
	{
		Result->SetBoolField(TEXT("needs_compile"), true);
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-property: Successfully %s %s.%s%s"),
		bClearOverride ? TEXT("cleared") : TEXT("set"),
		*AssetPath, *PropertyPath,
		ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (component: %s)"), *ComponentName));

	return FBridgeToolResult::Json(Result);
}
