// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgePropertySerializer.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "JsonObjectConverter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Animation/AnimBlueprint.h"
#include "Materials/Material.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "StructUtils/InstancedStruct.h"

TSharedPtr<FScopedTransaction> FBridgeAssetModifier::BeginTransaction(const FText& Description)
{
	return MakeShared<FScopedTransaction>(Description);
}

UObject* FBridgeAssetModifier::LoadAssetByPath(const FString& AssetPath, FString& OutError)
{
	if (!ValidateAssetPath(AssetPath, OutError))
	{
		return nullptr;
	}

	// Try to load the asset
	UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Object)
	{
		OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
		return nullptr;
	}

	return Object;
}

bool FBridgeAssetModifier::MarkModified(UObject* Object)
{
	if (!Object)
	{
		return false;
	}

	Object->Modify();
	return true;
}

bool FBridgeAssetModifier::MarkPackageDirty(UObject* Object)
{
	if (!Object)
	{
		return false;
	}

	UPackage* Package = Object->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		return true;
	}

	return false;
}

bool FBridgeAssetModifier::SaveAsset(UObject* Object, bool bPromptUser, FString& OutError)
{
	if (!Object)
	{
		OutError = TEXT("Cannot save null object");
		return false;
	}

	UPackage* Package = Object->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Object has no package");
		return false;
	}

	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());

	// Check if the file exists and is read-only before attempting to save
	if (IFileManager::Get().FileExists(*PackageFileName))
	{
		if (IFileManager::Get().IsReadOnly(*PackageFileName))
		{
			OutError = FString::Printf(TEXT("Cannot save '%s': file is read-only (check out from source control first)"), *PackageFileName);
			return false;
		}
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	// Don't use GError - it causes critical errors and crashes when save fails
	SaveArgs.Error = nullptr;
	SaveArgs.bWarnOfLongFilename = true;

	FSavePackageResultStruct Result = UPackage::Save(Package, Object, *PackageFileName, SaveArgs);

	if (Result.Result == ESavePackageResult::Success)
	{
		return true;
	}

	// Provide more specific error messages based on result
	switch (Result.Result)
	{
	case ESavePackageResult::Error:
		OutError = FString::Printf(TEXT("Error saving asset: %s"), *PackageFileName);
		break;
	case ESavePackageResult::Canceled:
		OutError = TEXT("Save was canceled");
		break;
	case ESavePackageResult::MissingFile:
		OutError = FString::Printf(TEXT("Missing file: %s"), *PackageFileName);
		break;
	default:
		OutError = FString::Printf(TEXT("Failed to save asset: %s (result: %d)"), *PackageFileName, static_cast<int32>(Result.Result));
		break;
	}
	return false;
}

bool FBridgeAssetModifier::CheckoutFile(const FString& PackageFileName, FString& OutError)
{
	ISourceControlModule& SCM = ISourceControlModule::Get();
	ISourceControlProvider& Provider = SCM.GetProvider();

	if (!Provider.IsEnabled())
	{
		OutError = TEXT("Source control is not enabled");
		return false;
	}

	FSourceControlStatePtr State = Provider.GetState(PackageFileName, EStateCacheUsage::ForceUpdate);
	if (!State.IsValid())
	{
		OutError = TEXT("Could not get source control state");
		return false;
	}

	if (State->IsCheckedOut() || State->IsAdded())
	{
		return true; // Already checked out
	}

	if (!State->CanCheckout())
	{
		OutError = FString::Printf(TEXT("Cannot check out '%s' (may be locked by another user)"), *FPaths::GetCleanFilename(PackageFileName));
		return false;
	}

	ECommandResult::Type Result = Provider.Execute(
		ISourceControlOperation::Create<FCheckOut>(),
		PackageFileName);

	if (Result != ECommandResult::Succeeded)
	{
		OutError = FString::Printf(TEXT("Source control checkout failed for '%s'"), *FPaths::GetCleanFilename(PackageFileName));
		return false;
	}

	return true;
}

bool FBridgeAssetModifier::CompileBlueprint(UBlueprint* Blueprint, FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Cannot compile null Blueprint");
		return false;
	}

	// Compile the Blueprint
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, nullptr);

	// Check for compile errors
	if (Blueprint->Status == BS_Error)
	{
		OutError = TEXT("Blueprint compilation failed with errors");
		return false;
	}

	return true;
}

