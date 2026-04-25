// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/QueryLevelTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#endif

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(UQueryLevelTool)
#endif

FString UQueryLevelTool::GetToolDescription() const
{
	return TEXT("List and inspect actors in the current game world. "
		"Optionally filter by class, tag, or name pattern. "
		"Use actor_name to get detailed info about a specific actor. "
		"Use include_properties to inspect actor and component property values.");
}

TMap<FString, FBridgeSchemaProperty> UQueryLevelTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> S;

	auto Prop = [](const FString& Type, const FString& Desc) {
		FBridgeSchemaProperty P;
		P.Type = Type;
		P.Description = Desc;
		return P;
	};

	S.Add(TEXT("actor_name"),        Prop(TEXT("string"),  TEXT("Find actor by name or label (wildcards: *pattern*)")));
	S.Add(TEXT("class_filter"),      Prop(TEXT("string"),  TEXT("Filter by class name (matches inherited classes; wildcards supported)")));
	S.Add(TEXT("tag_filter"),        Prop(TEXT("string"),  TEXT("Filter by actor tag")));
	S.Add(TEXT("search"),            Prop(TEXT("string"),  TEXT("Filter by name/label substring")));
	S.Add(TEXT("include_components"),Prop(TEXT("boolean"), TEXT("Include component list (default: false)")));
	S.Add(TEXT("include_transform"), Prop(TEXT("boolean"), TEXT("Include transforms (default: true)")));
	S.Add(TEXT("include_hidden"),    Prop(TEXT("boolean"), TEXT("Include hidden actors (default: false)")));
	S.Add(TEXT("include_properties"),Prop(TEXT("boolean"), TEXT("Include actor and component properties (default: false). Automatically enables component inclusion.")));
	S.Add(TEXT("property_filter"),   Prop(TEXT("string"),  TEXT("Filter properties by name (wildcards supported, e.g., '*Health*'). Only used when include_properties is true.")));
	S.Add(TEXT("limit"),             Prop(TEXT("integer"), TEXT("Max results (default: 100)")));
	S.Add(TEXT("world"),             Prop(TEXT("string"),  TEXT("World context: 'editor' (editor scene), 'pie' (Play-In-Editor), 'game' (packaged build only). Omit to use the first available world.")));
	S.Add(TEXT("include_foliage"),   Prop(TEXT("boolean"), TEXT("Include FoliageType instance counts from InstancedFoliageActors (default: false)")));
	S.Add(TEXT("include_grass"),     Prop(TEXT("boolean"), TEXT("Include LandscapeProxy grass/material info (default: false)")));

	return S;
}

