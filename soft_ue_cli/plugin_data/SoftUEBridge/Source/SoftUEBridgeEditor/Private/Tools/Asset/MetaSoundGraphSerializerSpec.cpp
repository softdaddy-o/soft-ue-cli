// Copyright soft-ue-expert. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tools/Asset/MetaSoundGraphSerializer.h"

#include "Dom/JsonObject.h"
#include "MetasoundFrontendDocument.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
	FMetaSoundGraphSerializerSpec,
	"SoftUEBridge.MetaSound.GraphSerializer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FMetaSoundGraphSerializerSpec)

void FMetaSoundGraphSerializerSpec::Define()
{
	Describe("SerializeDocument", [this]()
	{
		It("returns asset_type and an empty graph for a default document", [this]()
		{
			FMetasoundFrontendDocument Document;
			const TSharedPtr<FJsonObject> Json =
				MetaSoundGraphSerializer::SerializeDocument(Document, TEXT("MetaSoundSource"));

			TestTrue(TEXT("json produced"), Json.IsValid());
			TestEqual(TEXT("asset_type"), Json->GetStringField(TEXT("asset_type")), FString(TEXT("MetaSoundSource")));

			const TSharedPtr<FJsonObject> Graph = Json->GetObjectField(TEXT("graph"));
			TestTrue(TEXT("graph present"), Graph.IsValid());
			TestEqual(TEXT("no nodes"), Graph->GetArrayField(TEXT("nodes")).Num(), 0);
			TestEqual(TEXT("no edges"), Graph->GetArrayField(TEXT("edges")).Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
