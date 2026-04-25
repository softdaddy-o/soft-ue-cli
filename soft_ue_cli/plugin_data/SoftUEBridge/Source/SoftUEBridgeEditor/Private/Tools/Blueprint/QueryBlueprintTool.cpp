// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Blueprint/QueryBlueprintTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "EdGraph/EdGraph.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

FString UQueryBlueprintTool::GetToolDescription() const
{
	return TEXT("Query Blueprint structure: functions, variables, components, defaults, event graph, and component overrides. "
		"Use 'include' parameter to select sections: 'functions', 'variables', 'components', 'defaults', 'graph', 'component_overrides', 'all'.");
}

TMap<FString, FBridgeSchemaProperty> UQueryBlueprintTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Blueprint (e.g., /Game/Blueprints/BP_Character)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Include;
	Include.Type = TEXT("string");
	Include.Description = TEXT("Sections to include: 'functions', 'variables', 'components', 'defaults', 'graph', 'component_overrides', or 'all' (default: 'all')");
	Include.bRequired = false;
	Schema.Add(TEXT("include"), Include);

	FBridgeSchemaProperty Detailed;
	Detailed.Type = TEXT("boolean");
	Detailed.Description = TEXT("Include detailed info (flags, metadata, parameters) for each item (default: true)");
	Detailed.bRequired = false;
	Schema.Add(TEXT("detailed"), Detailed);

	// Defaults-specific options
	FBridgeSchemaProperty PropertyFilter;
	PropertyFilter.Type = TEXT("string");
	PropertyFilter.Description = TEXT("Filter properties by name (wildcards supported, e.g., '*Health*'). For defaults section.");
	PropertyFilter.bRequired = false;
	Schema.Add(TEXT("property_filter"), PropertyFilter);

	FBridgeSchemaProperty CategoryFilter;
	CategoryFilter.Type = TEXT("string");
	CategoryFilter.Description = TEXT("Filter properties by category (e.g., 'Stats'). For defaults section.");
	CategoryFilter.bRequired = false;
	Schema.Add(TEXT("category_filter"), CategoryFilter);

	FBridgeSchemaProperty IncludeInherited;
	IncludeInherited.Type = TEXT("boolean");
	IncludeInherited.Description = TEXT("Include inherited properties from parent classes (default: false). For defaults section.");
	IncludeInherited.bRequired = false;
	Schema.Add(TEXT("include_inherited"), IncludeInherited);

	// Component overrides specific options
	FBridgeSchemaProperty ComponentFilter;
	ComponentFilter.Type = TEXT("string");
	ComponentFilter.Description = TEXT("Filter components by name (wildcards supported, e.g., '*Mesh*'). For component_overrides section.");
	ComponentFilter.bRequired = false;
	Schema.Add(TEXT("component_filter"), ComponentFilter);

	FBridgeSchemaProperty IncludeNonOverridden;
	IncludeNonOverridden.Type = TEXT("boolean");
	IncludeNonOverridden.Description = TEXT("Include all properties, not just overridden ones (default: false). For component_overrides section.");
	IncludeNonOverridden.bRequired = false;
	Schema.Add(TEXT("include_non_overridden"), IncludeNonOverridden);

	FBridgeSchemaProperty Search;
	Search.Type = TEXT("string");
	Search.Description = TEXT("Filter functions, variables, and components by name (wildcards supported: *pattern*)");
	Search.bRequired = false;
	Schema.Add(TEXT("search"), Search);

	return Schema;
}

