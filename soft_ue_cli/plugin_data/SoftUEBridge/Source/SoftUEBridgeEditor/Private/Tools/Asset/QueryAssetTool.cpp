// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/QueryAssetTool.h"
#include "Tools/Asset/AssetIntrospectionUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "LandscapeGrassType.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "Tools/BridgeToolResult.h"
#include "SoftUEBridgeEditorModule.h"

FString UQueryAssetTool::GetToolDescription() const
{
	return TEXT("Query assets: search by pattern/class/path, or inspect a specific asset. "
		"Use 'query' for search mode, 'asset_path' for inspect mode. World assets include "
		"WorldSettings helpers such as DefaultGameMode.");
}

TMap<FString, FBridgeSchemaProperty> UQueryAssetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	// Search mode parameters
	FBridgeSchemaProperty Query;
	Query.Type = TEXT("string");
	Query.Description = TEXT("Search query (asset name pattern, supports * and ? wildcards). Triggers search mode.");
	Query.bRequired = false;
	Schema.Add(TEXT("query"), Query);

	FBridgeSchemaProperty ClassFilter;
	ClassFilter.Type = TEXT("string");
	ClassFilter.Description = TEXT("Filter by asset class (e.g., Blueprint, StaticMesh, Material)");
	ClassFilter.bRequired = false;
	Schema.Add(TEXT("class"), ClassFilter);

	FBridgeSchemaProperty PathFilter;
	PathFilter.Type = TEXT("string");
	PathFilter.Description = TEXT("Filter by path prefix (e.g., /Game/Blueprints)");
	PathFilter.bRequired = false;
	Schema.Add(TEXT("path"), PathFilter);

	FBridgeSchemaProperty Limit;
	Limit.Type = TEXT("integer");
	Limit.Description = TEXT("Maximum results for search (default: 100)");
	Limit.bRequired = false;
	Schema.Add(TEXT("limit"), Limit);

	// Inspect mode parameters
	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to inspect (e.g., /Game/Data/DT_Items). Triggers inspect mode.");
	AssetPath.bRequired = false;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Depth;
	Depth.Type = TEXT("integer");
	Depth.Description = TEXT("Recursion depth for nested objects (default: 2, max: 5). For inspect mode.");
	Depth.bRequired = false;
	Schema.Add(TEXT("depth"), Depth);

	FBridgeSchemaProperty IncludeDefaults;
	IncludeDefaults.Type = TEXT("boolean");
	IncludeDefaults.Description = TEXT("Include properties with default/empty values (default: false)");
	IncludeDefaults.bRequired = false;
	Schema.Add(TEXT("include_defaults"), IncludeDefaults);

	FBridgeSchemaProperty PropertyFilter;
	PropertyFilter.Type = TEXT("string");
	PropertyFilter.Description = TEXT("Filter properties by name (wildcards supported)");
	PropertyFilter.bRequired = false;
	Schema.Add(TEXT("property_filter"), PropertyFilter);

	FBridgeSchemaProperty CategoryFilter;
	CategoryFilter.Type = TEXT("string");
	CategoryFilter.Description = TEXT("Filter by UPROPERTY category");
	CategoryFilter.bRequired = false;
	Schema.Add(TEXT("category_filter"), CategoryFilter);

	FBridgeSchemaProperty RowFilter;
	RowFilter.Type = TEXT("string");
	RowFilter.Description = TEXT("Filter DataTable rows by name (wildcards supported)");
	RowFilter.bRequired = false;
	Schema.Add(TEXT("row_filter"), RowFilter);

	FBridgeSchemaProperty Search;
	Search.Type = TEXT("string");
	Search.Description = TEXT("General search filter (wildcards supported). Applies to properties and DataTable rows. Alternative to property_filter/row_filter.");
	Search.bRequired = false;
	Schema.Add(TEXT("search"), Search);

	return Schema;
}

TArray<FString> UQueryAssetTool::GetRequiredParams() const
{
	return {}; // Either query or asset_path must be provided
}

