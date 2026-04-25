// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/BridgeToolResult.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FBridgeToolResult FBridgeToolResult::Text(const FString& InText)
{
	FBridgeToolResult Result;
	Result.bIsError = false;

	TSharedPtr<FJsonObject> ContentItem = MakeShareable(new FJsonObject);
	ContentItem->SetStringField(TEXT("type"), TEXT("text"));
	ContentItem->SetStringField(TEXT("text"), InText);
	Result.Content.Add(ContentItem);

	return Result;
}

FBridgeToolResult FBridgeToolResult::Json(TSharedPtr<FJsonObject> JsonContent)
{
	FString JsonString;
	if (JsonContent.IsValid())
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(JsonContent.ToSharedRef(), Writer);
	}
	return Text(JsonString);
}

FBridgeToolResult FBridgeToolResult::Error(const FString& Message)
{
	FBridgeToolResult Result;
	Result.bIsError = true;

	TSharedPtr<FJsonObject> ContentItem = MakeShareable(new FJsonObject);
	ContentItem->SetStringField(TEXT("type"), TEXT("text"));
	ContentItem->SetStringField(TEXT("text"), Message);
	Result.Content.Add(ContentItem);

	return Result;
}

TSharedPtr<FJsonObject> FBridgeToolResult::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);

	TArray<TSharedPtr<FJsonValue>> ContentArray;
	for (const TSharedPtr<FJsonObject>& Item : Content)
	{
		ContentArray.Add(MakeShareable(new FJsonValueObject(Item)));
	}
	Root->SetArrayField(TEXT("content"), ContentArray);
	Root->SetBoolField(TEXT("isError"), bIsError);

	return Root;
}
