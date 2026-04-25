// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/CreateAssetTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgePropertySerializer.h"
#include "SoftUEBridgeEditorModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/DataTableFactory.h"
#include "Factories/WorldFactory.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "Materials/Material.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimInstance.h"

FString UCreateAssetTool::GetToolDescription() const
{
	return TEXT("Create a new asset by class name. Supports any UObject type including Blueprint, Material, DataTable, DataAsset, etc. "
		"Use full class names (e.g., 'Blueprint', 'Material', 'DataAsset') or Blueprint class paths.");
}

TMap<FString, FBridgeSchemaProperty> UCreateAssetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Full asset path including name (e.g., '/Game/Blueprints/BP_NewActor')");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty AssetClass;
	AssetClass.Type = TEXT("string");
	AssetClass.Description = TEXT("Asset class name. Examples: 'Blueprint', 'Material', 'DataTable', 'DataAsset', 'WidgetBlueprint', 'AnimBlueprint', "
		"or Blueprint class paths like '/Game/MyDataAsset.MyDataAsset_C'");
	AssetClass.bRequired = true;
	Schema.Add(TEXT("asset_class"), AssetClass);

	FBridgeSchemaProperty ParentClass;
	ParentClass.Type = TEXT("string");
	ParentClass.Description = TEXT("Parent class for Blueprints (e.g., 'Actor', 'Character'). For AnimBlueprint, specify skeleton asset path.");
	ParentClass.bRequired = false;
	Schema.Add(TEXT("parent_class"), ParentClass);

	FBridgeSchemaProperty RowStruct;
	RowStruct.Type = TEXT("string");
	RowStruct.Description = TEXT("Row struct path for DataTables");
	RowStruct.bRequired = false;
	Schema.Add(TEXT("row_struct"), RowStruct);

	return Schema;
}

TArray<FString> UCreateAssetTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("asset_class") };
}

FBridgeToolResult UCreateAssetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString AssetClass = GetStringArgOrDefault(Arguments, TEXT("asset_class"));
	FString ParentClass = GetStringArgOrDefault(Arguments, TEXT("parent_class"));
	FString RowStruct = GetStringArgOrDefault(Arguments, TEXT("row_struct"));

	if (AssetPath.IsEmpty() || AssetClass.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and asset_class are required"));
	}

	// Validate path format
	FString ValidateError;
	if (!FBridgeAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FBridgeToolResult::Error(ValidateError);
	}

	// Check if asset already exists
	if (FBridgeAssetModifier::AssetExists(AssetPath))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("create-asset: %s of class %s"), *AssetPath, *AssetClass);

	// Extract package path and asset name
	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName = FPackageName::GetShortName(AssetPath);

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "CreateAsset", "Create {0}"), FText::FromString(AssetPath)));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_class"), AssetClass);

	UObject* CreatedAsset = nullptr;

	// Resolve the asset class
	FString ClassError;
	UClass* ResolvedClass = FBridgePropertySerializer::ResolveClass(AssetClass, ClassError);

	// Special handling for known asset types that need factories
	if (ResolvedClass)
	{
		CreatedAsset = CreateAssetOfClass(ResolvedClass, AssetPath, PackagePath, AssetName, ParentClass, RowStruct, AssetTools, Result, ClassError);
	}
	else
	{
		// Try special names that don't directly map to classes
		CreatedAsset = CreateAssetByName(AssetClass, AssetPath, PackagePath, AssetName, ParentClass, RowStruct, AssetTools, Result, ClassError);
	}

	if (!CreatedAsset)
	{
		return FBridgeToolResult::Error(ClassError.IsEmpty() ? TEXT("Failed to create asset") : ClassError);
	}

	// Mark dirty and register
	FBridgeAssetModifier::MarkPackageDirty(CreatedAsset);
	FAssetRegistryModule::AssetCreated(CreatedAsset);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("created_class"), CreatedAsset->GetClass()->GetName());
	Result->SetBoolField(TEXT("needs_save"), true);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("create-asset: Successfully created %s of class %s"), *AssetPath, *CreatedAsset->GetClass()->GetName());

	return FBridgeToolResult::Json(Result);
}