FBridgeToolResult UQueryAssetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString Query = GetStringArgOrDefault(Arguments, TEXT("query"), TEXT(""));
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));

	// Determine mode
	if (!AssetPath.IsEmpty())
	{
		// Inspect mode
		int32 MaxDepth = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("depth"), 2), 1, 5);
		bool bIncludeDefaults = GetBoolArgOrDefault(Arguments, TEXT("include_defaults"), false);
		FString PropertyFilter = GetStringArgOrDefault(Arguments, TEXT("property_filter"), TEXT(""));
		FString CategoryFilter = GetStringArgOrDefault(Arguments, TEXT("category_filter"), TEXT(""));
		FString RowFilter = GetStringArgOrDefault(Arguments, TEXT("row_filter"), TEXT(""));
		FString SearchFilter = GetStringArgOrDefault(Arguments, TEXT("search"), TEXT(""));

		// Use search as fallback for property_filter and row_filter
		FString EffectivePropertyFilter = PropertyFilter.IsEmpty() ? SearchFilter : PropertyFilter;
		FString EffectiveRowFilter = RowFilter.IsEmpty() ? SearchFilter : RowFilter;

		return InspectAsset(AssetPath, MaxDepth, bIncludeDefaults, EffectivePropertyFilter, CategoryFilter, EffectiveRowFilter);
	}

	// Search mode
	FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class"), TEXT(""));
	FString PathFilter = GetStringArgOrDefault(Arguments, TEXT("path"), TEXT(""));
	int32 Limit = GetIntArgOrDefault(Arguments, TEXT("limit"), 100);

	if (Query.IsEmpty() && ClassFilter.IsEmpty() && PathFilter.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Provide 'asset_path' to inspect, or 'query'/'class'/'path' to search"));
	}

	return SearchAssets(Query, ClassFilter, PathFilter, Limit);
}

// === Search mode ===

FBridgeToolResult UQueryAssetTool::SearchAssets(const FString& Query, const FString& ClassFilter,
	const FString& PathFilter, int32 Limit) const
{
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("query-asset search: query='%s', class='%s', path='%s'"),
		*Query, *ClassFilter, *PathFilter);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;

	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
		Filter.bRecursivePaths = true;
	}

	if (!ClassFilter.IsEmpty())
	{
		UClass* FilterClass = FindFirstObjectSafe<UClass>(*ClassFilter);
		if (!FilterClass)
		{
			FilterClass = FindFirstObjectSafe<UClass>(*(TEXT("U") + ClassFilter));
		}
		if (!FilterClass)
		{
			FilterClass = FindFirstObjectSafe<UClass>(*(TEXT("A") + ClassFilter));
		}

		if (FilterClass)
		{
			Filter.ClassPaths.Add(FilterClass->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	// Apply name filter if specified
	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	int32 Count = 0;

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (Count >= Limit) break;

		// Apply name filter
		if (!Query.IsEmpty())
		{
			FString AssetName = AssetData.AssetName.ToString();
			if (!MatchesWildcard(AssetName, Query))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> AssetJson = MakeShareable(new FJsonObject);
		AssetJson->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		AssetJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		AssetJson->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

		AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));
		Count++;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());
	Result->SetNumberField(TEXT("total_matching"), AssetDataList.Num());
	Result->SetBoolField(TEXT("limit_reached"), Count >= Limit);

	return FBridgeToolResult::Json(Result);
}

// === Inspect mode ===

