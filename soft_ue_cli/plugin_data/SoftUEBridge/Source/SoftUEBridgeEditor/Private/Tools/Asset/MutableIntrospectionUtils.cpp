// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/MutableIntrospectionUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "SoftUEBridgeEditorModule.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FMutableAssetContext
	{
		FString AssetPath;
		FAssetData AssetData;
		UObject* AssetObject = nullptr;
		bool bAssetFound = false;
		bool bAssetLoaded = false;
		bool bLooksLikeMutableAsset = false;
		bool bMutablePluginEnabled = false;
		TSharedPtr<FJsonObject> PluginInfo;
	};

	struct FGraphInspectionData
	{
		TArray<TSharedPtr<FJsonValue>> Graphs;
		TArray<TSharedPtr<FJsonValue>> Nodes;
		TArray<TSharedPtr<FJsonValue>> Edges;
		TMap<FString, TSharedPtr<FJsonObject>> NodeByPath;
		TMultiMap<FString, FString> Adjacency;
		int32 ProjectorNodeCount = 0;
		int32 ObjectGroupNodeCount = 0;
		int32 TableNodeCount = 0;
		int32 OutputNodeCount = 0;
		int32 ParameterNodeCount = 0;
	};

	struct FRuntimeMutableParameter
	{
		int32 Index = INDEX_NONE;
		FString Name;
		FString ParameterType;
		TArray<FString> Options;
	};

	static bool ContainsToken(const FString& Source, std::initializer_list<const TCHAR*> Tokens)
	{
		for (const TCHAR* Token : Tokens)
		{
			if (Source.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static FString SafeClassName(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetName() : FString();
	}

	static bool LooksLikeMutableClassName(const FString& ClassName)
	{
		return ContainsToken(ClassName, {TEXT("CustomizableObject"), TEXT("Mutable")});
	}

	static FString JsonValueToFlatString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}

		switch (Value->Type)
		{
		case EJson::String:
			return Value->AsString();
		case EJson::Number:
			return FString::SanitizeFloat(Value->AsNumber());
		case EJson::Boolean:
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		default:
			return FString();
		}
	}

	static TSharedPtr<FJsonValue> SimplePropertyToJsonValue(const FProperty* Property, const void* ContainerPtr, UObject* ParentObject);

	static void AddUniqueString(TArray<FString>& Values, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Values.AddUnique(Value);
		}
	}

	static TArray<TSharedPtr<FJsonValue>> ToJsonStringArray(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (const FString& Value : Values)
		{
			JsonValues.Add(MakeShared<FJsonValueString>(Value));
		}
		return JsonValues;
	}

	static FString NormalizeMutableMemberName(FString Name)
	{
		Name.ToLowerInline();
		Name.ReplaceInline(TEXT("_"), TEXT(""));
		return Name;
	}

	static bool IsStringLikeProperty(const FProperty* Property)
	{
		return Property &&
			(Property->IsA<FStrProperty>() || Property->IsA<FNameProperty>() || Property->IsA<FTextProperty>());
	}

	static bool SetStringLikeProperty(FProperty* Property, uint8* Buffer, const FString& Value)
	{
		if (!Property || !Buffer)
		{
			return false;
		}
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Buffer);
		if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			StrProperty->SetPropertyValue(ValuePtr, Value);
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			NameProperty->SetPropertyValue(ValuePtr, FName(*Value));
			return true;
		}
		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			TextProperty->SetPropertyValue(ValuePtr, FText::FromString(Value));
			return true;
		}
		return false;
	}

	static bool SetIntegerProperty(FProperty* Property, uint8* Buffer, int32 Value)
	{
		FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
		if (!NumericProperty || !NumericProperty->IsInteger() || !Buffer)
		{
			return false;
		}
		NumericProperty->SetIntPropertyValue(
			NumericProperty->ContainerPtrToValuePtr<void>(Buffer),
			static_cast<int64>(Value));
		return true;
	}

	static FProperty* FindRuntimeReturnProperty(UFunction* Function)
	{
		if (!Function)
		{
			return nullptr;
		}
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (Property && Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				return Property;
			}
		}
		return nullptr;
	}

	static bool ReturnPropertyToString(FProperty* ReturnProperty, uint8* Buffer, UObject* Owner, FString& OutValue)
	{
		if (!ReturnProperty || !Buffer)
		{
			return false;
		}
		void* ReturnValuePtr = ReturnProperty->ContainerPtrToValuePtr<void>(Buffer);
		if (FStrProperty* StrProperty = CastField<FStrProperty>(ReturnProperty))
		{
			OutValue = StrProperty->GetPropertyValue(ReturnValuePtr);
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(ReturnProperty))
		{
			OutValue = NameProperty->GetPropertyValue(ReturnValuePtr).ToString();
			return true;
		}
		if (FTextProperty* TextProperty = CastField<FTextProperty>(ReturnProperty))
		{
			OutValue = TextProperty->GetPropertyValue(ReturnValuePtr).ToString();
			return true;
		}
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(ReturnProperty))
		{
			const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ReturnValuePtr);
			OutValue = EnumProperty->GetEnum()
				? EnumProperty->GetEnum()->GetNameStringByValue(EnumValue)
				: FString::Printf(TEXT("%lld"), static_cast<long long>(EnumValue));
			return true;
		}
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(ReturnProperty))
		{
			const uint8 ByteValue = ByteProperty->GetPropertyValue(ReturnValuePtr);
			OutValue = ByteProperty->Enum
				? ByteProperty->Enum->GetNameStringByValue(ByteValue)
				: FString::Printf(TEXT("%u"), static_cast<unsigned int>(ByteValue));
			return true;
		}

		ReturnProperty->ExportText_Direct(OutValue, ReturnValuePtr, ReturnValuePtr, Owner, PPF_None);
		return !OutValue.IsEmpty();
	}

	static bool ReturnPropertyToInteger(FProperty* ReturnProperty, uint8* Buffer, int32& OutValue)
	{
		FNumericProperty* NumericProperty = CastField<FNumericProperty>(ReturnProperty);
		if (!NumericProperty || !NumericProperty->IsInteger() || !Buffer)
		{
			return false;
		}
		OutValue = static_cast<int32>(NumericProperty->GetSignedIntPropertyValue(
			NumericProperty->ContainerPtrToValuePtr<void>(Buffer)));
		return true;
	}

	static TArray<FProperty*> GetRuntimeInputProperties(UFunction* Function)
	{
		TArray<FProperty*> Inputs;
		if (!Function)
		{
			return Inputs;
		}
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (Property && Property->HasAnyPropertyFlags(CPF_Parm) &&
				!Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				Inputs.Add(Property);
			}
		}
		return Inputs;
	}

	static bool TryCallNoParamRuntimeInteger(UObject* AssetObject, const FName FunctionName, int32& OutValue)
	{
		if (!AssetObject)
		{
			return false;
		}
		UFunction* Function = AssetObject->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}
		if (GetRuntimeInputProperties(Function).Num() > 0)
		{
			return false;
		}

		FStructOnScope Params(Function);
		uint8* Buffer = Params.GetStructMemory();
		AssetObject->ProcessEvent(Function, Buffer);
		return ReturnPropertyToInteger(FindRuntimeReturnProperty(Function), Buffer, OutValue);
	}

	static bool TryCallRuntimeIndexedString(UObject* AssetObject, const FName FunctionName, int32 Index, FString& OutValue)
	{
		if (!AssetObject)
		{
			return false;
		}
		UFunction* Function = AssetObject->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		FStructOnScope Params(Function);
		uint8* Buffer = Params.GetStructMemory();
		bool bAssignedIndex = false;
		for (FProperty* Property : GetRuntimeInputProperties(Function))
		{
			if (!bAssignedIndex && SetIntegerProperty(Property, Buffer, Index))
			{
				bAssignedIndex = true;
			}
		}
		if (!bAssignedIndex)
		{
			return false;
		}

		AssetObject->ProcessEvent(Function, Buffer);
		return ReturnPropertyToString(FindRuntimeReturnProperty(Function), Buffer, AssetObject, OutValue);
	}

	static bool RuntimeFunctionHasStringParameter(UFunction* Function)
	{
		for (FProperty* Property : GetRuntimeInputProperties(Function))
		{
			if (IsStringLikeProperty(Property))
			{
				return true;
			}
		}
		return false;
	}

	static bool TryCallEnumParameterNumValues(
		UObject* AssetObject,
		const FString& ParameterName,
		int32 ParameterIndex,
		int32& OutCount)
	{
		if (!AssetObject)
		{
			return false;
		}
		UFunction* Function = AssetObject->FindFunction(FName(TEXT("GetEnumParameterNumValues")));
		if (!Function)
		{
			return false;
		}

		FStructOnScope Params(Function);
		uint8* Buffer = Params.GetStructMemory();
		const bool bUsesName = RuntimeFunctionHasStringParameter(Function);
		bool bAssignedSelector = false;

		for (FProperty* Property : GetRuntimeInputProperties(Function))
		{
			if (bUsesName && !bAssignedSelector && SetStringLikeProperty(Property, Buffer, ParameterName))
			{
				bAssignedSelector = true;
			}
			else if (!bUsesName && !bAssignedSelector && SetIntegerProperty(Property, Buffer, ParameterIndex))
			{
				bAssignedSelector = true;
			}
		}
		if (!bAssignedSelector)
		{
			return false;
		}

		AssetObject->ProcessEvent(Function, Buffer);
		return ReturnPropertyToInteger(FindRuntimeReturnProperty(Function), Buffer, OutCount);
	}

	static bool TryCallEnumParameterValue(
		UObject* AssetObject,
		const FString& ParameterName,
		int32 ParameterIndex,
		int32 OptionIndex,
		FString& OutValue)
	{
		if (!AssetObject)
		{
			return false;
		}
		UFunction* Function = AssetObject->FindFunction(FName(TEXT("GetEnumParameterValue")));
		if (!Function)
		{
			return false;
		}

		FStructOnScope Params(Function);
		uint8* Buffer = Params.GetStructMemory();
		const bool bUsesName = RuntimeFunctionHasStringParameter(Function);
		bool bAssignedSelector = false;
		bool bAssignedOption = false;

		for (FProperty* Property : GetRuntimeInputProperties(Function))
		{
			if (bUsesName && !bAssignedSelector && SetStringLikeProperty(Property, Buffer, ParameterName))
			{
				bAssignedSelector = true;
				continue;
			}
			if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (!NumericProperty->IsInteger())
				{
					continue;
				}

				const FString NormalizedName = NormalizeMutableMemberName(Property->GetName());
				if (!bUsesName && !bAssignedSelector)
				{
					SetIntegerProperty(Property, Buffer, ParameterIndex);
					bAssignedSelector = true;
				}
				else if (!bAssignedOption ||
					NormalizedName.Contains(TEXT("value")) ||
					NormalizedName.Contains(TEXT("option")))
				{
					SetIntegerProperty(Property, Buffer, OptionIndex);
					bAssignedOption = true;
				}
			}
		}
		if (!bAssignedSelector || !bAssignedOption)
		{
			return false;
		}

		AssetObject->ProcessEvent(Function, Buffer);
		return ReturnPropertyToString(FindRuntimeReturnProperty(Function), Buffer, AssetObject, OutValue);
	}

	static TArray<FRuntimeMutableParameter> CollectRuntimeMutableParameters(UObject* AssetObject)
	{
		TArray<FRuntimeMutableParameter> RuntimeParameters;
		int32 RuntimeParameterCount = 0;
		if (!TryCallNoParamRuntimeInteger(AssetObject, FName(TEXT("GetParameterCount")), RuntimeParameterCount) ||
			RuntimeParameterCount <= 0)
		{
			return RuntimeParameters;
		}

		for (int32 Index = 0; Index < RuntimeParameterCount; ++Index)
		{
			FRuntimeMutableParameter RuntimeParameter;
			RuntimeParameter.Index = Index;
			TryCallRuntimeIndexedString(AssetObject, FName(TEXT("GetParameterName")), Index, RuntimeParameter.Name);
			TryCallRuntimeIndexedString(AssetObject, FName(TEXT("GetParameterType")), Index, RuntimeParameter.ParameterType);
			if (RuntimeParameter.Name.IsEmpty())
			{
				RuntimeParameter.Name = FString::Printf(TEXT("Parameter_%d"), Index);
			}

			int32 OptionCount = 0;
			if (TryCallEnumParameterNumValues(AssetObject, RuntimeParameter.Name, Index, OptionCount))
			{
				for (int32 OptionIndex = 0; OptionIndex < OptionCount; ++OptionIndex)
				{
					FString Option;
					if (TryCallEnumParameterValue(AssetObject, RuntimeParameter.Name, Index, OptionIndex, Option) &&
						!Option.IsEmpty())
					{
						RuntimeParameter.Options.AddUnique(Option);
					}
				}
			}
			RuntimeParameters.Add(RuntimeParameter);
		}

		return RuntimeParameters;
	}

	static void CollectStringsFromJsonValue(const TSharedPtr<FJsonValue>& Value, TArray<FString>& OutValues)
	{
		if (!Value.IsValid())
		{
			return;
		}

		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				CollectStringsFromJsonValue(Entry, OutValues);
			}
			return;
		}

		const FString FlatValue = JsonValueToFlatString(Value);
		if (!FlatValue.IsEmpty())
		{
			AddUniqueString(OutValues, FlatValue);
		}
	}

	static TSharedPtr<FJsonValue> SimplePropertyToJsonValue(const FProperty* Property, const void* ContainerPtr, UObject* ParentObject)
	{
		if (!Property || !ContainerPtr)
		{
			return nullptr;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		if (!ValuePtr)
		{
			return nullptr;
		}

		if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(ValuePtr));
		}
		if (const FIntProperty* IntProperty = CastField<const FIntProperty>(Property))
		{
			return MakeShared<FJsonValueNumber>(IntProperty->GetPropertyValue(ValuePtr));
		}
		if (const FInt64Property* Int64Property = CastField<const FInt64Property>(Property))
		{
			return MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), static_cast<long long>(Int64Property->GetPropertyValue(ValuePtr))));
		}
		if (const FFloatProperty* FloatProperty = CastField<const FFloatProperty>(Property))
		{
			return MakeShared<FJsonValueNumber>(FloatProperty->GetPropertyValue(ValuePtr));
		}
		if (const FDoubleProperty* DoubleProperty = CastField<const FDoubleProperty>(Property))
		{
			return MakeShared<FJsonValueNumber>(DoubleProperty->GetPropertyValue(ValuePtr));
		}
		if (const FNameProperty* NameProperty = CastField<const FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(ValuePtr).ToString());
		}
		if (const FStrProperty* StrProperty = CastField<const FStrProperty>(Property))
		{
			return MakeShared<FJsonValueString>(StrProperty->GetPropertyValue(ValuePtr));
		}
		if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property))
		{
			return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(ValuePtr).ToString());
		}
		if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
		{
			const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(
				ValuePtr);
			if (const UEnum* Enum = EnumProperty->GetEnum())
			{
				return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(EnumValue));
			}
			return MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), static_cast<long long>(EnumValue)));
		}
		if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
		{
			const uint8 ByteValue = ByteProperty->GetPropertyValue(ValuePtr);
			if (ByteProperty->Enum)
			{
				return MakeShared<FJsonValueString>(ByteProperty->Enum->GetNameStringByValue(ByteValue));
			}
			return MakeShared<FJsonValueNumber>(ByteValue);
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
		{
			if (const UObject* ValueObject = ObjectProperty->GetObjectPropertyValue(ValuePtr))
			{
				return MakeShared<FJsonValueString>(ValueObject->GetPathName());
			}
			return nullptr;
		}
		if (const FSoftObjectProperty* SoftObjectProperty = CastField<const FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr Value = SoftObjectProperty->GetPropertyValue(ValuePtr);
			if (!Value.IsNull())
			{
				return MakeShared<FJsonValueString>(Value.ToSoftObjectPath().ToString());
			}
			return nullptr;
		}
		if (const FSoftClassProperty* SoftClassProperty = CastField<const FSoftClassProperty>(Property))
		{
			const FSoftObjectPtr Value = SoftClassProperty->GetPropertyValue(ValuePtr);
			if (!Value.IsNull())
			{
				return MakeShared<FJsonValueString>(Value.ToSoftObjectPath().ToString());
			}
			return nullptr;
		}
		if (const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(Property))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				const TSharedPtr<FJsonValue> Entry = SimplePropertyToJsonValue(
					ArrayProperty->Inner,
					Helper.GetRawPtr(Index),
					ParentObject);
				if (Entry.IsValid())
				{
					Values.Add(Entry);
				}
			}
			return MakeShared<FJsonValueArray>(Values);
		}

		FString Exported;
		Property->ExportText_Direct(Exported, ValuePtr, ValuePtr, ParentObject, PPF_None);
		if (Exported.IsEmpty())
		{
			return nullptr;
		}
		return MakeShared<FJsonValueString>(Exported);
	}

	static TSharedPtr<FJsonObject> BuildPluginInfo(bool& bOutMutablePluginEnabled)
	{
		bOutMutablePluginEnabled = false;

		const TArray<FString> PluginNames = {
			TEXT("Mutable"),
			TEXT("MutableTools"),
			TEXT("CustomizableObject"),
			TEXT("CustomizableObjectEditor")
		};

		TSharedPtr<FJsonObject> PluginsJson = MakeShared<FJsonObject>();
		for (const FString& PluginName : PluginNames)
		{
			TSharedPtr<FJsonObject> PluginJson = MakeShared<FJsonObject>();
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName))
			{
				PluginJson->SetBoolField(TEXT("found"), true);
				PluginJson->SetBoolField(TEXT("enabled"), Plugin->IsEnabled());
				PluginJson->SetStringField(TEXT("version"), Plugin->GetDescriptor().VersionName);
				PluginJson->SetStringField(TEXT("descriptor_path"), Plugin->GetDescriptorFileName());
				if (Plugin->IsEnabled())
				{
					bOutMutablePluginEnabled = true;
				}
			}
			else
			{
				PluginJson->SetBoolField(TEXT("found"), false);
				PluginJson->SetBoolField(TEXT("enabled"), false);
			}

			PluginsJson->SetObjectField(PluginName, PluginJson);
		}

		return PluginsJson;
	}

	static FMutableAssetContext ResolveAssetContext(const FString& AssetPath)
	{
		FMutableAssetContext Context;
		Context.AssetPath = AssetPath;
		Context.PluginInfo = BuildPluginInfo(Context.bMutablePluginEnabled);

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Context.AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		Context.bAssetFound = Context.AssetData.IsValid();
		if (Context.bAssetFound)
		{
			Context.AssetObject = Context.AssetData.GetAsset();
		}
		if (!Context.AssetObject)
		{
			Context.AssetObject = LoadObject<UObject>(nullptr, *AssetPath);
		}

		Context.bAssetLoaded = Context.AssetObject != nullptr;
		const FString ClassName = Context.bAssetLoaded
			? SafeClassName(Context.AssetObject)
			: (Context.bAssetFound ? Context.AssetData.AssetClassPath.GetAssetName().ToString() : FString());
		Context.bLooksLikeMutableAsset = LooksLikeMutableClassName(ClassName);
		return Context;
	}

	static TSharedPtr<FJsonObject> BuildBaseResult(const FMutableAssetContext& Context)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("asset_path"), Context.AssetPath);
		Result->SetBoolField(TEXT("asset_found"), Context.bAssetFound);
		Result->SetBoolField(TEXT("asset_loaded"), Context.bAssetLoaded);
		Result->SetBoolField(TEXT("mutable_plugin_enabled"), Context.bMutablePluginEnabled);
		Result->SetObjectField(TEXT("plugins"), Context.PluginInfo);
		Result->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

		if (Context.bAssetFound)
		{
			Result->SetStringField(TEXT("asset_class"), Context.AssetData.AssetClassPath.GetAssetName().ToString());
			Result->SetStringField(TEXT("asset_name"), Context.AssetData.AssetName.ToString());
		}
		else
		{
			Result->SetStringField(TEXT("asset_class"), TEXT(""));
			Result->SetStringField(TEXT("asset_name"), TEXT(""));
		}

		if (Context.bAssetLoaded)
		{
			Result->SetStringField(TEXT("loaded_class"), SafeClassName(Context.AssetObject));
			Result->SetStringField(TEXT("loaded_object_path"), Context.AssetObject->GetPathName());
		}

		Result->SetBoolField(TEXT("is_customizable_object"), Context.bLooksLikeMutableAsset);
		return Result;
	}

	static TArray<UEdGraph*> CollectGraphs(UObject* AssetObject)
	{
		TArray<UEdGraph*> Graphs;
		if (!AssetObject)
		{
			return Graphs;
		}

		if (UEdGraph* DirectGraph = Cast<UEdGraph>(AssetObject))
		{
			Graphs.Add(DirectGraph);
		}

		TArray<UObject*> InnerObjects;
		GetObjectsWithOuter(AssetObject, InnerObjects, true);
		for (UObject* InnerObject : InnerObjects)
		{
			if (UEdGraph* Graph = Cast<UEdGraph>(InnerObject))
			{
				Graphs.AddUnique(Graph);
			}
		}

		return Graphs;
	}

	static TSharedPtr<FJsonObject> CollectInterestingObjectProperties(
		UObject* Object,
		bool bIncludeAllSimpleProperties,
		TArray<FString>* OutReferenceValues = nullptr)
	{
		TSharedPtr<FJsonObject> PropertiesJson = MakeShared<FJsonObject>();
		if (!Object)
		{
			return PropertiesJson;
		}

		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			const bool bInteresting = bIncludeAllSimpleProperties ||
				ContainsToken(PropertyName, {
					TEXT("Name"), TEXT("Parameter"), TEXT("Group"), TEXT("Option"),
					TEXT("Default"), TEXT("Tag"), TEXT("Population"), TEXT("Child"),
					TEXT("Object"), TEXT("Projector"), TEXT("Table"), TEXT("Mesh"),
					TEXT("Physics"), TEXT("Cloth"), TEXT("LOD"), TEXT("Memory"),
					TEXT("Limit"), TEXT("Nanite")
				});
			if (!bInteresting)
			{
				continue;
			}

			const TSharedPtr<FJsonValue> Value = SimplePropertyToJsonValue(Property, Object, Object);
			if (!Value.IsValid())
			{
				continue;
			}

			PropertiesJson->SetField(PropertyName, Value);
			if (OutReferenceValues)
			{
				CollectStringsFromJsonValue(Value, *OutReferenceValues);
			}
		}

		return PropertiesJson;
	}

	static bool IsParameterNode(const FString& ClassName, const FString& Title)
	{
		return ContainsToken(ClassName, {TEXT("Parameter")}) || ContainsToken(Title, {TEXT("Parameter")});
	}

	static FString DeriveNodeRole(const FString& ClassName, const FString& Title)
	{
		if (ContainsToken(ClassName, {TEXT("Projector")}) || ContainsToken(Title, {TEXT("Projector")}))
		{
			return TEXT("projector");
		}
		if (ContainsToken(ClassName, {TEXT("ObjectGroup")}) || ContainsToken(Title, {TEXT("Object Group")}))
		{
			return TEXT("object_group");
		}
		if (ContainsToken(ClassName, {TEXT("Table")}) || ContainsToken(Title, {TEXT("Table")}))
		{
			return TEXT("table");
		}
		if (ContainsToken(ClassName, {TEXT("Output")}) || ContainsToken(Title, {TEXT("Output")}))
		{
			return TEXT("output");
		}
		if (IsParameterNode(ClassName, Title))
		{
			return TEXT("parameter");
		}
		return TEXT("generic");
	}

	static FGraphInspectionData InspectGraphs(UObject* AssetObject, bool bIncludeNodeProperties)
	{
		FGraphInspectionData Data;
		const TArray<UEdGraph*> Graphs = CollectGraphs(AssetObject);
		TSet<FString> EdgeKeys;

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
			GraphJson->SetStringField(TEXT("name"), Graph->GetName());
			GraphJson->SetStringField(TEXT("path"), Graph->GetPathName());
			GraphJson->SetStringField(TEXT("class"), SafeClassName(Graph));
			GraphJson->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
			Data.Graphs.Add(MakeShared<FJsonValueObject>(GraphJson));

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				const FString NodeClass = SafeClassName(Node);
				const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
				const FString NodeRole = DeriveNodeRole(NodeClass, NodeTitle);

				TArray<FString> ReferenceValues;
				TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
				NodeJson->SetStringField(TEXT("path"), Node->GetPathName());
				NodeJson->SetStringField(TEXT("name"), Node->GetName());
				NodeJson->SetStringField(TEXT("title"), NodeTitle);
				NodeJson->SetStringField(TEXT("class"), NodeClass);
				NodeJson->SetStringField(TEXT("graph_path"), Graph->GetPathName());
				NodeJson->SetStringField(TEXT("graph_name"), Graph->GetName());
				NodeJson->SetNumberField(TEXT("pos_x"), Node->NodePosX);
				NodeJson->SetNumberField(TEXT("pos_y"), Node->NodePosY);
				NodeJson->SetStringField(TEXT("role"), NodeRole);
				NodeJson->SetBoolField(TEXT("is_parameter_node"), NodeRole == TEXT("parameter"));
				NodeJson->SetBoolField(TEXT("is_projector_node"), NodeRole == TEXT("projector"));
				NodeJson->SetBoolField(TEXT("is_object_group_node"), NodeRole == TEXT("object_group"));
				NodeJson->SetBoolField(TEXT("is_table_node"), NodeRole == TEXT("table"));
				NodeJson->SetBoolField(TEXT("is_output_node"), NodeRole == TEXT("output"));

				if (bIncludeNodeProperties)
				{
					NodeJson->SetObjectField(TEXT("properties"), CollectInterestingObjectProperties(Node, true, &ReferenceValues));
				}
				else
				{
					NodeJson->SetObjectField(TEXT("properties"), CollectInterestingObjectProperties(Node, false, &ReferenceValues));
				}

				NodeJson->SetArrayField(TEXT("references"), ToJsonStringArray(ReferenceValues));

				TArray<TSharedPtr<FJsonValue>> PinArray;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin)
					{
						continue;
					}

					TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
					PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
					PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"));
					PinJson->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
					PinJson->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());

					TArray<TSharedPtr<FJsonValue>> LinkedArray;
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (!LinkedPin || !LinkedPin->GetOwningNode())
						{
							continue;
						}

						LinkedArray.Add(MakeShared<FJsonValueString>(
							FString::Printf(TEXT("%s:%s"), *LinkedPin->GetOwningNode()->GetPathName(), *LinkedPin->PinName.ToString())));

						if (Pin->Direction == EGPD_Output)
						{
							const FString EdgeKey = FString::Printf(
								TEXT("%s|%s|%s|%s"),
								*Node->GetPathName(),
								*Pin->PinName.ToString(),
								*LinkedPin->GetOwningNode()->GetPathName(),
								*LinkedPin->PinName.ToString());
							if (!EdgeKeys.Contains(EdgeKey))
							{
								EdgeKeys.Add(EdgeKey);
								TSharedPtr<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
								EdgeJson->SetStringField(TEXT("from_node"), Node->GetPathName());
								EdgeJson->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
								EdgeJson->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->GetPathName());
								EdgeJson->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
								Data.Edges.Add(MakeShared<FJsonValueObject>(EdgeJson));
								Data.Adjacency.Add(Node->GetPathName(), LinkedPin->GetOwningNode()->GetPathName());
								Data.Adjacency.Add(LinkedPin->GetOwningNode()->GetPathName(), Node->GetPathName());
							}
						}
					}

					PinJson->SetArrayField(TEXT("linked_to"), LinkedArray);
					PinArray.Add(MakeShared<FJsonValueObject>(PinJson));
				}

				NodeJson->SetArrayField(TEXT("pins"), PinArray);
				Data.NodeByPath.Add(Node->GetPathName(), NodeJson);
				Data.Nodes.Add(MakeShared<FJsonValueObject>(NodeJson));

				if (NodeRole == TEXT("parameter")) ++Data.ParameterNodeCount;
				if (NodeRole == TEXT("projector")) ++Data.ProjectorNodeCount;
				if (NodeRole == TEXT("object_group")) ++Data.ObjectGroupNodeCount;
				if (NodeRole == TEXT("table")) ++Data.TableNodeCount;
				if (NodeRole == TEXT("output")) ++Data.OutputNodeCount;
			}
		}

		return Data;
	}

	static FString FirstStringField(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> FieldNames)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		for (const TCHAR* FieldName : FieldNames)
		{
			FString Value;
			if (Object->TryGetStringField(FieldName, Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}

	static TArray<FString> GatherArrayFieldValues(
		const TSharedPtr<FJsonObject>& Object,
		std::initializer_list<const TCHAR*> FieldNames)
	{
		TArray<FString> Values;
		if (!Object.IsValid())
		{
			return Values;
		}

		for (const TCHAR* FieldName : FieldNames)
		{
			const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(FieldName);
			if (!FieldValue)
			{
				continue;
			}
			CollectStringsFromJsonValue(*FieldValue, Values);
		}
		return Values;
	}

	static FString DeriveParameterType(const FString& ClassName, const FString& Title)
	{
		if (ContainsToken(ClassName, {TEXT("Bool")}) || ContainsToken(Title, {TEXT("Bool"), TEXT("Toggle")}))
		{
			return TEXT("bool");
		}
		if (ContainsToken(ClassName, {TEXT("Int")}) || ContainsToken(Title, {TEXT("Int")}))
		{
			return TEXT("int");
		}
		if (ContainsToken(ClassName, {TEXT("Float"), TEXT("Scalar")}) || ContainsToken(Title, {TEXT("Float"), TEXT("Scalar")}))
		{
			return TEXT("float");
		}
		if (ContainsToken(ClassName, {TEXT("Color")}) || ContainsToken(Title, {TEXT("Color")}))
		{
			return TEXT("color");
		}
		if (ContainsToken(ClassName, {TEXT("Projector")}) || ContainsToken(Title, {TEXT("Projector")}))
		{
			return TEXT("projector");
		}
		if (ContainsToken(ClassName, {TEXT("Texture"), TEXT("Image")}) || ContainsToken(Title, {TEXT("Texture"), TEXT("Image")}))
		{
			return TEXT("texture");
		}
		if (ContainsToken(ClassName, {TEXT("Enum")}) || ContainsToken(Title, {TEXT("Enum")}))
		{
			return TEXT("enum");
		}
		return TEXT("unknown");
	}

	static FString DeriveParameterBehavior(const FString& ClassName, const FString& Title, const TSharedPtr<FJsonObject>& Properties)
	{
		const FString CombinedHints = ClassName + TEXT(" ") + Title + TEXT(" ") + FirstStringField(
			Properties,
			{TEXT("GroupType"), TEXT("Behavior"), TEXT("ParamType"), TEXT("ParameterType")});
		if (ContainsToken(CombinedHints, {TEXT("OneOrNone")}))
		{
			return TEXT("one_or_none");
		}
		if (ContainsToken(CombinedHints, {TEXT("Toggle"), TEXT("Bool")}))
		{
			return TEXT("toggle");
		}
		if (ContainsToken(CombinedHints, {TEXT("Overlay")}))
		{
			return TEXT("overlay");
		}
		if (ContainsToken(CombinedHints, {TEXT("Projector")}))
		{
			return TEXT("projector");
		}
		if (ContainsToken(CombinedHints, {TEXT("Group"), TEXT("Slot")}))
		{
			return TEXT("slot");
		}
		return TEXT("select");
	}

	static TSharedPtr<FJsonObject> BuildUnavailableAssetResult(
		const FMutableAssetContext& Context,
		const FString& Status,
		const FString& Reason)
	{
		TSharedPtr<FJsonObject> Result = BuildBaseResult(Context);
		Result->SetStringField(TEXT("status"), Status);
		Result->SetStringField(TEXT("reason"), Reason);
		Result->SetBoolField(TEXT("available"), false);
		Result->SetArrayField(TEXT("graphs"), {});
		Result->SetArrayField(TEXT("nodes"), {});
		Result->SetArrayField(TEXT("edges"), {});
		Result->SetArrayField(TEXT("parameters"), {});
		Result->SetArrayField(TEXT("detected_signals"), {});
		return Result;
	}

	static TSharedPtr<FJsonObject> BuildGraphSummaryJson(const FGraphInspectionData& GraphData)
	{
		TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("graph_count"), GraphData.Graphs.Num());
		Summary->SetNumberField(TEXT("node_count"), GraphData.Nodes.Num());
		Summary->SetNumberField(TEXT("edge_count"), GraphData.Edges.Num());
		Summary->SetNumberField(TEXT("parameter_node_count"), GraphData.ParameterNodeCount);
		Summary->SetNumberField(TEXT("projector_node_count"), GraphData.ProjectorNodeCount);
		Summary->SetNumberField(TEXT("object_group_node_count"), GraphData.ObjectGroupNodeCount);
		Summary->SetNumberField(TEXT("table_node_count"), GraphData.TableNodeCount);
		Summary->SetNumberField(TEXT("output_node_count"), GraphData.OutputNodeCount);
		return Summary;
	}
}

