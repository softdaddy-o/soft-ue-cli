// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Unified property serialization/deserialization utility.
 * Consolidates duplicate PropertyToJson implementations across tools.
 *
 * Design Policy: Prefer generalization over hardcoding.
 * Uses UE reflection to handle any property type dynamically.
 */
class SOFTUEBRIDGEEDITOR_API FBridgePropertySerializer
{
public:
	/**
	 * Serialize a property value to JSON.
	 * Handles all FProperty types including TMap, TSet, TArray, structs, objects.
	 *
	 * @param Property The property to serialize
	 * @param Container Pointer to the container (object) holding the property
	 * @param Owner Optional owner object for context
	 * @param Depth Current recursion depth (for nested objects)
	 * @param MaxDepth Maximum recursion depth (default 3)
	 * @return JSON value representing the property, or nullptr if failed
	 */
	static TSharedPtr<FJsonValue> SerializePropertyValue(
		FProperty* Property,
		const void* Container,
		UObject* Owner = nullptr,
		int32 Depth = 0,
		int32 MaxDepth = 3);

	/**
	 * Serialize a property with metadata to a JSON object.
	 * Includes name, type, category, and value.
	 *
	 * @param Property The property to serialize
	 * @param Container Pointer to the container holding the property
	 * @param Owner Optional owner object
	 * @param Depth Current recursion depth
	 * @param MaxDepth Maximum recursion depth
	 * @param bIncludeMetadata Whether to include property metadata
	 * @return JSON object with property info, or nullptr if failed
	 */
	static TSharedPtr<FJsonObject> SerializeProperty(
		FProperty* Property,
		const void* Container,
		UObject* Owner = nullptr,
		int32 Depth = 0,
		int32 MaxDepth = 3,
		bool bIncludeMetadata = true);

	/**
	 * Serialize all properties of a UObject to JSON.
	 *
	 * @param Object The object to serialize
	 * @param Depth Current recursion depth
	 * @param MaxDepth Maximum recursion depth
	 * @param RequiredFlags Only include properties with these flags (0 = any)
	 * @param ExcludeFlags Exclude properties with these flags
	 * @return JSON object with all properties
	 */
	static TSharedPtr<FJsonObject> SerializeUObjectProperties(
		UObject* Object,
		int32 Depth = 0,
		int32 MaxDepth = 3,
		EPropertyFlags RequiredFlags = CPF_None,
		EPropertyFlags ExcludeFlags = CPF_None);

	/**
	 * Deserialize a JSON value to a property.
	 * Handles all FProperty types including TMap, TSet, TArray, structs, objects.
	 *
	 * @param Property The property to set
	 * @param Container Pointer to the container holding the property
	 * @param Value The JSON value to deserialize
	 * @param OutError Error message if deserialization fails
	 * @return true if successful
	 */
	static bool DeserializePropertyValue(
		FProperty* Property,
		void* Container,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	/**
	 * Get a human-readable type string for a property.
	 *
	 * @param Property The property
	 * @return Type string (e.g., "int32", "TArray<FString>", "TMap<FString, int32>")
	 */
	static FString GetPropertyTypeString(FProperty* Property);

	/**
	 * Resolve a UClass by name.
	 * Supports short names (e.g., "Actor"), full paths (e.g., "/Script/Engine.Actor"),
	 * and Blueprint class paths (e.g., "/Game/BP_MyClass.BP_MyClass_C").
	 *
	 * @param ClassName The class name to resolve
	 * @param OutError Error message if resolution fails
	 * @return The resolved class, or nullptr if not found
	 */
	static UClass* ResolveClass(const FString& ClassName, FString& OutError);

	/**
	 * Resolve a UClass and verify it's a subclass of the expected type.
	 *
	 * @param ClassName The class name to resolve
	 * @param ExpectedBase The expected base class
	 * @param OutError Error message if resolution fails
	 * @return The resolved class, or nullptr if not found or not a valid subclass
	 */
	template<typename T>
	static UClass* ResolveClassOfType(const FString& ClassName, FString& OutError)
	{
		UClass* ResolvedClass = ResolveClass(ClassName, OutError);
		if (ResolvedClass && !ResolvedClass->IsChildOf<T>())
		{
			OutError = FString::Printf(TEXT("Class '%s' is not a subclass of %s"), *ClassName, *T::StaticClass()->GetName());
			return nullptr;
		}
		return ResolvedClass;
	}

private:
	// Helper to serialize array properties
	static TSharedPtr<FJsonValue> SerializeArrayProperty(
		FArrayProperty* ArrayProp,
		const void* Container,
		UObject* Owner,
		int32 Depth,
		int32 MaxDepth);

	// Helper to serialize map properties
	static TSharedPtr<FJsonValue> SerializeMapProperty(
		FMapProperty* MapProp,
		const void* Container,
		UObject* Owner,
		int32 Depth,
		int32 MaxDepth);

	// Helper to serialize set properties
	static TSharedPtr<FJsonValue> SerializeSetProperty(
		FSetProperty* SetProp,
		const void* Container,
		UObject* Owner,
		int32 Depth,
		int32 MaxDepth);

	// Helper to serialize struct properties
	static TSharedPtr<FJsonValue> SerializeStructProperty(
		FStructProperty* StructProp,
		const void* Container,
		UObject* Owner,
		int32 Depth,
		int32 MaxDepth);

	// Helper to serialize object properties
	static TSharedPtr<FJsonValue> SerializeObjectProperty(
		FObjectProperty* ObjectProp,
		const void* Container,
		int32 Depth,
		int32 MaxDepth);

	// Helper to deserialize array properties
	static bool DeserializeArrayProperty(
		FArrayProperty* ArrayProp,
		void* Container,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	// Helper to deserialize map properties
	static bool DeserializeMapProperty(
		FMapProperty* MapProp,
		void* Container,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	// Helper to deserialize set properties
	static bool DeserializeSetProperty(
		FSetProperty* SetProp,
		void* Container,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	// Helper to deserialize object properties
	static bool DeserializeObjectProperty(
		FObjectProperty* ObjectProp,
		void* Container,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);
};
