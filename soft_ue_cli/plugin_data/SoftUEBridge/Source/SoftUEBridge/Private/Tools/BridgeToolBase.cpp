// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/BridgeToolBase.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"

FBridgeToolDefinition UBridgeToolBase::GetDefinition() const
{
	FBridgeToolDefinition Def;
	Def.Name = GetToolName();
	Def.Description = GetToolDescription();
	Def.InputSchema = GetInputSchema();
	Def.Required = GetRequiredParams();
	return Def;
}

bool UBridgeToolBase::GetStringArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, FString& Out)
{
	if (!Args.IsValid()) return false;
	return Args->TryGetStringField(Key, Out);
}

FString UBridgeToolBase::GetStringArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, const FString& Default)
{
	FString Val;
	if (Args.IsValid() && Args->TryGetStringField(Key, Val)) return Val;
	return Default;
}

bool UBridgeToolBase::GetBoolArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool& Out)
{
	if (!Args.IsValid()) return false;
	return Args->TryGetBoolField(Key, Out);
}

bool UBridgeToolBase::GetBoolArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool Default)
{
	bool Val;
	if (Args.IsValid() && Args->TryGetBoolField(Key, Val)) return Val;
	return Default;
}

bool UBridgeToolBase::GetIntArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32& Out)
{
	if (!Args.IsValid()) return false;
	return Args->TryGetNumberField(Key, Out);
}

int32 UBridgeToolBase::GetIntArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32 Default)
{
	int32 Val;
	if (Args.IsValid() && Args->TryGetNumberField(Key, Val)) return Val;
	return Default;
}

bool UBridgeToolBase::GetFloatArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, float& Out)
{
	if (!Args.IsValid()) return false;
	double Dbl;
	if (!Args->TryGetNumberField(Key, Dbl)) return false;
	Out = (float)Dbl;
	return true;
}

float UBridgeToolBase::GetFloatArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, float Default)
{
	float Out;
	if (GetFloatArg(Args, Key, Out)) return Out;
	return Default;
}

bool UBridgeToolBase::MatchesWildcard(const FString& Name, const FString& Pattern)
{
	if (Pattern.IsEmpty()) return true;

	const FString NameLower = Name.ToLower();
	const FString PatternLower = Pattern.ToLower();

	const bool bHasLeadingStar = PatternLower.StartsWith(TEXT("*"));
	const bool bHasTrailingStar = PatternLower.EndsWith(TEXT("*"));

	if (bHasLeadingStar && bHasTrailingStar)
	{
		FString Middle = PatternLower.Mid(1, PatternLower.Len() - 2);
		return NameLower.Contains(Middle);
	}
	if (bHasLeadingStar)
	{
		FString Suffix = PatternLower.Mid(1);
		return NameLower.EndsWith(Suffix);
	}
	if (bHasTrailingStar)
	{
		FString Prefix = PatternLower.Left(PatternLower.Len() - 1);
		return NameLower.StartsWith(Prefix);
	}

	// No wildcards — substring match (also covers exact match)
	return NameLower.Contains(PatternLower);
}

FString UBridgeToolBase::GetActorLabelSafe(const AActor* Actor)
{
#if WITH_EDITOR
	return Actor->GetActorLabel();
#else
	return Actor->GetName();
#endif
}

UWorld* UBridgeToolBase::FindWorldByType(const FString& WorldType)
{
	if (!GEngine) return nullptr;

	EWorldType::Type DesiredType = EWorldType::None;
	if (WorldType == TEXT("editor"))      DesiredType = EWorldType::Editor;
	else if (WorldType == TEXT("pie"))    DesiredType = EWorldType::PIE;
	else if (WorldType == TEXT("game"))   DesiredType = EWorldType::Game;

	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (!Ctx.World()) continue;

		if (DesiredType != EWorldType::None)
		{
			if (Ctx.WorldType == DesiredType) return Ctx.World();
		}
		else
		{
			// No preference: return the first available world
			if (Ctx.WorldType == EWorldType::PIE  ||
				Ctx.WorldType == EWorldType::Game  ||
				Ctx.WorldType == EWorldType::Editor)
			{
				return Ctx.World();
			}
		}
	}
	return nullptr;
}