TArray<FString> UQueryBlueprintTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UQueryBlueprintTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	FString Include = GetStringArgOrDefault(Arguments, TEXT("include"), TEXT("all")).ToLower();
	bool bDetailed = GetBoolArgOrDefault(Arguments, TEXT("detailed"), true);

	// Defaults-specific options
	FString PropertyFilter = GetStringArgOrDefault(Arguments, TEXT("property_filter"), TEXT(""));
	FString CategoryFilter = GetStringArgOrDefault(Arguments, TEXT("category_filter"), TEXT(""));
	bool bIncludeInherited = GetBoolArgOrDefault(Arguments, TEXT("include_inherited"), false);

	// Component overrides specific options
	FString ComponentFilter = GetStringArgOrDefault(Arguments, TEXT("component_filter"), TEXT(""));
	bool bIncludeNonOverridden = GetBoolArgOrDefault(Arguments, TEXT("include_non_overridden"), false);

	// General search filter
	FString SearchFilter = GetStringArgOrDefault(Arguments, TEXT("search"), TEXT(""));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("query-blueprint: path='%s', include='%s', detailed=%s, search='%s'"),
		*AssetPath, *Include, bDetailed ? TEXT("true") : TEXT("false"), *SearchFilter);

	// Load the blueprint
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("query-blueprint: Failed to load Blueprint at '%s'"), *AssetPath);
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load Blueprint: %s"), *AssetPath));
	}

	// Build result JSON
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("name"), Blueprint->GetName());
	Result->SetStringField(TEXT("path"), AssetPath);

	// Parent class info
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
		Result->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass->GetPathName());
	}
	else
	{
		Result->SetStringField(TEXT("parent_class"), TEXT("None"));
	}

	// Blueprint type
	FString BlueprintTypeStr;
	switch (Blueprint->BlueprintType)
	{
	case BPTYPE_Normal: BlueprintTypeStr = TEXT("Normal"); break;
	case BPTYPE_Const: BlueprintTypeStr = TEXT("Const"); break;
	case BPTYPE_MacroLibrary: BlueprintTypeStr = TEXT("MacroLibrary"); break;
	case BPTYPE_Interface: BlueprintTypeStr = TEXT("Interface"); break;
	case BPTYPE_LevelScript: BlueprintTypeStr = TEXT("LevelScript"); break;
	case BPTYPE_FunctionLibrary: BlueprintTypeStr = TEXT("FunctionLibrary"); break;
	default: BlueprintTypeStr = TEXT("Unknown");
	}
	Result->SetStringField(TEXT("blueprint_type"), BlueprintTypeStr);

	// Add requested sections
	bool bAll = Include == TEXT("all");

	if (bAll || Include == TEXT("functions"))
	{
		Result->SetObjectField(TEXT("functions"), ExtractFunctions(Blueprint, bDetailed, SearchFilter));
	}

	if (bAll || Include == TEXT("variables"))
	{
		Result->SetObjectField(TEXT("variables"), ExtractVariables(Blueprint, bDetailed, SearchFilter));
	}

	if (bAll || Include == TEXT("components"))
	{
		Result->SetObjectField(TEXT("components"), ExtractComponents(Blueprint, bDetailed, SearchFilter));
	}

	if (bAll || Include == TEXT("graph"))
	{
		Result->SetObjectField(TEXT("event_graph"), ExtractEventGraphSummary(Blueprint));
	}

	if (bAll || Include == TEXT("defaults"))
	{
		Result->SetObjectField(TEXT("defaults"), ExtractDefaults(Blueprint, bIncludeInherited, CategoryFilter, PropertyFilter));
	}

	if (Include == TEXT("component_overrides"))
	{
		// Note: component_overrides is NOT included in 'all' to avoid noise - must be explicitly requested
		Result->SetObjectField(TEXT("component_overrides"), ExtractComponentOverrides(Blueprint, ComponentFilter, PropertyFilter, bIncludeNonOverridden));
	}

	return FBridgeToolResult::Json(Result);
}

TSharedPtr<FJsonObject> UQueryBlueprintTool::ExtractFunctions(UBlueprint* Blueprint, bool bDetailed, const FString& SearchFilter) const
{
	TSharedPtr<FJsonObject> FunctionsObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> FunctionArray;

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		// Apply search filter on function name
		if (!SearchFilter.IsEmpty() && !MatchesWildcard(Graph->GetName(), SearchFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> FuncObj = MakeShareable(new FJsonObject);
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());

		if (bDetailed)
		{
			// Find function entry node for parameters
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
				{
					// Get function flags
					TArray<TSharedPtr<FJsonValue>> FlagsArray;
					if (EntryNode->GetFunctionFlags() & FUNC_Public) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("Public"))));
					if (EntryNode->GetFunctionFlags() & FUNC_Protected) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("Protected"))));
					if (EntryNode->GetFunctionFlags() & FUNC_Private) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("Private"))));
					if (EntryNode->GetFunctionFlags() & FUNC_Static) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("Static"))));
					if (EntryNode->GetFunctionFlags() & FUNC_BlueprintPure) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("Pure"))));
					if (EntryNode->GetFunctionFlags() & FUNC_BlueprintCallable) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("BlueprintCallable"))));
					FuncObj->SetArrayField(TEXT("flags"), FlagsArray);

					// Get parameters from pins
					TArray<TSharedPtr<FJsonValue>> ParamsArray;
					for (UEdGraphPin* Pin : EntryNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Output && !Pin->PinName.IsEqual(UEdGraphSchema_K2::PN_Then))
						{
							TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
							ParamObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
							ParamObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
							if (Pin->PinType.PinSubCategoryObject.IsValid())
							{
								ParamObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
							}
							ParamsArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
						}
					}
					FuncObj->SetArrayField(TEXT("parameters"), ParamsArray);
					break;
				}
			}
		}

		FunctionArray.Add(MakeShareable(new FJsonValueObject(FuncObj)));
	}

	FunctionsObj->SetArrayField(TEXT("items"), FunctionArray);
	FunctionsObj->SetNumberField(TEXT("count"), FunctionArray.Num());

	return FunctionsObj;
}