void FBridgeAssetModifier::RefreshBlueprintNodes(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}

	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
}

void FBridgeAssetModifier::RefreshMaterial(class UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	// Notify Material Editor of changes via PreEditChange/PostEditChange
	// Note: FMaterialUpdateContext removed to avoid RHI module dependency issues
	// PreEditChange/PostEditChange will trigger Material Editor UI refresh
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
}

bool FBridgeAssetModifier::ValidateAssetPath(const FString& AssetPath, FString& OutError)
{
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("Asset path is empty");
		return false;
	}

	// Must start with /
	if (!AssetPath.StartsWith(TEXT("/")))
	{
		OutError = TEXT("Asset path must start with '/'");
		return false;
	}

	// Basic character validation
	for (TCHAR Char : AssetPath)
	{
		if (!FChar::IsAlnum(Char) && Char != '/' && Char != '_' && Char != '-' && Char != '.')
		{
			OutError = FString::Printf(TEXT("Invalid character in asset path: '%c'"), Char);
			return false;
		}
	}

	return true;
}

bool FBridgeAssetModifier::AssetExists(const FString& AssetPath)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	return AssetData.IsValid();
}

bool FBridgeAssetModifier::ParseArrayIndex(const FString& Segment, FString& OutName, int32& OutIndex)
{
	OutIndex = -1;

	int32 BracketStart = INDEX_NONE;
	if (!Segment.FindChar('[', BracketStart))
	{
		OutName = Segment;
		return true;
	}

	int32 BracketEnd = INDEX_NONE;
	if (!Segment.FindChar(']', BracketEnd) || BracketEnd <= BracketStart + 1)
	{
		return false;
	}

	OutName = Segment.Left(BracketStart);
	FString IndexStr = Segment.Mid(BracketStart + 1, BracketEnd - BracketStart - 1);

	if (!FCString::IsNumeric(*IndexStr))
	{
		return false;
	}

	OutIndex = FCString::Atoi(*IndexStr);
	return OutIndex >= 0;
}

