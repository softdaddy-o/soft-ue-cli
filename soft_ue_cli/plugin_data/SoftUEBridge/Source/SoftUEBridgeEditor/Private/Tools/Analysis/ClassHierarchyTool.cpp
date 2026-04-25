// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Analysis/ClassHierarchyTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Tools/BridgeToolResult.h"
#include "SoftUEBridgeEditorModule.h"

FString UClassHierarchyTool::GetToolDescription() const
{
	return TEXT("Browse class inheritance tree showing parent and child classes. "\
		"Supports both C++ and Blueprint classes.");
}

TMap<FString, FBridgeSchemaProperty> UClassHierarchyTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty ClassName;
	ClassName.Type = TEXT("string");
	ClassName.Description = TEXT("Class name to inspect (e.g., 'AActor', 'UActorComponent', 'BP_MyBlueprint')");
	ClassName.bRequired = true;
	Schema.Add(TEXT("class_name"), ClassName);

	FBridgeSchemaProperty Direction;
	Direction.Type = TEXT("string");
	Direction.Description = TEXT("Direction: 'parents', 'children', or 'both' (default: 'both')");
	Direction.bRequired = false;
	Schema.Add(TEXT("direction"), Direction);

	FBridgeSchemaProperty IncludeBlueprints;
	IncludeBlueprints.Type = TEXT("boolean");
	IncludeBlueprints.Description = TEXT("Include Blueprint subclasses when showing children (default: true)");
	IncludeBlueprints.bRequired = false;
	Schema.Add(TEXT("include_blueprints"), IncludeBlueprints);

	FBridgeSchemaProperty Depth;
	Depth.Type = TEXT("integer");
	Depth.Description = TEXT("Maximum inheritance depth to traverse (default: 10)");
	Depth.bRequired = false;
	Schema.Add(TEXT("depth"), Depth);

	return Schema;
}

TArray<FString> UClassHierarchyTool::GetRequiredParams() const
{
	return { TEXT("class_name") };
}

FBridgeToolResult UClassHierarchyTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	// Get class name
	FString ClassName;
	if (!GetStringArg(Arguments, TEXT("class_name"), ClassName))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: class_name"));
	}

	// Get optional parameters
	FString Direction = GetStringArgOrDefault(Arguments, TEXT("direction"), TEXT("both")).ToLower();
	bool bIncludeBlueprints = GetBoolArgOrDefault(Arguments, TEXT("include_blueprints"), true);
	int32 MaxDepth = GetIntArgOrDefault(Arguments, TEXT("depth"), 10);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("get-class-hierarchy: class='%s', direction='%s', depth=%d"),
		*ClassName, *Direction, MaxDepth);

	// Validate direction
	if (Direction != TEXT("parents") && Direction != TEXT("children") && Direction != TEXT("both"))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Invalid direction '%s'. Must be 'parents', 'children', or 'both'"), *Direction));
	}

	// Find class
	UClass* Class = FindClassByName(ClassName);
	if (!Class)
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("get-class-hierarchy: Class '%s' not found"), *ClassName);
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Class '%s' not found"), *ClassName));
	}

	// Build hierarchy
	TSharedPtr<FJsonObject> Result = BuildHierarchyJson(Class, Direction, bIncludeBlueprints, MaxDepth);

	return FBridgeToolResult::Json(Result);
}

UClass* UClassHierarchyTool::FindClassByName(const FString& ClassName) const
{
	// Try finding as a C++ class using FindFirstObject (more reliable)
	UClass* Class = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (Class)
	{
		return Class;
	}

	// Try with "U" prefix for UObject classes
	if (!ClassName.StartsWith(TEXT("U")) && !ClassName.StartsWith(TEXT("A")) && !ClassName.StartsWith(TEXT("F")))
	{
		Class = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::ExactClass);
		if (Class)
		{
			return Class;
		}

		// Try with "A" prefix for Actor classes
		Class = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::ExactClass);
		if (Class)
		{
			return Class;
		}
	}

	// Search for Blueprint class by asset name (includes WidgetBlueprint, AnimBlueprint, etc.)
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Use FARFilter with bRecursiveClasses to find ALL Blueprint-derived types
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (AssetData.AssetName.ToString().Equals(ClassName, ESearchCase::IgnoreCase))
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
			if (Blueprint && Blueprint->GeneratedClass)
			{
				return Blueprint->GeneratedClass;
			}
		}
	}

	return nullptr;
}