TSharedPtr<FJsonObject> MutableIntrospectionUtils::BuildCustomizableObjectGraphResult(
	const FString& AssetPath,
	bool bIncludeNodeProperties)
{
	const FMutableAssetContext Context = ResolveAssetContext(AssetPath);
	if (!Context.bAssetFound)
	{
		return BuildUnavailableAssetResult(Context, TEXT("asset_not_found"), TEXT("Asset was not found in the registry or could not be loaded."));
	}
	if (!Context.bLooksLikeMutableAsset)
	{
		return BuildUnavailableAssetResult(Context, TEXT("not_customizable_object"), TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}
	if (!Context.bAssetLoaded)
	{
		return BuildUnavailableAssetResult(Context, TEXT("not_loaded"), TEXT("Asset metadata exists, but the asset could not be loaded. This commonly happens when the Mutable plugin is not enabled for the project."));
	}

	const FGraphInspectionData GraphData = InspectGraphs(Context.AssetObject, bIncludeNodeProperties);

	TSharedPtr<FJsonObject> Result = BuildBaseResult(Context);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("available"), true);
	Result->SetArrayField(TEXT("graphs"), GraphData.Graphs);
	Result->SetArrayField(TEXT("nodes"), GraphData.Nodes);
	Result->SetArrayField(TEXT("edges"), GraphData.Edges);
	Result->SetObjectField(TEXT("summary"), BuildGraphSummaryJson(GraphData));
	return Result;
}

