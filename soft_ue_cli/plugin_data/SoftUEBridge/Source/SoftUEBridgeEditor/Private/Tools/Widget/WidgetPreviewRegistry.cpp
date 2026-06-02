// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Widget/WidgetPreviewRegistry.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Misc/Guid.h"

namespace
{
	struct FRegisteredWidgetPreview
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UUserWidget> Widget;
		FString Handle;
	};

	TArray<FRegisteredWidgetPreview>& GetWidgetPreviewRegistry()
	{
		static TArray<FRegisteredWidgetPreview> Registry;
		return Registry;
	}
}

FString FWidgetPreviewRegistry::MakeHandle(const FString& WidgetClassPath, UWorld* World)
{
	return FString::Printf(
		TEXT("softue-preview:%s:%s:%s"),
		*GetNameSafe(World),
		*WidgetClassPath,
		*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
}

void FWidgetPreviewRegistry::RegisterPreview(UWorld* World, UUserWidget* Widget, const FString& Handle)
{
	if (!World || !Widget)
	{
		return;
	}

	FRegisteredWidgetPreview Entry;
	Entry.World = World;
	Entry.Widget = Widget;
	Entry.Handle = Handle;
	GetWidgetPreviewRegistry().Add(Entry);
}

int32 FWidgetPreviewRegistry::RemovePreviewsForWorld(UWorld* World, TArray<FString>* OutRemovedHandles)
{
	TArray<FRegisteredWidgetPreview>& Registry = GetWidgetPreviewRegistry();
	int32 RemovedCount = 0;

	for (int32 Index = Registry.Num() - 1; Index >= 0; --Index)
	{
		FRegisteredWidgetPreview& Entry = Registry[Index];
		const bool bInvalidEntry = !Entry.World.IsValid() || !Entry.Widget.IsValid();
		const bool bMatchesWorld = World && Entry.World.Get() == World;
		if (!bInvalidEntry && !bMatchesWorld)
		{
			continue;
		}

		if (bMatchesWorld)
		{
			if (UUserWidget* Widget = Entry.Widget.Get())
			{
				Widget->RemoveFromParent();
			}
			if (OutRemovedHandles)
			{
				OutRemovedHandles->Add(Entry.Handle);
			}
			++RemovedCount;
		}

		Registry.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}

	return RemovedCount;
}

int32 FWidgetPreviewRegistry::RemovePreviewByHandle(const FString& Handle, TArray<FString>* OutRemovedHandles)
{
	if (Handle.IsEmpty())
	{
		return 0;
	}

	TArray<FRegisteredWidgetPreview>& Registry = GetWidgetPreviewRegistry();
	int32 RemovedCount = 0;

	for (int32 Index = Registry.Num() - 1; Index >= 0; --Index)
	{
		FRegisteredWidgetPreview& Entry = Registry[Index];
		if (!Entry.Handle.Equals(Handle, ESearchCase::CaseSensitive))
		{
			continue;
		}

		if (UUserWidget* Widget = Entry.Widget.Get())
		{
			Widget->RemoveFromParent();
		}
		if (OutRemovedHandles)
		{
			OutRemovedHandles->Add(Entry.Handle);
		}
		++RemovedCount;
		Registry.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}

	return RemovedCount;
}

int32 FWidgetPreviewRegistry::CountPreviewsForWorld(UWorld* World)
{
	if (!World)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FRegisteredWidgetPreview& Entry : GetWidgetPreviewRegistry())
	{
		if (Entry.World.Get() == World && Entry.Widget.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

void FWidgetPreviewRegistry::CollectPreviewWidgetsForWorld(UWorld* World, TArray<UUserWidget*>& OutWidgets)
{
	if (!World)
	{
		return;
	}

	for (const FRegisteredWidgetPreview& Entry : GetWidgetPreviewRegistry())
	{
		UWorld* EntryWorld = Entry.World.Get();
		UUserWidget* Widget = Entry.Widget.Get();
		if (!EntryWorld || !Widget || EntryWorld != World)
		{
			continue;
		}
		if (!OutWidgets.Contains(Widget))
		{
			OutWidgets.Add(Widget);
		}
	}
}

void FWidgetPreviewRegistry::ListPreviewsForWorld(UWorld* World, TArray<FWidgetPreviewSummary>& OutSummaries)
{
	OutSummaries.Reset();

	for (const FRegisteredWidgetPreview& Entry : GetWidgetPreviewRegistry())
	{
		UWorld* EntryWorld = Entry.World.Get();
		UUserWidget* Widget = Entry.Widget.Get();
		if (!EntryWorld || !Widget)
		{
			continue;
		}
		if (World && EntryWorld != World)
		{
			continue;
		}

		FWidgetPreviewSummary Summary;
		Summary.Handle = Entry.Handle;
		Summary.WorldName = EntryWorld->GetName();
		Summary.WidgetName = Widget->GetName();
		Summary.WidgetClass = Widget->GetClass() ? Widget->GetClass()->GetName() : TEXT("");
		OutSummaries.Add(Summary);
	}
}