FBridgeToolResult UQueryLevelTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	UWorld* World = FindWorldByType(GetStringArgOrDefault(Args, TEXT("world")));
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No world available. Specify 'world': 'editor', 'pie', or 'game'."));
	}

	const FString ActorName    = GetStringArgOrDefault(Args, TEXT("actor_name"));
	const FString ClassFilter  = GetStringArgOrDefault(Args, TEXT("class_filter"));
	const FString TagFilter    = GetStringArgOrDefault(Args, TEXT("tag_filter"));
	const FString SearchFilter = GetStringArgOrDefault(Args, TEXT("search"));
	const bool bProperties     = GetBoolArgOrDefault(Args, TEXT("include_properties"), false);
	const bool bComponents     = GetBoolArgOrDefault(Args, TEXT("include_components"), false) || bProperties;
	const bool bTransform      = GetBoolArgOrDefault(Args, TEXT("include_transform"), true);
	const bool bHidden         = GetBoolArgOrDefault(Args, TEXT("include_hidden"), false);
	const FString PropertyFilter = GetStringArgOrDefault(Args, TEXT("property_filter"));
	const int32 Limit          = GetIntArgOrDefault(Args, TEXT("limit"), 100);
	const bool bFoliage        = GetBoolArgOrDefault(Args, TEXT("include_foliage"), false);
	const bool bGrass          = GetBoolArgOrDefault(Args, TEXT("include_grass"), false);

	// Detail mode: find one specific actor
	if (!ActorName.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			if (MatchesWildcard(Actor->GetName(), ActorName) ||
				MatchesWildcard(GetActorLabelSafe(Actor), ActorName))
			{
				return FBridgeToolResult::Json(ActorToJson(Actor, true, true, bProperties, PropertyFilter));
			}
		}
		return FBridgeToolResult::Error(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));
	}

	// List mode
	TArray<TSharedPtr<FJsonValue>> ActorsArr;
	bool bLimitReached = false;

	// Resolve class filter: try to find as UClass for inheritance matching,
	// fall back to wildcard name matching if not found or contains wildcards
	UClass* FilterClass = nullptr;
	if (!ClassFilter.IsEmpty() && !ClassFilter.Contains(TEXT("*")) && !ClassFilter.Contains(TEXT("?")))
	{
		FilterClass = FindFirstObjectSafe<UClass>(*ClassFilter);
		if (!FilterClass)
		{
			FilterClass = FindFirstObjectSafe<UClass>(*(TEXT("A") + ClassFilter));
		}
		if (!FilterClass)
		{
			FilterClass = FindFirstObjectSafe<UClass>(*(TEXT("U") + ClassFilter));
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		if (!bHidden && Actor->IsHidden()) continue;

		if (!ClassFilter.IsEmpty())
		{
			if (FilterClass)
			{
				if (!Actor->GetClass()->IsChildOf(FilterClass)) continue;
			}
			else
			{
				if (!MatchesWildcard(Actor->GetClass()->GetName(), ClassFilter)) continue;
			}
		}

		if (!TagFilter.IsEmpty())
		{
			bool bTagMatch = false;
			for (const FName& Tag : Actor->Tags)
			{
				if (MatchesWildcard(Tag.ToString(), TagFilter)) { bTagMatch = true; break; }
			}
			if (!bTagMatch) continue;
		}

		if (!SearchFilter.IsEmpty() &&
			!MatchesWildcard(Actor->GetName(), SearchFilter) &&
			!MatchesWildcard(GetActorLabelSafe(Actor), SearchFilter))
		{
			continue;
		}

		if (ActorsArr.Num() >= Limit) { bLimitReached = true; break; }

		TSharedPtr<FJsonObject> ActorJson = ActorToJson(Actor, bComponents, bTransform, bProperties, PropertyFilter);
		ActorsArr.Add(MakeShareable(new FJsonValueObject(ActorJson)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetArrayField(TEXT("actors"), ActorsArr);
	Result->SetNumberField(TEXT("actor_count"), ActorsArr.Num());
	Result->SetBoolField(TEXT("limit_reached"), bLimitReached);

	if (bFoliage)
	{
		Result->SetObjectField(TEXT("foliage"), CollectFoliageInfo(World));
	}
	if (bGrass)
	{
		Result->SetObjectField(TEXT("landscape_grass"), CollectLandscapeGrassInfo(World));
	}

	return FBridgeToolResult::Json(Result);
}

TSharedPtr<FJsonObject> UQueryLevelTool::ActorToJson(AActor* Actor, bool bComponents, bool bTransform, bool bProperties, const FString& PropertyFilter) const
{
	TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
	J->SetStringField(TEXT("name"), Actor->GetName());
	J->SetStringField(TEXT("label"), GetActorLabelSafe(Actor));
	J->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	J->SetBoolField(TEXT("is_hidden"), Actor->IsHidden());

	if (bTransform)
	{
		J->SetObjectField(TEXT("transform"), TransformToJson(Actor->GetActorTransform()));
	}

	if (Actor->Tags.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const FName& Tag : Actor->Tags)
		{
			Tags.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
		}
		J->SetArrayField(TEXT("tags"), Tags);
	}

	// Actor-level properties
	if (bProperties)
	{
		J->SetArrayField(TEXT("properties"), CollectProperties(Actor, PropertyFilter));
	}

	if (bComponents)
	{
		TArray<TSharedPtr<FJsonValue>> Comps;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* C : Components)
		{
			if (!C) continue;
			TSharedPtr<FJsonObject> CJ = MakeShareable(new FJsonObject);
			CJ->SetStringField(TEXT("name"), C->GetName());
			CJ->SetStringField(TEXT("class"), C->GetClass()->GetName());
			CJ->SetBoolField(TEXT("is_active"), C->IsActive());

			// Component-level properties
			if (bProperties)
			{
				CJ->SetArrayField(TEXT("properties"), CollectProperties(C, PropertyFilter));
			}

			Comps.Add(MakeShareable(new FJsonValueObject(CJ)));
		}
		J->SetArrayField(TEXT("components"), Comps);
	}

	return J;
}

TArray<TSharedPtr<FJsonValue>> UQueryLevelTool::CollectProperties(UObject* Object, const FString& PropertyFilter) const
{
	TArray<TSharedPtr<FJsonValue>> PropsArr;
	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop) continue;

		if (!PropertyFilter.IsEmpty() && !MatchesWildcard(Prop->GetName(), PropertyFilter))
		{
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
		if (!ValuePtr) continue;

		TSharedPtr<FJsonObject> PropJson = PropertyToJson(Prop, ValuePtr, Object);
		if (PropJson.IsValid())
		{
			PropsArr.Add(MakeShareable(new FJsonValueObject(PropJson)));
		}
	}
	return PropsArr;
}

