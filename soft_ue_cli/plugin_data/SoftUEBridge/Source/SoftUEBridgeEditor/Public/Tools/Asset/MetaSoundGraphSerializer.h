// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMetasoundFrontendDocument;
class FJsonObject;

/**
 * Pure (side-effect-free) serialization of a MetaSound Frontend document to JSON.
 *
 * This is the Humble Object for the metasound-inspect tool: it takes an already-loaded
 * document and produces JSON, so it can be unit-tested in an automation spec with a
 * hand-built FMetasoundFrontendDocument — no asset loading, no editor graph, no HTTP.
 *
 * The owning tool (UInspectMetaSoundTool) is the thin shell that loads the asset and
 * hands the const document to SerializeDocument().
 */
namespace MetaSoundGraphSerializer
{
	/**
	 * Serialize the build/default page of a MetaSound document.
	 *
	 * @param Document   The MetaSound Frontend document (read-only).
	 * @param AssetType  Caller-resolved label, e.g. "MetaSoundSource" or "MetaSoundPatch".
	 * @return JSON: { asset_type, graph: { inputs[], outputs[], nodes[], edges[] } }
	 */
	TSharedPtr<FJsonObject> SerializeDocument(const FMetasoundFrontendDocument& Document, const FString& AssetType);
}
