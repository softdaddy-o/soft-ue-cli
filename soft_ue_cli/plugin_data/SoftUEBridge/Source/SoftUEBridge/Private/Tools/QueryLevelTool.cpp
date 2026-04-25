// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/QueryLevelTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

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
	S.Add(TEXT("class_filter"),      Prop(TEXT("string"),  TEXT("Filter by class name (wildcards supported)")));
	S.Add(TEXT("tag_filter"),        Prop(TEXT("string"),  TEXT("Filter by actor tag")));
	S.Add(TEXT("search"),            Prop(TEXT("string"),  TEXT("Filter by name/label substring")));
	S.Add(TEXT("include_components"),Prop(TEXT("boolean"), TEXT("Include component list (default: false)")));
	S.Add(TEXT("include_transform"), Prop(TEXT("boolean"), TEXT("Include transforms (default: true)")));
	S.Add(TEXT("include_hidden"),    Prop(TEXT("boolean"), TEXT("Include hidden actors (default: false)")));
	S.Add(TEXT("include_properties"),Prop(TEXT("boolean"), TEXT("Include actor and component properties (default: false). Automatically enables component inclusion.")));
	S.Add(TEXT("property_filter"),   Prop(TEXT("string"),  TEXT("Filter properties by name (wildcards supported, e.g., '*Health*'). Only used when include_properties is true.")));
	S.Add(TEXT("limit"),             Prop(TEXT("integer"), TEXT("Max results (default: 100)")));
	S.Add(TEXT("world"),             Prop(TEXT("string"),  TEXT("World context: 'editor' (editor scene), 'pie' (Play-In-Editor), 'game' (packaged build only). Omit to use the first available world.")));

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

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		if (!bHidden && Actor->IsHidden()) continue;

		if (!ClassFilter.IsEmpty() && !MatchesWildcard(Actor->GetClass()->GetName(), ClassFilter)) continue;

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