TSharedPtr<FJsonObject> UQueryBlueprintTool::ExtractVariables(UBlueprint* Blueprint, bool bDetailed, const FString& SearchFilter) const
{
	TSharedPtr<FJsonObject> VariablesObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> VarArray;

	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		// Apply search filter on variable name
		if (!SearchFilter.IsEmpty() && !MatchesWildcard(Var.VarName.ToString(), SearchFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject);
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());

		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject->GetName());
		}

		VarObj->SetBoolField(TEXT("is_array"), Var.VarType.IsArray());
		VarObj->SetBoolField(TEXT("is_set"), Var.VarType.IsSet());
		VarObj->SetBoolField(TEXT("is_map"), Var.VarType.IsMap());

		if (bDetailed)
		{
			// Replication
			if (Var.RepNotifyFunc != NAME_None)
			{
				VarObj->SetStringField(TEXT("replication"), TEXT("ReplicatedUsing"));
				VarObj->SetStringField(TEXT("rep_notify_func"), Var.RepNotifyFunc.ToString());
			}
			else if (Var.PropertyFlags & CPF_Net)
			{
				VarObj->SetStringField(TEXT("replication"), TEXT("Replicated"));
			}
			else
			{
				VarObj->SetStringField(TEXT("replication"), TEXT("None"));
			}

			// Category
			if (!Var.Category.IsEmpty())
			{
				VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
			}

			// Flags
			TArray<TSharedPtr<FJsonValue>> FlagsArray;
			if (Var.PropertyFlags & CPF_Edit) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("EditAnywhere"))));
			if (Var.PropertyFlags & CPF_BlueprintVisible) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("BlueprintReadWrite"))));
			if (Var.PropertyFlags & CPF_ExposeOnSpawn) FlagsArray.Add(MakeShareable(new FJsonValueString(TEXT("ExposeOnSpawn"))));
			VarObj->SetArrayField(TEXT("flags"), FlagsArray);
		}

		VarArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
	}

	VariablesObj->SetArrayField(TEXT("items"), VarArray);
	VariablesObj->SetNumberField(TEXT("count"), VarArray.Num());

	return VariablesObj;
}

TSharedPtr<FJsonObject> UQueryBlueprintTool::ExtractComponents(UBlueprint* Blueprint, bool bDetailed, const FString& SearchFilter) const
{
	TSharedPtr<FJsonObject> ComponentsObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> CompArray;

	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();

		for (USCS_Node* Node : AllNodes)
		{
			if (!Node || !Node->ComponentTemplate)
			{
				continue;
			}

			// Apply search filter on component name
			if (!SearchFilter.IsEmpty() && !MatchesWildcard(Node->GetVariableName().ToString(), SearchFilter))
			{
				continue;
			}

			TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject);
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("Unknown"));

			if (bDetailed)
			{
				// Parent component
				if (Node->ParentComponentOrVariableName != NAME_None)
				{
					CompObj->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
				}

				// Is root
				if (Blueprint->SimpleConstructionScript->GetRootNodes().Contains(Node))
				{
					CompObj->SetBoolField(TEXT("is_root"), true);
				}
			}

			CompArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
		}
	}

	ComponentsObj->SetArrayField(TEXT("items"), CompArray);
	ComponentsObj->SetNumberField(TEXT("count"), CompArray.Num());

	return ComponentsObj;
}

