// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "ClothTools.generated.h"

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothQueryTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-query"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothChaosQueryTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-chaos-query"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothConvertTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-convert"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothChaosStitchTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-chaos-stitch"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothChaosSetConfigTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-chaos-set-config"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothCreateTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-create"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothBindTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-bind"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothSetConfigTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-set-config"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothApplyWeightMapTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-apply-weightmap"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UClothSetCollisionTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("cloth-set-collision"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};
