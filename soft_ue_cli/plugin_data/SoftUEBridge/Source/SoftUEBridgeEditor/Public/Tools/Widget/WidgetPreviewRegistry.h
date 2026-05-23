// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UUserWidget;
class UWorld;

class SOFTUEBRIDGEEDITOR_API FWidgetPreviewRegistry
{
public:
	static FString MakeHandle(const FString& WidgetClassPath, UWorld* World);
	static void RegisterPreview(UWorld* World, UUserWidget* Widget, const FString& Handle);
	static int32 RemovePreviewsForWorld(UWorld* World, TArray<FString>* OutRemovedHandles = nullptr);
	static int32 CountPreviewsForWorld(UWorld* World);
};