TArray<UClass*> UClassHierarchyTool::GetParentClasses(UClass* Class, int32 MaxDepth) const
{
	TArray<UClass*> Parents;

	if (!Class)
	{
		return Parents;
	}

	UClass* CurrentClass = Class->GetSuperClass();
	int32 Depth = 0;

	while (CurrentClass && CurrentClass != UObject::StaticClass() && Depth < MaxDepth)
	{
		Parents.Add(CurrentClass);
		CurrentClass = CurrentClass->GetSuperClass();
		Depth++;
	}

	// Add UObject if we reached it
	if (CurrentClass == UObject::StaticClass())
	{
		Parents.Add(CurrentClass);
	}

	return Parents;
}

TArray<UClass*> UClassHierarchyTool::GetChildClasses(UClass* Class, bool bIncludeBlueprints, int32 MaxDepth) const
{
	TArray<UClass*> Children;

	if (!Class)
	{
		return Children;
	}

	// Get all classes derived from this class
	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(Class, DerivedClasses, false);

	// Filter by depth (only direct children for now, full depth hierarchy is built recursively)
	for (UClass* DerivedClass : DerivedClasses)
	{
		if (DerivedClass->GetSuperClass() == Class)
		{
			// Check if it's a Blueprint class
			bool bIsBlueprint = DerivedClass->IsChildOf(UBlueprintGeneratedClass::StaticClass());

			if (bIncludeBlueprints || !bIsBlueprint)
			{
				Children.Add(DerivedClass);
			}
		}
	}

	return Children;
}

TSharedPtr<FJsonObject> UClassHierarchyTool::ClassToJson(UClass* Class) const
{
	if (!Class)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ClassJson = MakeShareable(new FJsonObject);

	ClassJson->SetStringField(TEXT("name"), Class->GetName());
	ClassJson->SetStringField(TEXT("path_name"), Class->GetPathName());

	// Check if Blueprint class
	bool bIsBlueprint = Class->IsChildOf(UBlueprintGeneratedClass::StaticClass());
	ClassJson->SetBoolField(TEXT("is_blueprint"), bIsBlueprint);

	if (bIsBlueprint)
	{
		ClassJson->SetStringField(TEXT("type"), TEXT("Blueprint"));
	}
	else if (Class->HasAnyClassFlags(CLASS_Native))
	{
		ClassJson->SetStringField(TEXT("type"), TEXT("C++"));
	}
	else
	{
		ClassJson->SetStringField(TEXT("type"), TEXT("Unknown"));
	}

	// Class flags
	ClassJson->SetBoolField(TEXT("is_abstract"), Class->HasAnyClassFlags(CLASS_Abstract));
	ClassJson->SetBoolField(TEXT("is_deprecated"), Class->HasAnyClassFlags(CLASS_Deprecated));

	return ClassJson;
}

TSharedPtr<FJsonObject> UClassHierarchyTool::BuildHierarchyJson(UClass* Class, const FString& Direction, bool bIncludeBlueprints, int32 MaxDepth) const
{
	if (!Class)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Current class info
	Result->SetObjectField(TEXT("class"), ClassToJson(Class));

	// Parent classes
	if (Direction == TEXT("parents") || Direction == TEXT("both"))
	{
		TArray<UClass*> Parents = GetParentClasses(Class, MaxDepth);
		TArray<TSharedPtr<FJsonValue>> ParentsArray;

		for (UClass* Parent : Parents)
		{
			TSharedPtr<FJsonObject> ParentJson = ClassToJson(Parent);
			if (ParentJson.IsValid())
			{
				ParentsArray.Add(MakeShareable(new FJsonValueObject(ParentJson)));
			}
		}

		Result->SetArrayField(TEXT("parents"), ParentsArray);
		Result->SetNumberField(TEXT("parent_count"), ParentsArray.Num());
	}

	// Child classes
	if (Direction == TEXT("children") || Direction == TEXT("both"))
	{
		TArray<UClass*> Children = GetChildClasses(Class, bIncludeBlueprints, 1); // Direct children only
		TArray<TSharedPtr<FJsonValue>> ChildrenArray;

		for (UClass* Child : Children)
		{
			TSharedPtr<FJsonObject> ChildJson = ClassToJson(Child);
			if (ChildJson.IsValid())
			{
				// Recursively get children if depth allows
				if (MaxDepth > 1)
				{
					TArray<UClass*> GrandChildren = GetChildClasses(Child, bIncludeBlueprints, 1);
					if (GrandChildren.Num() > 0)
					{
						ChildJson->SetNumberField(TEXT("child_count"), GrandChildren.Num());
					}
				}

				ChildrenArray.Add(MakeShareable(new FJsonValueObject(ChildJson)));
			}
		}

		Result->SetArrayField(TEXT("children"), ChildrenArray);
		Result->SetNumberField(TEXT("child_count"), ChildrenArray.Num());
	}

	return Result;
}