TSharedPtr<FJsonObject> UQueryBlueprintTool::ExtractEventGraphSummary(UBlueprint* Blueprint) const
{
	TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> EventsArray;
	TArray<TSharedPtr<FJsonValue>> CustomEventsArray;

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;

			// Standard events (BeginPlay, Tick, etc.)
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				if (!Cast<UK2Node_CustomEvent>(EventNode))
				{
					TSharedPtr<FJsonObject> EventObj = MakeShareable(new FJsonObject);
					EventObj->SetStringField(TEXT("name"), EventNode->GetFunctionName().ToString());
					EventObj->SetStringField(TEXT("node_title"), EventNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
					EventsArray.Add(MakeShareable(new FJsonValueObject(EventObj)));
				}
			}

			// Custom events
			if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
			{
				TSharedPtr<FJsonObject> EventObj = MakeShareable(new FJsonObject);
				EventObj->SetStringField(TEXT("name"), CustomEventNode->CustomFunctionName.ToString());

				// Get parameters
				TArray<TSharedPtr<FJsonValue>> ParamsArray;
				for (UEdGraphPin* Pin : CustomEventNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output &&
						!Pin->PinName.IsEqual(UEdGraphSchema_K2::PN_Then) &&
						Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
						ParamObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						ParamObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
						ParamsArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
					}
				}
				EventObj->SetArrayField(TEXT("parameters"), ParamsArray);

				CustomEventsArray.Add(MakeShareable(new FJsonValueObject(EventObj)));
			}
		}
	}

	GraphObj->SetArrayField(TEXT("events"), EventsArray);
	GraphObj->SetArrayField(TEXT("custom_events"), CustomEventsArray);
	GraphObj->SetNumberField(TEXT("event_count"), EventsArray.Num());
	GraphObj->SetNumberField(TEXT("custom_event_count"), CustomEventsArray.Num());

	return GraphObj;
}

TSharedPtr<FJsonObject> UQueryBlueprintTool::ExtractDefaults(UBlueprint* Blueprint, bool bIncludeInherited, const FString& CategoryFilter, const FString& PropertyFilter) const
{
	TSharedPtr<FJsonObject> DefaultsObj = MakeShareable(new FJsonObject);

	// Get generated class
	UBlueprintGeneratedClass* GenClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	if (!GenClass)
	{
		DefaultsObj->SetStringField(TEXT("error"), TEXT("Blueprint has no generated class"));
		return DefaultsObj;
	}

	// Get CDO
	UObject* CDO = GenClass->GetDefaultObject();
	if (!CDO)
	{
		DefaultsObj->SetStringField(TEXT("error"), TEXT("Failed to get CDO"));
		return DefaultsObj;
	}

	DefaultsObj->SetStringField(TEXT("generated_class"), GenClass->GetName());

	// Iterate properties
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;
	EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeInherited
		? EFieldIteratorFlags::IncludeSuper
		: EFieldIteratorFlags::ExcludeSuper;

	for (TFieldIterator<FProperty> PropIt(GenClass, SuperFlags); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property)
		{
			continue;
		}

		// Apply property name filter
		if (!PropertyFilter.IsEmpty())
		{
			FString PropName = Property->GetName();
			if (!PropName.Contains(PropertyFilter.Replace(TEXT("*"), TEXT(""))))
			{
				continue;
			}
		}

		// Apply category filter
		if (!CategoryFilter.IsEmpty())
		{
			FString Category = Property->GetMetaData(TEXT("Category"));
			if (Category.IsEmpty() || !Category.Contains(CategoryFilter))
			{
				continue;
			}
		}

		// Get property value from CDO
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
		if (!ValuePtr)
		{
			continue;
		}

		TSharedPtr<FJsonObject> PropertyJson = PropertyToJson(Property, ValuePtr, CDO);
		if (PropertyJson.IsValid())
		{
			PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropertyJson)));
		}
	}

	DefaultsObj->SetArrayField(TEXT("properties"), PropertiesArray);
	DefaultsObj->SetNumberField(TEXT("property_count"), PropertiesArray.Num());

	return DefaultsObj;
}

TSharedPtr<FJsonObject> UQueryBlueprintTool::PropertyToJson(FProperty* Property, void* Container, UObject* Owner) const
{
	if (!Property || !Container)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropertyJson = MakeShareable(new FJsonObject);

	// Basic info
	PropertyJson->SetStringField(TEXT("name"), Property->GetName());
	PropertyJson->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

	// Category
	FString Category = Property->GetMetaData(TEXT("Category"));
	if (!Category.IsEmpty())
	{
		PropertyJson->SetStringField(TEXT("category"), Category);
	}

	// Export default value as string
	FString DefaultValue;
	Property->ExportText_Direct(DefaultValue, Container, Container, Owner, PPF_None);
	PropertyJson->SetStringField(TEXT("default_value"), DefaultValue);

	return PropertyJson;
}

