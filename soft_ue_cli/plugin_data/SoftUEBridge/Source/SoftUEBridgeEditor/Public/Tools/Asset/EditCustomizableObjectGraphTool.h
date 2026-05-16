// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "EditCustomizableObjectGraphTool.generated.h"

UCLASS()
class SOFTUEBRIDGEEDITOR_API UAddCustomizableObjectNodeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("add-customizable-object-node"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API USetCustomizableObjectNodePropertyTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("set-customizable-object-node-property"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UConnectCustomizableObjectPinsTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("connect-customizable-object-pins"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API URegenerateCustomizableObjectNodePinsTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("regenerate-customizable-object-node-pins"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UCompileCustomizableObjectTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("compile-customizable-object"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UCreateCustomizableObjectFromSpecTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("create-customizable-object-from-spec"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API URemoveCustomizableObjectNodeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("remove-customizable-object-node"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};

UCLASS()
class SOFTUEBRIDGEEDITOR_API UWireCustomizableObjectSlotFromTableTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("wire-customizable-object-slot-from-table"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};