UObject* UCreateAssetTool::CreateAssetOfClass(
	UClass* AssetClass,
	const FString& AssetPath,
	const FString& PackagePath,
	const FString& AssetName,
	const FString& ParentClass,
	const FString& RowStruct,
	IAssetTools& AssetTools,
	TSharedPtr<FJsonObject>& Result,
	FString& OutError)
{
	// Blueprint
	if (AssetClass->IsChildOf<UBlueprint>() || AssetClass == UBlueprint::StaticClass())
	{
		return CreateBlueprint(PackagePath, AssetName, ParentClass, AssetTools, Result, OutError);
	}

	// AnimBlueprint
	if (AssetClass->IsChildOf<UAnimBlueprint>() || AssetClass == UAnimBlueprint::StaticClass())
	{
		return CreateAnimBlueprint(AssetPath, AssetName, ParentClass, Result, OutError);
	}

	// WidgetBlueprint
	if (AssetClass->IsChildOf<UWidgetBlueprint>() || AssetClass == UWidgetBlueprint::StaticClass())
	{
		return CreateWidgetBlueprint(PackagePath, AssetName, AssetTools, Result, OutError);
	}

	// Material
	if (AssetClass->IsChildOf<UMaterial>() || AssetClass == UMaterial::StaticClass())
	{
		return CreateMaterial(PackagePath, AssetName, AssetTools, OutError);
	}

	// DataTable
	if (AssetClass->IsChildOf<UDataTable>() || AssetClass == UDataTable::StaticClass())
	{
		return CreateDataTable(PackagePath, AssetName, RowStruct, AssetTools, OutError);
	}

	// World/Level
	if (AssetClass->IsChildOf<UWorld>() || AssetClass == UWorld::StaticClass())
	{
		return CreateLevel(PackagePath, AssetName, AssetTools, OutError);
	}

	// DataAsset and subclasses - use direct instantiation
	if (AssetClass->IsChildOf<UDataAsset>())
	{
		return CreateDataAsset(AssetPath, AssetName, AssetClass, OutError);
	}

	// Generic UObject - try to find a factory or use direct instantiation
	return CreateGenericAsset(AssetPath, AssetName, AssetClass, AssetTools, OutError);
}

UObject* UCreateAssetTool::CreateAssetByName(
	const FString& AssetClassName,
	const FString& AssetPath,
	const FString& PackagePath,
	const FString& AssetName,
	const FString& ParentClass,
	const FString& RowStruct,
	IAssetTools& AssetTools,
	TSharedPtr<FJsonObject>& Result,
	FString& OutError)
{
	// Handle string names that don't directly map to class names
	FString LowerName = AssetClassName.ToLower();

	if (LowerName == TEXT("blueprint"))
	{
		return CreateBlueprint(PackagePath, AssetName, ParentClass, AssetTools, Result, OutError);
	}

	if (LowerName == TEXT("material"))
	{
		return CreateMaterial(PackagePath, AssetName, AssetTools, OutError);
	}

	if (LowerName == TEXT("datatable"))
	{
		return CreateDataTable(PackagePath, AssetName, RowStruct, AssetTools, OutError);
	}

	if (LowerName == TEXT("level") || LowerName == TEXT("map") || LowerName == TEXT("world"))
	{
		return CreateLevel(PackagePath, AssetName, AssetTools, OutError);
	}

	if (LowerName == TEXT("widgetblueprint") || LowerName == TEXT("widget") || LowerName == TEXT("userwidget"))
	{
		return CreateWidgetBlueprint(PackagePath, AssetName, AssetTools, Result, OutError);
	}

	if (LowerName == TEXT("animblueprint") || LowerName == TEXT("animbp"))
	{
		return CreateAnimBlueprint(AssetPath, AssetName, ParentClass, Result, OutError);
	}

	if (LowerName == TEXT("dataasset"))
	{
		return CreateDataAsset(AssetPath, AssetName, UDataAsset::StaticClass(), OutError);
	}

	OutError = FString::Printf(TEXT("Unknown asset class: %s. Use class names like 'Blueprint', 'Material', 'DataAsset', or full class paths."), *AssetClassName);
	return nullptr;
}

UObject* UCreateAssetTool::CreateBlueprint(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& ParentClassName,
	IAssetTools& AssetTools,
	TSharedPtr<FJsonObject>& Result,
	FString& OutError)
{
	// Resolve parent class
	UClass* ParentUClass = AActor::StaticClass(); // Default

	if (!ParentClassName.IsEmpty())
	{
		FString ClassError;
		UClass* ResolvedParent = FBridgePropertySerializer::ResolveClass(ParentClassName, ClassError);
		if (ResolvedParent)
		{
			ParentUClass = ResolvedParent;
		}
	}

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentUClass;

	UObject* CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);

	if (CreatedAsset)
	{
		Result->SetStringField(TEXT("parent_class"), ParentUClass->GetName());
	}
	else
	{
		OutError = TEXT("Failed to create Blueprint");
	}

	return CreatedAsset;
}

UObject* UCreateAssetTool::CreateMaterial(
	const FString& PackagePath,
	const FString& AssetName,
	IAssetTools& AssetTools,
	FString& OutError)
{
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UObject* CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);

	if (!CreatedAsset)
	{
		OutError = TEXT("Failed to create Material");
	}

	return CreatedAsset;
}

