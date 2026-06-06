// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/SkeletalMeshSocketTool.h"

#include "Tools/Asset/AssetReferenceRepointUtils.h"
#include "Utils/BridgeAssetModifier.h"

#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty SkeletalSocketSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

bool ReadVector(
	const TSharedPtr<FJsonObject>& Arguments,
	const FString& FieldName,
	const FVector& DefaultValue,
	FVector& OutValue,
	FString& OutError)
{
	OutValue = DefaultValue;
	if (!Arguments.IsValid() || !Arguments->HasField(FieldName))
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Arguments->TryGetArrayField(FieldName, Values) || !Values || Values->Num() != 3)
	{
		OutError = FString::Printf(TEXT("skeletal socket: %s must be an array [x,y,z]"), *FieldName);
		return false;
	}

	OutValue = FVector(
		static_cast<double>((*Values)[0]->AsNumber()),
		static_cast<double>((*Values)[1]->AsNumber()),
		static_cast<double>((*Values)[2]->AsNumber()));
	return true;
}

FRotator VectorToRotator(const FVector& Value)
{
	return FRotator(Value.X, Value.Y, Value.Z);
}

TArray<TSharedPtr<FJsonValue>> VectorToJsonArray(const FVector& Value)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Add(MakeShared<FJsonValueNumber>(Value.X));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Y));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Z));
	return Result;
}

TArray<TSharedPtr<FJsonValue>> RotatorToJsonArray(const FRotator& Value)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Add(MakeShared<FJsonValueNumber>(Value.Pitch));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Yaw));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Roll));
	return Result;
}

USkeletalMesh* LoadSkeletalMesh(const FString& AssetPath, FString& OutError)
{
	USkeletalMesh* Mesh = FBridgeAssetModifier::LoadAssetByPath<USkeletalMesh>(AssetPath, OutError);
	if (!Mesh)
	{
		return nullptr;
	}
	return Mesh;
}

bool ValidateBoneName(USkeletalMesh* Mesh, const FName& BoneName, FString& OutError)
{
	if (!Mesh || BoneName.IsNone())
	{
		OutError = TEXT("skeletal socket: bone_name is required");
		return false;
	}
	if (Mesh->GetRefSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("skeletal socket: bone '%s' does not exist on mesh"), *BoneName.ToString());
		return false;
	}
	return true;
}

USkeletalMeshSocket* FindMeshOnlySocket(USkeletalMesh* Mesh, const FName& SocketName, int32* OutIndex = nullptr)
{
	if (OutIndex)
	{
		*OutIndex = INDEX_NONE;
	}
	if (!Mesh || SocketName.IsNone())
	{
		return nullptr;
	}

	TArray<TObjectPtr<USkeletalMeshSocket>>& Sockets = Mesh->GetMeshOnlySocketList();
	for (int32 Index = 0; Index < Sockets.Num(); ++Index)
	{
		USkeletalMeshSocket* Socket = Sockets[Index];
		if (Socket && Socket->SocketName == SocketName)
		{
			if (OutIndex)
			{
				*OutIndex = Index;
			}
			return Socket;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> SocketToJson(USkeletalMeshSocket* Socket)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!Socket)
	{
		return Json;
	}

	Json->SetStringField(TEXT("socket_name"), Socket->SocketName.ToString());
	Json->SetStringField(TEXT("bone_name"), Socket->BoneName.ToString());
	Json->SetArrayField(TEXT("location"), VectorToJsonArray(Socket->RelativeLocation));
	Json->SetArrayField(TEXT("rotation"), RotatorToJsonArray(Socket->RelativeRotation));
	Json->SetArrayField(TEXT("scale"), VectorToJsonArray(Socket->RelativeScale));
	return Json;
}

void RemoveSocket(USkeletalMesh* Mesh, int32 SocketIndex)
{
	if (Mesh && SocketIndex != INDEX_NONE)
	{
		Mesh->GetMeshOnlySocketList().RemoveAt(SocketIndex);
	}
}
}

FString USkeletalMeshSocketCreateTool::GetToolDescription() const
{
	return TEXT("Create or update a mesh-owned SkeletalMesh socket by directly assigning socket and bone names.");
}

TMap<FString, FBridgeSchemaProperty> USkeletalMeshSocketCreateTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), SkeletalSocketSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("socket_name"), SkeletalSocketSchemaProperty(TEXT("string"), TEXT("Socket name"), true));
	Schema.Add(TEXT("bone_name"), SkeletalSocketSchemaProperty(TEXT("string"), TEXT("Bone name to attach the socket to"), true));
	Schema.Add(TEXT("location"), SkeletalSocketSchemaProperty(TEXT("array"), TEXT("Relative location [x,y,z]")));
	Schema.Add(TEXT("rotation"), SkeletalSocketSchemaProperty(TEXT("array"), TEXT("Relative rotation [pitch,yaw,roll]")));
	Schema.Add(TEXT("scale"), SkeletalSocketSchemaProperty(TEXT("array"), TEXT("Relative scale [x,y,z]")));
	Schema.Add(TEXT("save"), SkeletalSocketSchemaProperty(TEXT("boolean"), TEXT("Save the mesh after mutation")));
	Schema.Add(TEXT("checkout"), SkeletalSocketSchemaProperty(TEXT("boolean"), TEXT("Checkout the mesh before mutation")));
	return Schema;
}

TArray<FString> USkeletalMeshSocketCreateTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("socket_name"), TEXT("bone_name") };
}