TSharedPtr<FJsonObject> UQueryLevelTool::TransformToJson(const FTransform& T) const
{
	auto Vec3 = [](const FVector& V) {
		TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
		J->SetNumberField(TEXT("x"), V.X);
		J->SetNumberField(TEXT("y"), V.Y);
		J->SetNumberField(TEXT("z"), V.Z);
		return J;
	};

	FRotator R = T.Rotator();
	TSharedPtr<FJsonObject> RotJ = MakeShareable(new FJsonObject);
	RotJ->SetNumberField(TEXT("pitch"), R.Pitch);
	RotJ->SetNumberField(TEXT("yaw"), R.Yaw);
	RotJ->SetNumberField(TEXT("roll"), R.Roll);

	TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
	J->SetObjectField(TEXT("location"), Vec3(T.GetLocation()));
	J->SetObjectField(TEXT("rotation"), RotJ);
	J->SetObjectField(TEXT("scale"), Vec3(T.GetScale3D()));
	return J;
}

TSharedPtr<FJsonObject> UQueryLevelTool::PropertyToJson(FProperty* Property, void* Container, UObject* Owner) const
{
	if (!Property || !Container)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropJson = MakeShareable(new FJsonObject);

	PropJson->SetStringField(TEXT("name"), Property->GetName());
	PropJson->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

	FString Value;
	Property->ExportText_Direct(Value, Container, Container, Owner, PPF_None);
	PropJson->SetStringField(TEXT("value"), Value);

	return PropJson;
}

FString UQueryLevelTool::GetPropertyTypeString(FProperty* Property) const
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

TSharedPtr<FJsonObject> UQueryLevelTool::CollectFoliageInfo(UWorld* World) const
{
	TSharedPtr<FJsonObject> FoliageJson = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> TypesArray;
	int32 TotalInstances = 0;

#if WITH_EDITOR
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		if (!IFA) continue;

		for (auto& Pair : IFA->GetFoliageInfos())
		{
			const UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& Info = *Pair.Value;

			if (!FoliageType) continue;

			TSharedPtr<FJsonObject> TypeJson = MakeShareable(new FJsonObject);
			TypeJson->SetStringField(TEXT("name"), FoliageType->GetName());
			TypeJson->SetStringField(TEXT("path"), FoliageType->GetPathName());

			int32 InstanceCount = Info.Instances.Num();
			TypeJson->SetNumberField(TEXT("instance_count"), InstanceCount);
			TotalInstances += InstanceCount;

			if (FoliageType->GetSource())
			{
				TypeJson->SetStringField(TEXT("source_mesh"), FoliageType->GetSource()->GetPathName());
			}

			TypesArray.Add(MakeShareable(new FJsonValueObject(TypeJson)));
		}
	}
#else
	FoliageJson->SetStringField(TEXT("note"), TEXT("Foliage data is only available in editor builds."));
#endif

	FoliageJson->SetArrayField(TEXT("foliage_types"), TypesArray);
	FoliageJson->SetNumberField(TEXT("type_count"), TypesArray.Num());
	FoliageJson->SetNumberField(TEXT("total_instances"), TotalInstances);

	return FoliageJson;
}

TSharedPtr<FJsonObject> UQueryLevelTool::CollectLandscapeGrassInfo(UWorld* World) const
{
	TSharedPtr<FJsonObject> GrassJson = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ProxiesArray;

#if WITH_EDITOR
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;
		if (!Proxy) continue;

		TSharedPtr<FJsonObject> ProxyJson = MakeShareable(new FJsonObject);
		ProxyJson->SetStringField(TEXT("name"), Proxy->GetName());
		ProxyJson->SetStringField(TEXT("label"), GetActorLabelSafe(Proxy));
		ProxyJson->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());
		ProxyJson->SetNumberField(TEXT("component_count"), Proxy->LandscapeComponents.Num());

		TSet<FString> SeenMaterials;
		TArray<TSharedPtr<FJsonValue>> MaterialsArray;
		for (ULandscapeComponent* Comp : Proxy->LandscapeComponents)
		{
			if (!Comp) continue;
			TArray<UMaterialInterface*> CompMaterials;
			Comp->GetUsedMaterials(CompMaterials);
			for (UMaterialInterface* Mat : CompMaterials)
			{
				if (Mat && !SeenMaterials.Contains(Mat->GetPathName()))
				{
					SeenMaterials.Add(Mat->GetPathName());
					MaterialsArray.Add(MakeShareable(new FJsonValueString(Mat->GetPathName())));
				}
			}
		}
		ProxyJson->SetArrayField(TEXT("materials"), MaterialsArray);

		ProxiesArray.Add(MakeShareable(new FJsonValueObject(ProxyJson)));
	}
#else
	GrassJson->SetStringField(TEXT("note"), TEXT("Landscape grass data is only available in editor builds."));
#endif

	GrassJson->SetArrayField(TEXT("landscape_proxies"), ProxiesArray);
	GrassJson->SetNumberField(TEXT("proxy_count"), ProxiesArray.Num());

	return GrassJson;
}
