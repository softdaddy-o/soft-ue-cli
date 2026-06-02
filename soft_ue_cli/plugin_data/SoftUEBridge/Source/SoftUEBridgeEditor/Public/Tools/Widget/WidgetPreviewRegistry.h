// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UUserWidget;
class UWorld;

struct SOFTUEBRIDGEEDITOR_API FWidgetPreviewSummary
{
	FString Handle;
	FString WorldName;
	FString WidgetName;
	FString WidgetClass;
};

class SOFTUEBRIDGEEDITOR_API FWidgetPreviewRegistry
{
public:
	static FString MakeHandle(const FString& WidgetClassPath, UWorld* World);
	static void RegisterPreview(UWorld* World, UUserWidget* Widget, const FString& Handle);
	static int32 RemovePreviewsForWorld(UWorld* World, TArray<FString>* OutRemovedHandles = nullptr);
	static int32 RemovePreviewByHandle(const FString& Handle, TArray<FString>* OutRemovedHandles = nullptr);
	static int32 CountPreviewsForWorld(UWorld* World);
	static void CollectPreviewWidgetsForWorld(UWorld* World, TArray<UUserWidget*>& OutWidgets);
	static void ListPreviewsForWorld(UWorld* World, TArray<FWidgetPreviewSummary>& OutSummaries);
};