FBridgeToolResult UQueryAssetTool::InspectAsset(const FString& AssetPath, int32 MaxDepth,
	bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter,
	const FString& RowFilter) const
{
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("query-asset inspect: path='%s'"), *AssetPath);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	UObject* AssetObject = AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
	if (!AssetObject)
	{
		AssetObject = LoadObject<UObject>(nullptr, *AssetPath);
	}

	UDataTable* DataTable = Cast<UDataTable>(AssetObject);
	if (DataTable)
	{
		TSharedPtr<FJsonObject> Result = InspectDataTable(DataTable, RowFilter);
		return FBridgeToolResult::Json(Result);
	}

	if (UWorld* World = Cast<UWorld>(AssetObject))
	{
		return FBridgeToolResult::Json(InspectWorldAsset(World, MaxDepth, bIncludeDefaults, PropertyFilter, CategoryFilter));
	}

	if (UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(AssetObject))
	{
		return FBridgeToolResult::Json(AssetIntrospectionUtils::InspectUserDefinedEnum(UserEnum, AssetPath));
	}

	if (UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(AssetObject))
	{
		return FBridgeToolResult::Json(AssetIntrospectionUtils::InspectUserDefinedStruct(UserStruct, AssetPath));
	}

	if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetObject))
	{
		if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UDataAsset::StaticClass()))
		{
			return FBridgeToolResult::Json(
				InspectDataAssetBlueprint(Blueprint, MaxDepth, bIncludeDefaults, PropertyFilter, CategoryFilter));
		}
	}

	UDataAsset* DataAsset = Cast<UDataAsset>(AssetObject);
	if (DataAsset)
	{
		TSharedPtr<FJsonObject> Result = InspectDataAsset(DataAsset);
		return FBridgeToolResult::Json(Result);
	}

	ULandscapeGrassType* GrassType = Cast<ULandscapeGrassType>(AssetObject);
	if (GrassType)
	{
		return FBridgeToolResult::Json(InspectGrassType(GrassType));
	}

	if (AssetObject)
	{
		TSharedPtr<FJsonObject> Result = InspectObject(AssetObject, MaxDepth, bIncludeDefaults, PropertyFilter, CategoryFilter);
		return FBridgeToolResult::Json(Result);
	}

	if (AssetData.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to load asset '%s' (class '%s')"),
			*AssetPath,
			*AssetData.AssetClassPath.GetAssetName().ToString()));
	}

	return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectDataTable(UDataTable* DataTable, const FString& RowFilter) const
{
	if (!DataTable)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("DataTable"));
	Result->SetStringField(TEXT("name"), DataTable->GetName());
	Result->SetStringField(TEXT("row_struct"), DataTable->GetRowStructPathName().ToString());

	TArray<TSharedPtr<FJsonValue>> RowsArray;
	TArray<FName> RowNames = DataTable->GetRowNames();

	for (const FName& RowName : RowNames)
	{
		if (!RowFilter.IsEmpty() && !MatchesWildcard(RowName.ToString(), RowFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> RowJson = MakeShareable(new FJsonObject);
		RowJson->SetStringField(TEXT("name"), RowName.ToString());

		// Export row data as string
		FString RowData;
		if (const uint8* RowPtr = DataTable->FindRowUnchecked(RowName))
		{
			if (const UScriptStruct* RowStruct = DataTable->GetRowStruct())
			{
				RowStruct->ExportText(RowData, RowPtr, nullptr, nullptr, PPF_None, nullptr);
				RowJson->SetStringField(TEXT("data"), RowData);
			}
		}

		RowsArray.Add(MakeShareable(new FJsonValueObject(RowJson)));
	}

	Result->SetArrayField(TEXT("rows"), RowsArray);
	Result->SetNumberField(TEXT("row_count"), RowsArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectWorldAsset(UWorld* World, int32 MaxDepth,
	bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter) const
{
	if (!World)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = InspectObject(World, MaxDepth, bIncludeDefaults, PropertyFilter, CategoryFilter);
	Result->SetStringField(TEXT("type"), TEXT("World"));

	if (const ULevel* PersistentLevel = World->PersistentLevel)
	{
		Result->SetStringField(TEXT("persistent_level"), PersistentLevel->GetPathName());
	}

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	if (!WorldSettings && World->PersistentLevel)
	{
		WorldSettings = World->PersistentLevel->GetWorldSettings();
	}

	if (WorldSettings)
	{
		const FString DefaultGameModePath = GetPathNameSafe(WorldSettings->DefaultGameMode);

		TSharedPtr<FJsonObject> Settings = MakeShareable(new FJsonObject);
		Settings->SetStringField(TEXT("path"), WorldSettings->GetPathName());
		Settings->SetStringField(TEXT("class"), WorldSettings->GetClass()->GetPathName());
		Settings->SetStringField(TEXT("default_game_mode"), DefaultGameModePath);
		Settings->SetBoolField(TEXT("has_default_game_mode_override"), WorldSettings->DefaultGameMode != nullptr);

		Result->SetObjectField(TEXT("world_settings"), Settings);
		Result->SetStringField(TEXT("default_game_mode"), DefaultGameModePath);
	}

	return Result;
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectDataAsset(UDataAsset* DataAsset) const
{
	if (!DataAsset)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("DataAsset"));
	Result->SetStringField(TEXT("name"), DataAsset->GetName());
	Result->SetStringField(TEXT("class"), DataAsset->GetClass()->GetName());

	// Get properties
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;

	for (TFieldIterator<FProperty> PropIt(DataAsset->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(DataAsset);
		if (!ValuePtr) continue;

		TSharedPtr<FJsonObject> PropJson = PropertyToJson(Property, ValuePtr, DataAsset, 0, 2, false);
		if (PropJson.IsValid())
		{
			PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropJson)));
		}
	}

	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("property_count"), PropertiesArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectDataAssetBlueprint(UBlueprint* Blueprint, int32 MaxDepth,
	bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter) const
{
	if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(UDataAsset::StaticClass()))
	{
		return nullptr;
	}

	UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
	if (!DefaultObject)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = InspectObject(DefaultObject, MaxDepth, bIncludeDefaults, PropertyFilter, CategoryFilter);
	Result->SetStringField(TEXT("type"), TEXT("DataAssetBlueprint"));
	Result->SetStringField(TEXT("asset_name"), Blueprint->GetName());
	Result->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("generated_class"), Blueprint->GeneratedClass->GetName());
	return Result;
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectObject(UObject* Object, int32 MaxDepth,
	bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter) const
{
	if (!Object)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("UObject"));
	Result->SetStringField(TEXT("name"), Object->GetName());
	Result->SetStringField(TEXT("class"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("path"), Object->GetPathName());

	// Class hierarchy
	TArray<TSharedPtr<FJsonValue>> HierarchyArray;
	for (UClass* Class = Object->GetClass(); Class; Class = Class->GetSuperClass())
	{
		HierarchyArray.Add(MakeShareable(new FJsonValueString(Class->GetName())));
	}
	Result->SetArrayField(TEXT("class_hierarchy"), HierarchyArray);

	// Properties
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;

	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		// Apply filters
		if (!PropertyFilter.IsEmpty() && !MatchesWildcard(Property->GetName(), PropertyFilter))
		{
			continue;
		}

		if (!CategoryFilter.IsEmpty())
		{
			FString Category = Property->GetMetaData(TEXT("Category"));
			if (!MatchesWildcard(Category, CategoryFilter))
			{
				continue;
			}
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		if (!ValuePtr) continue;

		TSharedPtr<FJsonObject> PropJson = PropertyToJson(Property, ValuePtr, Object, 0, MaxDepth, bIncludeDefaults);
		if (PropJson.IsValid())
		{
			PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropJson)));
		}
	}

	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("property_count"), PropertiesArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectGrassType(ULandscapeGrassType* GrassType) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("LandscapeGrassType"));
	Result->SetStringField(TEXT("name"), GrassType->GetName());
	Result->SetStringField(TEXT("path"), GrassType->GetPathName());

	TArray<TSharedPtr<FJsonValue>> VarietiesArray;
	for (const FGrassVariety& Variety : GrassType->GrassVarieties)
	{
		TSharedPtr<FJsonObject> VarJson = MakeShareable(new FJsonObject);

		// Mesh
		VarJson->SetStringField(TEXT("grass_mesh"), Variety.GrassMesh ? Variety.GrassMesh->GetPathName() : TEXT("None"));

		// Density and placement
		VarJson->SetNumberField(TEXT("grass_density"), Variety.GrassDensity.Default);
		VarJson->SetBoolField(TEXT("use_grid"), Variety.bUseGrid);
		VarJson->SetNumberField(TEXT("placement_jitter"), Variety.PlacementJitter);

		// Cull distances
		VarJson->SetNumberField(TEXT("start_cull_distance"), Variety.StartCullDistance.Default);
		VarJson->SetNumberField(TEXT("end_cull_distance"), Variety.EndCullDistance.Default);

		// Instance settings
		VarJson->SetNumberField(TEXT("min_lod"), Variety.MinLOD);

		// Scaling mode
		FString ScalingStr;
		switch (Variety.Scaling)
		{
		case EGrassScaling::Uniform: ScalingStr = TEXT("Uniform"); break;
		case EGrassScaling::Free:    ScalingStr = TEXT("Free");    break;
		case EGrassScaling::LockXY:  ScalingStr = TEXT("LockXY"); break;
		default:                     ScalingStr = TEXT("Unknown"); break;
		}
		VarJson->SetStringField(TEXT("scaling"), ScalingStr);
		VarJson->SetNumberField(TEXT("scale_x_min"), Variety.ScaleX.Min);
		VarJson->SetNumberField(TEXT("scale_x_max"), Variety.ScaleX.Max);
		VarJson->SetNumberField(TEXT("scale_y_min"), Variety.ScaleY.Min);
		VarJson->SetNumberField(TEXT("scale_y_max"), Variety.ScaleY.Max);
		VarJson->SetNumberField(TEXT("scale_z_min"), Variety.ScaleZ.Min);
		VarJson->SetNumberField(TEXT("scale_z_max"), Variety.ScaleZ.Max);

		VarietiesArray.Add(MakeShareable(new FJsonValueObject(VarJson)));
	}

	Result->SetArrayField(TEXT("grass_varieties"), VarietiesArray);
	Result->SetNumberField(TEXT("variety_count"), VarietiesArray.Num());

	return Result;
}

