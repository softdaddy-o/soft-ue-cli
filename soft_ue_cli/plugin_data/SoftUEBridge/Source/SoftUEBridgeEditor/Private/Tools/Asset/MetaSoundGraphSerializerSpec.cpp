// Copyright soft-ue-expert. All Rights Reserved.

// Automation spec for the pure MetaSound graph serializer (Humble Object).
// Run in-editor: Window > Test Automation > "SoftUEBridge.MetaSound.GraphSerializer",
// or:  -ExecCmds="Automation RunTests SoftUEBridge.MetaSound; Quit"
//
// This is the first automation spec in the plugin. It exercises only the pure serializer
// with hand-built FMetasoundFrontendDocument values — no asset loading, no editor graph.
// Drive the richer increments (single node, edge, literal default, paged graph) here
// red-green; each assertion below maps to a Phase B step in the plan.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tools/Asset/MetaSoundGraphSerializer.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "MetasoundFrontendDocument.h"

BEGIN_DEFINE_SPEC(
	FMetaSoundGraphSerializerSpec,
	"SoftUEBridge.MetaSound.GraphSerializer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FMetaSoundGraphSerializerSpec)

void FMetaSoundGraphSerializerSpec::Define()
{
	Describe("SerializeDocument", [this]()
	{
		// B1 — empty document yields the expected top-level shape with empty graph arrays.
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

		// B2..B6 — add in-editor, one at a time, compiling/running after each:
		//   B2: fabricate a node in RootGraph.GetDefaultGraph().Nodes (+ a dependency so
		//       class_name resolves) -> assert nodes[0].id / class_name.
		//   B3: add an FMetasoundFrontendEdge -> assert edges[0] from/to ids.
		//   B4: add an InputLiteral -> assert input_defaults[0].type/value.
		//   B5: add RootGraph.Interface.Inputs/Outputs -> assert graph.inputs/outputs.
		//   B6: add a non-default page -> assert the build/default page is read (and the
		//       deprecated editor-only RootGraph.Graph is never touched).
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