bool FBridgeAssetModifier::FindPropertyByPath(
	UObject* Object,
	const FString& PropertyPath,
	FProperty*& OutProperty,
	void*& OutContainer,
	FString& OutError)
{
	if (!Object)
	{
		OutError = TEXT("Object is null");
		return false;
	}

	if (PropertyPath.IsEmpty())
	{
		OutError = TEXT("Property path is empty");
		return false;
	}

	// Split path into segments
	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."));

	if (Segments.Num() == 0)
	{
		OutError = TEXT("Invalid property path");
		return false;
	}

	UStruct* CurrentStruct = Object->GetClass();
	void* CurrentContainer = Object;
	FProperty* CurrentProperty = nullptr;

	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		const FString& Segment = Segments[i];

		// Parse array index if present
		FString PropertyName;
		int32 ArrayIndex;
		if (!ParseArrayIndex(Segment, PropertyName, ArrayIndex))
		{
			OutError = FString::Printf(TEXT("Invalid array index in segment: %s"), *Segment);
			return false;
		}

		// Find the property
		CurrentProperty = CurrentStruct->FindPropertyByName(*PropertyName);
		if (!CurrentProperty)
		{
			OutError = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
			return false;
		}

		// Handle array access
		if (ArrayIndex >= 0)
		{
			FArrayProperty* ArrayProp = CastField<FArrayProperty>(CurrentProperty);
			if (!ArrayProp)
			{
				OutError = FString::Printf(TEXT("Property '%s' is not an array"), *PropertyName);
				return false;
			}

			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CurrentContainer));
			if (ArrayIndex >= ArrayHelper.Num())
			{
				OutError = FString::Printf(TEXT("Array index %d out of bounds (size: %d)"), ArrayIndex, ArrayHelper.Num());
				return false;
			}

			CurrentContainer = ArrayHelper.GetRawPtr(ArrayIndex);
			CurrentProperty = ArrayProp->Inner;

			// If this is the last segment, we're done
			if (i == Segments.Num() - 1)
			{
				break;
			}

			// Otherwise, continue with inner struct
			FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
			if (InnerStructProp)
			{
				CurrentStruct = InnerStructProp->Struct;
				if (InnerStructProp->Struct == FInstancedStruct::StaticStruct())
				{
					FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(CurrentContainer);
					if (!InstancedStruct || !InstancedStruct->IsValid())
					{
						OutError = FString::Printf(TEXT("InstancedStruct array element '%s' is empty"), *Segment);
						return false;
					}

					CurrentContainer = InstancedStruct->GetMutableMemory();
					CurrentStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
					if (!CurrentContainer || !CurrentStruct)
					{
						OutError = FString::Printf(TEXT("InstancedStruct array element '%s' has no resolved inner struct"), *Segment);
						return false;
					}
				}
			}
			else
			{
				OutError = FString::Printf(TEXT("Cannot traverse into non-struct array element at: %s"), *Segment);
				return false;
			}
		}
		else
		{
			// Not an array access - check if this is the last segment
			if (i == Segments.Num() - 1)
			{
				// This is the target property
				break;
			}

			// Need to traverse into a struct
			FStructProperty* StructProp = CastField<FStructProperty>(CurrentProperty);
			FObjectProperty* ObjectProp = CastField<FObjectProperty>(CurrentProperty);

			if (StructProp)
			{
				CurrentContainer = CurrentProperty->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;

				if (StructProp->Struct == FInstancedStruct::StaticStruct())
				{
					FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(CurrentContainer);
					if (!InstancedStruct || !InstancedStruct->IsValid())
					{
						OutError = FString::Printf(TEXT("InstancedStruct property '%s' is empty"), *PropertyName);
						return false;
					}

					CurrentContainer = InstancedStruct->GetMutableMemory();
					CurrentStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
					if (!CurrentContainer || !CurrentStruct)
					{
						OutError = FString::Printf(TEXT("InstancedStruct property '%s' has no resolved inner struct"), *PropertyName);
						return false;
					}
				}
			}
			else if (ObjectProp)
			{
				UObject* ObjectValue = ObjectProp->GetObjectPropertyValue_InContainer(CurrentContainer);
				if (!ObjectValue)
				{
					OutError = FString::Printf(TEXT("Object property '%s' is null"), *PropertyName);
					return false;
				}
				CurrentContainer = ObjectValue;
				CurrentStruct = ObjectValue->GetClass();
			}
			else
			{
				OutError = FString::Printf(TEXT("Cannot traverse property '%s' - not a struct or object"), *PropertyName);
				return false;
			}
		}
	}

	OutProperty = CurrentProperty;
	OutContainer = CurrentContainer;
	return true;
}

bool FBridgeAssetModifier::SetPropertyFromJson(
	FProperty* Property,
	void* Container,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	// Delegate to the unified BridgePropertySerializer
	// This provides support for TMap, TSet, Object references, and all other property types
	return FBridgePropertySerializer::DeserializePropertyValue(Property, Container, Value, OutError);
}

UEdGraph* FBridgeAssetModifier::FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Check standard graphs first
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	// Check AnimBlueprint-specific graphs
	if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint))
	{
		TArray<UEdGraph*> AllGraphs;
		AnimBP->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}
			if (Blueprint->UbergraphPages.Contains(Graph) || Blueprint->FunctionGraphs.Contains(Graph))
			{
				continue; // Already checked
			}
			if (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}
	}

	return nullptr;
}

UEdGraphNode* FBridgeAssetModifier::FindNodeByGuid(UBlueprint* Blueprint, const FGuid& NodeGuid, UEdGraph** OutGraph)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	TArray<UEdGraph*> AllGraphs;
	GetAllSearchableGraphs(Blueprint, AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == NodeGuid)
			{
				if (OutGraph)
				{
					*OutGraph = Graph;
				}
				return Node;
			}
		}
	}

	return nullptr;
}

void FBridgeAssetModifier::GetAllSearchableGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
{
	if (!Blueprint)
	{
		return;
	}

	OutGraphs.Append(Blueprint->UbergraphPages);
	OutGraphs.Append(Blueprint->FunctionGraphs);

	if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint))
	{
		TArray<UEdGraph*> AnimGraphs;
		AnimBP->GetAllGraphs(AnimGraphs);

		for (UEdGraph* Graph : AnimGraphs)
		{
			if (Graph && !OutGraphs.Contains(Graph))
			{
				OutGraphs.Add(Graph);
			}
		}
	}
}