FBridgeToolResult USkeletalMeshSocketCreateTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	const FString SocketNameText = GetStringArgOrDefault(Arguments, TEXT("socket_name"), TEXT(""));
	const FString BoneNameText = GetStringArgOrDefault(Arguments, TEXT("bone_name"), TEXT(""));
	if (AssetPath.IsEmpty() || SocketNameText.IsEmpty() || BoneNameText.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("skeletal-mesh-socket-create: asset_path, socket_name, and bone_name are required"));
	}

	FString Error;
	USkeletalMesh* Mesh = LoadSkeletalMesh(AssetPath, Error);
	if (!Mesh)
	{
		return FBridgeToolResult::Error(Error);
	}

	const FName SocketName(*SocketNameText.TrimStartAndEnd());
	const FName BoneName(*BoneNameText.TrimStartAndEnd());
	if (SocketName.IsNone())
	{
		return FBridgeToolResult::Error(TEXT("skeletal-mesh-socket-create: socket_name is required"));
	}
	if (!ValidateBoneName(Mesh, BoneName, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	FVector Location = FVector::ZeroVector;
	FVector RotationVector = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	if (!ReadVector(Arguments, TEXT("location"), FVector::ZeroVector, Location, Error)
		|| !ReadVector(Arguments, TEXT("rotation"), FVector::ZeroVector, RotationVector, Error)
		|| !ReadVector(Arguments, TEXT("scale"), FVector::OneVector, Scale, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	if (!SoftUE::AssetReferenceRepointUtils::CheckoutObjectPackageIfRequested(Mesh, bCheckout, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Create Skeletal Mesh Socket")));

	USkeletalMeshSocket* Socket = FindMeshOnlySocket(Mesh, SocketName);
	const bool bCreated = Socket == nullptr;
	if (!Socket)
	{
		Socket = NewObject<USkeletalMeshSocket>(Mesh);
		Socket->SocketName = SocketName;
		Socket->BoneName = BoneName;
		Mesh->AddSocket(Socket, false);
		Socket = FindMeshOnlySocket(Mesh, SocketName);
		if (!Socket)
		{
			return FBridgeToolResult::Error(TEXT("skeletal-mesh-socket-create: failed to add mesh socket"));
		}
	}

	FBridgeAssetModifier::MarkModified(Mesh);
	Socket->Modify();
	Socket->SocketName = SocketName;
	Socket->BoneName = BoneName;
	Socket->RelativeLocation = Location;
	Socket->RelativeRotation = VectorToRotator(RotationVector);
	Socket->RelativeScale = Scale;
	Mesh->PostEditChange();
	FBridgeAssetModifier::MarkPackageDirty(Mesh);

	if (bSave && !FBridgeAssetModifier::SaveAsset(Mesh, false, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Mesh->GetPathName());
	Result->SetBoolField(TEXT("created"), bCreated);
	Result->SetBoolField(TEXT("updated"), !bCreated);
	Result->SetObjectField(TEXT("socket"), SocketToJson(Socket));
	Result->SetBoolField(TEXT("saved"), bSave);
	return FBridgeToolResult::Json(Result);
}

FString USkeletalMeshSocketRemoveTool::GetToolDescription() const
{
	return TEXT("Remove a mesh-owned SkeletalMesh socket by name.");
}

TMap<FString, FBridgeSchemaProperty> USkeletalMeshSocketRemoveTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), SkeletalSocketSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("socket_name"), SkeletalSocketSchemaProperty(TEXT("string"), TEXT("Socket name"), true));
	Schema.Add(TEXT("save"), SkeletalSocketSchemaProperty(TEXT("boolean"), TEXT("Save the mesh after mutation")));
	Schema.Add(TEXT("checkout"), SkeletalSocketSchemaProperty(TEXT("boolean"), TEXT("Checkout the mesh before mutation")));
	return Schema;
}

TArray<FString> USkeletalMeshSocketRemoveTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("socket_name") };
}

FBridgeToolResult USkeletalMeshSocketRemoveTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	const FString SocketNameText = GetStringArgOrDefault(Arguments, TEXT("socket_name"), TEXT(""));
	if (AssetPath.IsEmpty() || SocketNameText.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("skeletal-mesh-socket-remove: asset_path and socket_name are required"));
	}

	FString Error;
	USkeletalMesh* Mesh = LoadSkeletalMesh(AssetPath, Error);
	if (!Mesh)
	{
		return FBridgeToolResult::Error(Error);
	}

	const FName SocketName(*SocketNameText.TrimStartAndEnd());
	if (SocketName.IsNone())
	{
		return FBridgeToolResult::Error(TEXT("skeletal-mesh-socket-remove: socket_name is required"));
	}
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	if (!SoftUE::AssetReferenceRepointUtils::CheckoutObjectPackageIfRequested(Mesh, bCheckout, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	int32 SocketIndex = INDEX_NONE;
	USkeletalMeshSocket* Socket = FindMeshOnlySocket(Mesh, SocketName, &SocketIndex);
	if (!Socket || SocketIndex == INDEX_NONE)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("skeletal-mesh-socket-remove: mesh-owned socket '%s' not found"),
			*SocketName.ToString()));
	}

	TSharedPtr<FJsonObject> RemovedSocket = SocketToJson(Socket);
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Remove Skeletal Mesh Socket")));

	FBridgeAssetModifier::MarkModified(Mesh);
	Socket->Modify();
	RemoveSocket(Mesh, SocketIndex);
	Mesh->PostEditChange();
	FBridgeAssetModifier::MarkPackageDirty(Mesh);

	if (bSave && !FBridgeAssetModifier::SaveAsset(Mesh, false, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Mesh->GetPathName());
	Result->SetObjectField(TEXT("removed_socket"), RemovedSocket);
	Result->SetBoolField(TEXT("saved"), bSave);
	return FBridgeToolResult::Json(Result);
}