// === Helpers ===

TSharedPtr<FJsonObject> UQueryAssetTool::PropertyToJson(FProperty* Property, void* Container,
	UObject* Owner, int32 CurrentDepth, int32 MaxDepth, bool bIncludeDefaults) const
{
	if (!Property || !Container)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropJson = MakeShareable(new FJsonObject);
	PropJson->SetStringField(TEXT("name"), Property->GetName());
	PropJson->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

	FString Category = Property->GetMetaData(TEXT("Category"));
	if (!Category.IsEmpty())
	{
		PropJson->SetStringField(TEXT("category"), Category);
	}

	// Export value
	FString Value;
	Property->ExportText_Direct(Value, Container, Container, Owner, PPF_None);

	if (!bIncludeDefaults && Value.IsEmpty())
	{
		return nullptr;
	}

	PropJson->SetStringField(TEXT("value"), Value);

	return PropJson;
}

FString UQueryAssetTool::GetPropertyTypeString(FProperty* Property) const
{
	if (!Property) return TEXT("unknown");

	if (Property->IsA<FBoolProperty>()) return TEXT("bool");
	if (Property->IsA<FIntProperty>()) return TEXT("int32");
	if (Property->IsA<FFloatProperty>()) return TEXT("float");
	if (Property->IsA<FStrProperty>()) return TEXT("FString");
	if (Property->IsA<FNameProperty>()) return TEXT("FName");
	if (Property->IsA<FTextProperty>()) return TEXT("FText");

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return StructProp->Struct ? StructProp->Struct->GetName() : TEXT("struct");
	}

	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		return ObjProp->PropertyClass ? FString::Printf(TEXT("TObjectPtr<%s>"), *ObjProp->PropertyClass->GetName()) : TEXT("UObject*");
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("TArray<%s>"), *GetPropertyTypeString(ArrayProp->Inner));
	}

	return Property->GetClass()->GetName();
}