FString UQueryBlueprintTool::GetPropertyTypeString(FProperty* Property) const
{
	if (!Property)
	{
		return TEXT("unknown");
	}

	if (Property->IsA<FBoolProperty>()) return TEXT("bool");
	if (Property->IsA<FIntProperty>()) return TEXT("int32");
	if (Property->IsA<FFloatProperty>()) return TEXT("float");
	if (Property->IsA<FNameProperty>()) return TEXT("FName");
	if (Property->IsA<FStrProperty>()) return TEXT("FString");
	if (Property->IsA<FTextProperty>()) return TEXT("FText");

	if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property))
	{
		if (ObjectProp->PropertyClass)
		{
			return FString::Printf(TEXT("TObjectPtr<%s>"), *ObjectProp->PropertyClass->GetName());
		}
		return TEXT("TObjectPtr<UObject>");
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct)
		{
			return StructProp->Struct->GetName();
		}
		return TEXT("struct");
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FString InnerType = GetPropertyTypeString(ArrayProp->Inner);
		return FString::Printf(TEXT("TArray<%s>"), *InnerType);
	}

	return Property->GetClass()->GetName();
}

TSharedPtr<FJsonObject> UQueryBlueprintTool::ExtractComponentOverrides(UBlueprint* Blueprint, const FString& ComponentFilter, const FString& PropertyFilter, bool bIncludeNonOverridden) const
{
	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;

	// Helper lambda to check if property name matches filter
	auto MatchesPropertyFilter = [&PropertyFilter](const FString& PropName) -> bool
	{
		if (PropertyFilter.IsEmpty())
		{
			return true;
		}
		FString FilterPattern = PropertyFilter.Replace(TEXT("*"), TEXT(""));
		return PropName.Contains(FilterPattern);
	};

	// Helper lambda to check if component name matches filter
	auto MatchesComponentFilter = [&ComponentFilter](const FString& CompName) -> bool
	{
		if (ComponentFilter.IsEmpty())
		{
			return true;
		}
		FString FilterPattern = ComponentFilter.Replace(TEXT("*"), TEXT(""));
		return CompName.Contains(FilterPattern);
	};

	// Helper lambda to extract overridden properties from a component template
	auto ExtractOverriddenProperties = [this, &MatchesPropertyFilter, bIncludeNonOverridden](
		UActorComponent* ComponentTemplate,
		UActorComponent* DefaultComponent) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> PropertiesArray;

		if (!ComponentTemplate)
		{
			return PropertiesArray;
		}

		UClass* ComponentClass = ComponentTemplate->GetClass();

		for (TFieldIterator<FProperty> PropIt(ComponentClass); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (!Property)
			{
				continue;
			}

			// Skip non-editable properties
			if (!(Property->PropertyFlags & CPF_Edit))
			{
				continue;
			}

			FString PropName = Property->GetName();
			if (!MatchesPropertyFilter(PropName))
			{
				continue;
			}

			void* TemplateValuePtr = Property->ContainerPtrToValuePtr<void>(ComponentTemplate);
			if (!TemplateValuePtr)
			{
				continue;
			}

			// Check if property is overridden (different from default)
			bool bIsOverridden = false;
			FString DefaultValueStr;

			if (DefaultComponent)
			{
				void* DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultComponent);
				if (DefaultValuePtr)
				{
					bIsOverridden = !Property->Identical(TemplateValuePtr, DefaultValuePtr);
					Property->ExportText_Direct(DefaultValueStr, DefaultValuePtr, DefaultValuePtr, DefaultComponent, PPF_None);
				}
			}

			// Skip non-overridden properties unless explicitly requested
			if (!bIsOverridden && !bIncludeNonOverridden)
			{
				continue;
			}

			TSharedPtr<FJsonObject> PropObj = MakeShareable(new FJsonObject);
			PropObj->SetStringField(TEXT("name"), PropName);
			PropObj->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

			// Current value
			FString CurrentValueStr;
			Property->ExportText_Direct(CurrentValueStr, TemplateValuePtr, TemplateValuePtr, ComponentTemplate, PPF_None);
			PropObj->SetStringField(TEXT("value"), CurrentValueStr);

			// Default value (if available)
			if (!DefaultValueStr.IsEmpty())
			{
				PropObj->SetStringField(TEXT("default_value"), DefaultValueStr);
			}

			PropObj->SetBoolField(TEXT("is_overridden"), bIsOverridden);

			// Category
			FString Category = Property->GetMetaData(TEXT("Category"));
			if (!Category.IsEmpty())
			{
				PropObj->SetStringField(TEXT("category"), Category);
			}

			PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropObj)));
		}

		return PropertiesArray;
	};

	// 1. Process SCS components (components added in this Blueprint)
	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();

		for (USCS_Node* Node : AllNodes)
		{
			if (!Node || !Node->ComponentTemplate)
			{
				continue;
			}

			FString ComponentName = Node->GetVariableName().ToString();
			if (!MatchesComponentFilter(ComponentName))
			{
				continue;
			}

			UActorComponent* Template = Node->ComponentTemplate;
			UClass* ComponentClass = Template->GetClass();

			// Get class default object for comparison
			UActorComponent* ComponentCDO = ComponentClass->GetDefaultObject<UActorComponent>();

			TArray<TSharedPtr<FJsonValue>> OverriddenProperties = ExtractOverriddenProperties(Template, ComponentCDO);

			// Only add if there are overrides or we're including all
			if (OverriddenProperties.Num() > 0 || bIncludeNonOverridden)
			{
				TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject);
				CompObj->SetStringField(TEXT("name"), ComponentName);
				CompObj->SetStringField(TEXT("class"), ComponentClass->GetName());
				CompObj->SetBoolField(TEXT("is_inherited"), false);
				CompObj->SetArrayField(TEXT("overrides"), OverriddenProperties);
				CompObj->SetNumberField(TEXT("override_count"), OverriddenProperties.Num());

				ComponentsArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
			}
		}
	}

	// 2. Process inherited component overrides via InheritableComponentHandler
	UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(false);
	if (ICH)
	{
		TArray<UActorComponent*> OverrideTemplates;
		ICH->GetAllTemplates(OverrideTemplates);

		for (UActorComponent* OverrideTemplate : OverrideTemplates)
		{
			if (!OverrideTemplate)
			{
				continue;
			}

			FString ComponentName = OverrideTemplate->GetName();

			if (!MatchesComponentFilter(ComponentName))
			{
				continue;
			}

			// Find the parent component for comparison
			UActorComponent* ParentComponent = nullptr;
			FString ParentBlueprintPath;

			// Get parent class to find the original component
			UClass* ParentClass = Blueprint->ParentClass;
			if (ParentClass)
			{
				if (AActor* ParentCDO = Cast<AActor>(ParentClass->GetDefaultObject()))
				{
					// Find component by name in parent
					TArray<UActorComponent*> ParentComponents;
					ParentCDO->GetComponents(ParentComponents);
					for (UActorComponent* Comp : ParentComponents)
					{
						if (Comp && Comp->GetName() == OverrideTemplate->GetName())
						{
							ParentComponent = Comp;
							break;
						}
					}
				}

				// Get parent Blueprint path if it's a Blueprint class
				if (UBlueprintGeneratedClass* ParentBPClass = Cast<UBlueprintGeneratedClass>(ParentClass))
				{
					if (UBlueprint* ParentBP = Cast<UBlueprint>(ParentBPClass->ClassGeneratedBy))
					{
						ParentBlueprintPath = ParentBP->GetPathName();
					}
				}
			}

			TArray<TSharedPtr<FJsonValue>> OverriddenProperties = ExtractOverriddenProperties(OverrideTemplate, ParentComponent);

			// Only add if there are overrides or we're including all
			if (OverriddenProperties.Num() > 0 || bIncludeNonOverridden)
			{
				TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject);
				CompObj->SetStringField(TEXT("name"), ComponentName);
				CompObj->SetStringField(TEXT("class"), OverrideTemplate->GetClass()->GetName());
				CompObj->SetBoolField(TEXT("is_inherited"), true);

				if (!ParentBlueprintPath.IsEmpty())
				{
					CompObj->SetStringField(TEXT("parent_blueprint"), ParentBlueprintPath);
				}

				CompObj->SetArrayField(TEXT("overrides"), OverriddenProperties);
				CompObj->SetNumberField(TEXT("override_count"), OverriddenProperties.Num());

				ComponentsArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
			}
		}
	}

	ResultObj->SetArrayField(TEXT("components"), ComponentsArray);
	ResultObj->SetNumberField(TEXT("count"), ComponentsArray.Num());

	return ResultObj;
}