TSharedPtr<FJsonObject> MutableIntrospectionUtils::BuildMutableParameterResult(const FString& AssetPath)
{
	const FMutableAssetContext Context = ResolveAssetContext(AssetPath);
	if (!Context.bAssetFound)
	{
		return BuildUnavailableAssetResult(Context, TEXT("asset_not_found"), TEXT("Asset was not found in the registry or could not be loaded."));
	}
	if (!Context.bLooksLikeMutableAsset)
	{
		return BuildUnavailableAssetResult(Context, TEXT("not_customizable_object"), TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}
	if (!Context.bAssetLoaded)
	{
		return BuildUnavailableAssetResult(Context, TEXT("not_loaded"), TEXT("Asset metadata exists, but the asset could not be loaded. This commonly happens when the Mutable plugin is not enabled for the project."));
	}

	const FGraphInspectionData GraphData = InspectGraphs(Context.AssetObject, false);
	const TArray<FRuntimeMutableParameter> RuntimeParameters = CollectRuntimeMutableParameters(Context.AssetObject);
	TSet<int32> ConsumedRuntimeParameterIndexes;

	TArray<TSharedPtr<FJsonValue>> Parameters;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : GraphData.NodeByPath)
	{
		const TSharedPtr<FJsonObject>& Node = Pair.Value;
		if (!Node.IsValid() || !Node->GetBoolField(TEXT("is_parameter_node")))
		{
			continue;
		}

		const FString NodeClass = Node->GetStringField(TEXT("class"));
		const FString NodeTitle = Node->GetStringField(TEXT("title"));
		const TSharedPtr<FJsonObject> Properties = Node->GetObjectField(TEXT("properties"));
		const FString ParameterName = FirstStringField(Properties, {
			TEXT("ParameterName"), TEXT("ParamName"), TEXT("ObjectName"), TEXT("Name")
		});

		TArray<FString> AllowedOptions = GatherArrayFieldValues(Properties, {
			TEXT("Options"), TEXT("AllowedOptions"), TEXT("PossibleValues"), TEXT("Values")
		});
		const FString FinalParameterName = ParameterName.IsEmpty() ? NodeTitle : ParameterName;
		const FRuntimeMutableParameter* MatchingRuntimeParameter = nullptr;
		for (const FRuntimeMutableParameter& RuntimeParameter : RuntimeParameters)
		{
			if (!RuntimeParameter.Name.IsEmpty() &&
				RuntimeParameter.Name.Equals(FinalParameterName, ESearchCase::IgnoreCase))
			{
				MatchingRuntimeParameter = &RuntimeParameter;
				ConsumedRuntimeParameterIndexes.Add(RuntimeParameter.Index);
				for (const FString& Option : RuntimeParameter.Options)
				{
					AddUniqueString(AllowedOptions, Option);
				}
				break;
			}
		}
		TArray<FString> Tags = GatherArrayFieldValues(Properties, {TEXT("Tags"), TEXT("ParameterTags"), TEXT("Tag")});
		TArray<FString> PopulationTags = GatherArrayFieldValues(Properties, {
			TEXT("PopulationTags"), TEXT("PopulationTag")
		});
		TArray<FString> OwnedChildObjects = GatherArrayFieldValues(Properties, {
			TEXT("ChildObjects"), TEXT("ChildObject"), TEXT("Children"), TEXT("Object"), TEXT("ObjectName")
		});
		TArray<FString> CompatibilityLinks = GatherArrayFieldValues(Properties, {
			TEXT("Compatible"), TEXT("Compatibility"), TEXT("CompatibleTags")
		});
		TArray<FString> ExclusivityLinks = GatherArrayFieldValues(Properties, {
			TEXT("Exclusive"), TEXT("Exclusivity"), TEXT("Incompatible"), TEXT("IncompatibleTags")
		});

		TArray<FString> ConnectedNodes;
		GraphData.Adjacency.MultiFind(Pair.Key, ConnectedNodes);
		TArray<TSharedPtr<FJsonValue>> ConnectedNodeArray;
		for (const FString& ConnectedPath : ConnectedNodes)
		{
			ConnectedNodeArray.Add(MakeShared<FJsonValueString>(ConnectedPath));
			if (const TSharedPtr<FJsonObject>* ConnectedNode = GraphData.NodeByPath.Find(ConnectedPath))
			{
				const FString ConnectedClass = (*ConnectedNode)->GetStringField(TEXT("class"));
				if (ContainsToken(ConnectedClass, {TEXT("Compatible"), TEXT("Constraint")}))
				{
					AddUniqueString(CompatibilityLinks, ConnectedPath);
				}
				if (ContainsToken(ConnectedClass, {TEXT("Exclusive"), TEXT("Incompatible")}))
				{
					AddUniqueString(ExclusivityLinks, ConnectedPath);
				}
			}
		}

		TSharedPtr<FJsonObject> ParameterJson = MakeShared<FJsonObject>();
		ParameterJson->SetStringField(TEXT("name"), FinalParameterName);
		ParameterJson->SetStringField(
			TEXT("parameter_type"),
			MatchingRuntimeParameter && !MatchingRuntimeParameter->ParameterType.IsEmpty()
				? MatchingRuntimeParameter->ParameterType
				: DeriveParameterType(NodeClass, NodeTitle));
		ParameterJson->SetStringField(TEXT("group_type"), FirstStringField(Properties, {
			TEXT("GroupType"), TEXT("ParameterGroup"), TEXT("Group"), TEXT("GroupName")
		}));
		ParameterJson->SetStringField(TEXT("behavior"), DeriveParameterBehavior(NodeClass, NodeTitle, Properties));
		ParameterJson->SetStringField(TEXT("default_value"), FirstStringField(Properties, {
			TEXT("DefaultValue"), TEXT("CurrentDefaultValue"), TEXT("Value")
		}));
		ParameterJson->SetStringField(TEXT("default_option"), FirstStringField(Properties, {
			TEXT("DefaultOption"), TEXT("DefaultItem"), TEXT("DefaultState")
		}));
		ParameterJson->SetArrayField(TEXT("allowed_options"), ToJsonStringArray(AllowedOptions));
		ParameterJson->SetArrayField(TEXT("options"), ToJsonStringArray(AllowedOptions));
		ParameterJson->SetArrayField(TEXT("tags"), ToJsonStringArray(Tags));
		ParameterJson->SetArrayField(TEXT("population_tags"), ToJsonStringArray(PopulationTags));
		ParameterJson->SetArrayField(TEXT("owned_child_objects"), ToJsonStringArray(OwnedChildObjects));
		ParameterJson->SetArrayField(TEXT("compatibility_links"), ToJsonStringArray(CompatibilityLinks));
		ParameterJson->SetArrayField(TEXT("exclusivity_links"), ToJsonStringArray(ExclusivityLinks));
		ParameterJson->SetArrayField(TEXT("connected_nodes"), ConnectedNodeArray);
		ParameterJson->SetStringField(TEXT("source_node"), Pair.Key);
		ParameterJson->SetStringField(TEXT("source_node_class"), NodeClass);
		ParameterJson->SetStringField(TEXT("source_node_title"), NodeTitle);
		ParameterJson->SetStringField(TEXT("source"), MatchingRuntimeParameter ? TEXT("graph+runtime") : TEXT("graph"));
		if (MatchingRuntimeParameter)
		{
			ParameterJson->SetNumberField(TEXT("runtime_index"), MatchingRuntimeParameter->Index);
		}
		ParameterJson->SetObjectField(TEXT("raw_properties"), Properties);
		Parameters.Add(MakeShared<FJsonValueObject>(ParameterJson));
	}

	for (const FRuntimeMutableParameter& RuntimeParameter : RuntimeParameters)
	{
		if (ConsumedRuntimeParameterIndexes.Contains(RuntimeParameter.Index))
		{
			continue;
		}

		TSharedPtr<FJsonObject> ParameterJson = MakeShared<FJsonObject>();
		ParameterJson->SetStringField(TEXT("name"), RuntimeParameter.Name);
		ParameterJson->SetStringField(
			TEXT("parameter_type"),
			RuntimeParameter.ParameterType.IsEmpty() ? TEXT("unknown") : RuntimeParameter.ParameterType);
		ParameterJson->SetStringField(TEXT("group_type"), TEXT(""));
		ParameterJson->SetStringField(TEXT("behavior"), TEXT("select"));
		ParameterJson->SetStringField(TEXT("default_value"), TEXT(""));
		ParameterJson->SetStringField(TEXT("default_option"), TEXT(""));
		ParameterJson->SetArrayField(TEXT("allowed_options"), ToJsonStringArray(RuntimeParameter.Options));
		ParameterJson->SetArrayField(TEXT("options"), ToJsonStringArray(RuntimeParameter.Options));
		ParameterJson->SetArrayField(TEXT("tags"), {});
		ParameterJson->SetArrayField(TEXT("population_tags"), {});
		ParameterJson->SetArrayField(TEXT("owned_child_objects"), {});
		ParameterJson->SetArrayField(TEXT("compatibility_links"), {});
		ParameterJson->SetArrayField(TEXT("exclusivity_links"), {});
		ParameterJson->SetArrayField(TEXT("connected_nodes"), {});
		ParameterJson->SetStringField(TEXT("source"), TEXT("runtime"));
		ParameterJson->SetNumberField(TEXT("runtime_index"), RuntimeParameter.Index);
		Parameters.Add(MakeShared<FJsonValueObject>(ParameterJson));
	}

	TSharedPtr<FJsonObject> Result = BuildBaseResult(Context);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("available"), true);
	Result->SetArrayField(TEXT("parameters"), Parameters);
	TSharedPtr<FJsonObject> Summary = BuildGraphSummaryJson(GraphData);
	Summary->SetNumberField(TEXT("parameter_count"), Parameters.Num());
	Summary->SetNumberField(TEXT("graph_parameter_count"), GraphData.ParameterNodeCount);
	Summary->SetNumberField(TEXT("runtime_parameter_count"), RuntimeParameters.Num());
	Result->SetObjectField(TEXT("summary"), Summary);
	return Result;
}

