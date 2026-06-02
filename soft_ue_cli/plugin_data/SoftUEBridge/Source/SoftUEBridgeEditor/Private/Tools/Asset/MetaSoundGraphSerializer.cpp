// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/MetaSoundGraphSerializer.h"

#include "Dom/JsonObject.h"
#include "MetasoundFrontendDocument.h"

namespace
{
FString ResolveClassName(const FMetasoundFrontendDocument& Document, const FGuid& ClassID)
{
	for (const FMetasoundFrontendClass& Dependency : Document.Dependencies)
	{
		if (Dependency.ID == ClassID)
		{
			return Dependency.Metadata.GetClassName().ToString();
		}
	}
	return TEXT("");
}

TSharedPtr<FJsonObject> VertexToJson(const FMetasoundFrontendVertex& Vertex)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Vertex.Name.ToString());
	Json->SetStringField(TEXT("type"), Vertex.TypeName.ToString());
	Json->SetStringField(TEXT("vertex_id"), Vertex.VertexID.ToString());
	return Json;
}

TSharedPtr<FJsonObject> LiteralToJson(const FMetasoundFrontendVertexLiteral& Literal)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("vertex_id"), Literal.VertexID.ToString());
	Json->SetNumberField(TEXT("type"), static_cast<double>(static_cast<uint8>(Literal.Value.GetType())));
	Json->SetStringField(TEXT("value"), Literal.Value.ToString());
	return Json;
}

TSharedPtr<FJsonObject> NodeToJson(const FMetasoundFrontendDocument& Document, const FMetasoundFrontendNode& Node)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("id"), Node.GetID().ToString());
	Json->SetStringField(TEXT("name"), Node.Name.ToString());
	Json->SetStringField(TEXT("class_id"), Node.ClassID.ToString());
	Json->SetStringField(TEXT("class_name"), ResolveClassName(Document, Node.ClassID));

	TArray<TSharedPtr<FJsonValue>> Inputs;
	for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Inputs)
	{
		Inputs.Add(MakeShared<FJsonValueObject>(VertexToJson(Vertex)));
	}
	Json->SetArrayField(TEXT("inputs"), Inputs);

	TArray<TSharedPtr<FJsonValue>> Outputs;
	for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Outputs)
	{
		Outputs.Add(MakeShared<FJsonValueObject>(VertexToJson(Vertex)));
	}
	Json->SetArrayField(TEXT("outputs"), Outputs);

	TArray<TSharedPtr<FJsonValue>> Defaults;
	for (const FMetasoundFrontendVertexLiteral& Literal : Node.InputLiterals)
	{
		Defaults.Add(MakeShared<FJsonValueObject>(LiteralToJson(Literal)));
	}
	Json->SetArrayField(TEXT("input_defaults"), Defaults);

	return Json;
}

TSharedPtr<FJsonObject> EdgeToJson(const FMetasoundFrontendEdge& Edge)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("from_node"), Edge.FromNodeID.ToString());
	Json->SetStringField(TEXT("from_vertex"), Edge.FromVertexID.ToString());
	Json->SetStringField(TEXT("to_node"), Edge.ToNodeID.ToString());
	Json->SetStringField(TEXT("to_vertex"), Edge.ToVertexID.ToString());
	return Json;
}

TSharedPtr<FJsonValue> ClassVertexToJson(const FMetasoundFrontendClassVertex& Vertex)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Vertex.Name.ToString());
	Json->SetStringField(TEXT("type"), Vertex.TypeName.ToString());
	return MakeShared<FJsonValueObject>(Json);
}
}

namespace MetaSoundGraphSerializer
{
TSharedPtr<FJsonObject> SerializeDocument(const FMetasoundFrontendDocument& Document, const FString& AssetType)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_type"), AssetType);

	TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> GraphInputs;
	for (const FMetasoundFrontendClassInput& Input : Document.RootGraph.Interface.Inputs)
	{
		GraphInputs.Add(ClassVertexToJson(Input));
	}
	Graph->SetArrayField(TEXT("inputs"), GraphInputs);

	TArray<TSharedPtr<FJsonValue>> GraphOutputs;
	for (const FMetasoundFrontendClassOutput& Output : Document.RootGraph.Interface.Outputs)
	{
		GraphOutputs.Add(ClassVertexToJson(Output));
	}
	Graph->SetArrayField(TEXT("outputs"), GraphOutputs);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	TArray<TSharedPtr<FJsonValue>> Edges;
	if (const FMetasoundFrontendGraph* Page = Document.RootGraph.FindConstGraph(Metasound::Frontend::DefaultPageID))
	{
		for (const FMetasoundFrontendNode& Node : Page->Nodes)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Document, Node)));
		}
		for (const FMetasoundFrontendEdge& Edge : Page->Edges)
		{
			Edges.Add(MakeShared<FJsonValueObject>(EdgeToJson(Edge)));
		}
	}
	Graph->SetArrayField(TEXT("nodes"), Nodes);
	Graph->SetArrayField(TEXT("edges"), Edges);

	Root->SetObjectField(TEXT("graph"), Graph);
	return Root;
}
}