UObject* UCreateAssetTool::CreateDataTable(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& RowStruct,
	IAssetTools& AssetTools,
	FString& OutError)
{
	UDataTableFactory* Factory = NewObject<UDataTableFactory>();

	if (!RowStruct.IsEmpty())
	{
		UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*RowStruct, EFindFirstObjectOptions::ExactClass);
		if (Struct)
		{
			Factory->Struct = Struct;
		}
	}

	UObject* CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UDataTable::StaticClass(), Factory);

	if (!CreatedAsset)
	{
		OutError = TEXT("Failed to create DataTable");
	}

	return CreatedAsset;
}

UObject* UCreateAssetTool::CreateLevel(
	const FString& PackagePath,
	const FString& AssetName,
	IAssetTools& AssetTools,
	FString& OutError)
{
	UWorldFactory* Factory = NewObject<UWorldFactory>();
	Factory->WorldType = EWorldType::Editor;

	UObject* CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UWorld::StaticClass(), Factory);

	if (!CreatedAsset)
	{
		OutError = TEXT("Failed to create Level");
	}

	return CreatedAsset;
}

UObject* UCreateAssetTool::CreateWidgetBlueprint(
	const FString& PackagePath,
	const FString& AssetName,
	IAssetTools& AssetTools,
	TSharedPtr<FJsonObject>& Result,
	FString& OutError)
{
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = UUserWidget::StaticClass();

	UObject* CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);

	if (CreatedAsset)
	{
		Result->SetStringField(TEXT("parent_class"), TEXT("UserWidget"));
	}
	else
	{
		OutError = TEXT("Failed to create WidgetBlueprint");
	}

	return CreatedAsset;
}

UObject* UCreateAssetTool::CreateAnimBlueprint(
	const FString& AssetPath,
	const FString& AssetName,
	const FString& SkeletonPath,
	TSharedPtr<FJsonObject>& Result,
	FString& OutError)
{
	USkeleton* TargetSkeleton = nullptr;

	// Try to load skeleton from path
	if (!SkeletonPath.IsEmpty())
	{
		TargetSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	}

	// If no skeleton specified, try to find any skeleton in the project
	if (!TargetSkeleton)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> SkeletonAssets;
		AssetRegistryModule.Get().GetAssetsByClass(USkeleton::StaticClass()->GetClassPathName(), SkeletonAssets);

		if (SkeletonAssets.Num() > 0)
		{
			TargetSkeleton = Cast<USkeleton>(SkeletonAssets[0].GetAsset());
		}
	}

	if (!TargetSkeleton)
	{
		OutError = TEXT("AnimBlueprint requires a skeleton. No skeleton found. Specify skeleton path in parent_class parameter.");
		return nullptr;
	}

	UObject* CreatedAsset = FKismetEditorUtilities::CreateBlueprint(
		UAnimInstance::StaticClass(),
		CreatePackage(*AssetPath),
		*AssetName,
		BPTYPE_Normal,
		UAnimBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());

	if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(CreatedAsset))
	{
		AnimBP->TargetSkeleton = TargetSkeleton;
		Result->SetStringField(TEXT("skeleton"), TargetSkeleton->GetPathName());
	}
	else
	{
		OutError = TEXT("Failed to create AnimBlueprint");
	}

	return CreatedAsset;
}

UObject* UCreateAssetTool::CreateDataAsset(
	const FString& AssetPath,
	const FString& AssetName,
	UClass* DataAssetClass,
	FString& OutError)
{
	// Direct instantiation for DataAsset and subclasses
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		OutError = TEXT("Failed to create package");
		return nullptr;
	}

	UDataAsset* NewAsset = NewObject<UDataAsset>(Package, DataAssetClass, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		OutError = FString::Printf(TEXT("Failed to create DataAsset of class %s"), *DataAssetClass->GetName());
		return nullptr;
	}

	return NewAsset;
}

UObject* UCreateAssetTool::CreateGenericAsset(
	const FString& AssetPath,
	const FString& AssetName,
	UClass* AssetClass,
	IAssetTools& AssetTools,
	FString& OutError)
{
	// Try to find a factory for this class
	TArray<UFactory*> Factories = AssetTools.GetNewAssetFactories();
	UFactory* FoundFactory = nullptr;

	for (UFactory* Factory : Factories)
	{
		if (Factory->SupportedClass == AssetClass ||
			(Factory->SupportedClass && AssetClass->IsChildOf(Factory->SupportedClass)))
		{
			FoundFactory = Factory;
			break;
		}
	}

	if (FoundFactory)
	{
		FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		return AssetTools.CreateAsset(AssetName, PackagePath, AssetClass, FoundFactory);
	}

	// Fallback to direct instantiation
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		OutError = TEXT("Failed to create package");
		return nullptr;
	}

	UObject* NewAsset = NewObject<UObject>(Package, AssetClass, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		OutError = FString::Printf(TEXT("Failed to create asset of class %s"), *AssetClass->GetName());
		return nullptr;
	}

	return NewAsset;
}