TSharedPtr<FJsonObject> MutableIntrospectionUtils::BuildMutableDiagnosticsResult(const FString& AssetPath)
{
	const FMutableAssetContext Context = ResolveAssetContext(AssetPath);
	TSharedPtr<FJsonObject> Result = BuildBaseResult(Context);
	Result->SetArrayField(TEXT("detected_signals"), {});

	if (!Context.bAssetFound)
	{
		Result->SetStringField(TEXT("status"), TEXT("asset_not_found"));
		Result->SetBoolField(TEXT("available"), false);
		return Result;
	}

	const bool bAvailable = Context.bLooksLikeMutableAsset && Context.bAssetLoaded;
	Result->SetBoolField(TEXT("available"), bAvailable);
	Result->SetStringField(TEXT("status"), bAvailable ? TEXT("ok") : TEXT("limited"));

	FGraphInspectionData GraphData;
	if (bAvailable)
	{
		GraphData = InspectGraphs(Context.AssetObject, false);
		Result->SetObjectField(TEXT("graph_summary"), BuildGraphSummaryJson(GraphData));
	}

	TArray<TSharedPtr<FJsonValue>> Signals;
	TSharedPtr<FJsonObject> AssetSignals = Context.bAssetLoaded
		? CollectInterestingObjectProperties(Context.AssetObject, false)
		: MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : AssetSignals->Values)
	{
		TSharedPtr<FJsonObject> SignalJson = MakeShared<FJsonObject>();
		SignalJson->SetStringField(TEXT("property"), Pair.Key);
		SignalJson->SetStringField(TEXT("value"), JsonValueToFlatString(Pair.Value));
		SignalJson->SetStringField(TEXT("owner"), Context.bAssetLoaded ? Context.AssetObject->GetPathName() : Context.AssetPath);
		Signals.Add(MakeShared<FJsonValueObject>(SignalJson));
	}
	Result->SetArrayField(TEXT("detected_signals"), Signals);

	auto MakeCapability = [](const FString& Status, const FString& Reason) {
		TSharedPtr<FJsonObject> Capability = MakeShared<FJsonObject>();
		Capability->SetStringField(TEXT("status"), Status);
		Capability->SetStringField(TEXT("reason"), Reason);
		return Capability;
	};

	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(
		TEXT("projector_support"),
		bAvailable && GraphData.ProjectorNodeCount > 0
			? MakeCapability(TEXT("detected"), TEXT("Projector-like nodes were found in the asset graph."))
			: MakeCapability(TEXT("unknown"), TEXT("No projector support signal could be derived without Mutable-specific runtime APIs.")));
	Capabilities->SetObjectField(
		TEXT("clothing_enablement"),
		AssetSignals->Values.Contains(TEXT("Cloth")) || AssetSignals->Values.Contains(TEXT("Clothing"))
			? MakeCapability(TEXT("detected"), TEXT("Found cloth-related reflected properties on the asset."))
			: MakeCapability(TEXT("unknown"), TEXT("No cloth-related reflected properties were detected.")));
	Capabilities->SetObjectField(
		TEXT("physics_asset_merge"),
		MakeCapability(TEXT("unknown"), TEXT("Physics-asset merge support could not be derived without Mutable-specific runtime APIs.")));
	Capabilities->SetObjectField(
		TEXT("animation_bp_physics_asset_manipulation"),
		MakeCapability(TEXT("unknown"), TEXT("Animation Blueprint physics-asset manipulation could not be derived generically.")));
	Capabilities->SetObjectField(
		TEXT("lod_settings"),
		MakeCapability(TEXT("unknown"), TEXT("LOD configuration was not exposed through generic reflected properties for this asset.")));
	Capabilities->SetObjectField(
		TEXT("working_memory"),
		MakeCapability(TEXT("unknown"), TEXT("Working-memory limits require Mutable-specific runtime/system APIs.")));
	Capabilities->SetObjectField(
		TEXT("generated_instance_limit"),
		MakeCapability(TEXT("unknown"), TEXT("Generated instance limits require Mutable-specific runtime/system APIs.")));
	Capabilities->SetObjectField(
		TEXT("nanite_support"),
		MakeCapability(TEXT("unknown"), TEXT("Nanite compatibility could not be asserted generically for this engine/plugin combination.")));
	Result->SetObjectField(TEXT("capabilities"), Capabilities);

	return Result;
}
