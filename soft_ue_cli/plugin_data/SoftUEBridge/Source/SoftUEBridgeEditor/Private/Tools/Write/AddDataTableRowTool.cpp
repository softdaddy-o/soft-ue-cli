// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/AddDataTableRowTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/DataTable.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

FString UAddDataTableRowTool::GetToolDescription() const
{
	return TEXT("Add a new row to a DataTable.");
}

TMap<FString, FBridgeSchemaProperty> UAddDataTableRowTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("DataTable asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty RowName;
	RowName.Type = TEXT("string");
	RowName.Description = TEXT("Name for the new row");
	RowName.bRequired = true;
	Schema.Add(TEXT("row_name"), RowName);

	FBridgeSchemaProperty RowData;
	RowData.Type = TEXT("string");
	RowData.Description = TEXT("Row data as JSON string with property names matching the row struct. Example: {\"Name\":\"Value\",\"Count\":5}");
	RowData.bRequired = false;
	Schema.Add(TEXT("row_data"), RowData);

	return Schema;
}

TArray<FString> UAddDataTableRowTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("row_name") };
}

FBridgeToolResult UAddDataTableRowTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString RowName = GetStringArgOrDefault(Arguments, TEXT("row_name"));
	FString RowDataString = GetStringArgOrDefault(Arguments, TEXT("row_data"));

	// Parse row_data JSON string if provided
	TSharedPtr<FJsonObject> RowData = nullptr;
	if (!RowDataString.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RowDataString);
		if (!FJsonSerializer::Deserialize(Reader, RowData) || !RowData.IsValid())
		{
			return FBridgeToolResult::Error(TEXT("row_data must be valid JSON string"));
		}
	}

	if (AssetPath.IsEmpty() || RowName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and row_name are required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-datatable-row: %s to %s"), *RowName, *AssetPath);

	// Load the DataTable
	FString LoadError;
	UDataTable* DataTable = FBridgeAssetModifier::LoadAssetByPath<UDataTable>(AssetPath, LoadError);
	if (!DataTable)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Check if row already exists
	if (DataTable->FindRowUnchecked(FName(*RowName)))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Row already exists: %s"), *RowName));
	}

	// Get the row struct
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return FBridgeToolResult::Error(TEXT("DataTable has no row struct"));
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "AddRow", "Add row {0} to {1}"),
			FText::FromString(RowName), FText::FromString(AssetPath)));

	FBridgeAssetModifier::MarkModified(DataTable);

	// Create a new row with default values
	uint8* RowMemory = (uint8*)FMemory::Malloc(RowStruct->GetStructureSize());
	RowStruct->InitializeStruct(RowMemory);

	// If row data is provided, try to populate it
	if (RowData.IsValid())
	{
		FJsonObjectConverter::JsonObjectToUStruct(RowData.ToSharedRef(), RowStruct, RowMemory);
	}

	// Add the row
	DataTable->AddRow(FName(*RowName), *reinterpret_cast<FTableRowBase*>(RowMemory));

	// Cleanup
	RowStruct->DestroyStruct(RowMemory);
	FMemory::Free(RowMemory);

	FBridgeAssetModifier::MarkPackageDirty(DataTable);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("row_name"), RowName);
	Result->SetBoolField(TEXT("needs_save"), true);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-datatable-row: Added row %s"), *RowName);

	return FBridgeToolResult::Json(Result);
}
