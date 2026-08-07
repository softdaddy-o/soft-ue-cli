// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Cloth/ClothTools.h"
#include "Tools/Cloth/BridgeClothBindings.h"

#include "BoneWeights.h"
#include "Animation/Skeleton.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "ClothLODData.h"
#include "ClothPhysicalMeshData.h"
#include "ClothVertBoneData.h"
#include "ClothingAsset.h"
#include "ClothingAssetBase.h"
#include "ClothingAssetFactoryInterface.h"
#include "ClothingSystemEditorInterfaceModule.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/CollectionClothSeamFacade.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ClothingAssetExporter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Features/IModularFeatures.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PointWeightMap.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ScopedTransaction.h"
#include "Utils/ClothingMeshUtils.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgeJsonObjectUtils.h"
#include "Utils/BridgeLegacyClothWeightMaps.h"

#if WITH_DEV_AUTOMATION_TESTS

static FBridgeLegacyWeightMapTarget MakeTestLegacyWeightMapTarget(EWeightMapTargetCommon Id, const TCHAR* CliName, const TCHAR* MapName)
{
	FBridgeLegacyWeightMapTarget Target;
	Target.Id = static_cast<uint8>(Id);
	Target.CliName = CliName;
	Target.MapName = FName(MapName);
	return Target;
}
#include "Misc/AutomationTest.h"
#endif

namespace
{
constexpr float DefaultClothSectionWeldTolerance = 0.1f;

FBridgeSchemaProperty ClothSchemaProperty(
	const FString& Type,
	const FString& Description,
	bool bRequired = false,
	const TArray<FString>& EnumValues = {})
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	Property.Enum = EnumValues;
	return Property;
}

USkeletalMesh* LoadSkeletalMesh(const FString& AssetPath, FString& OutError)
{
	return FBridgeAssetModifier::LoadAssetByPath<USkeletalMesh>(AssetPath, OutError);
}

UClothingAssetBase* FindClothingAsset(USkeletalMesh* Mesh, const FString& AssetName)
{
	if (!Mesh || AssetName.IsEmpty())
	{
		return nullptr;
	}

	for (UClothingAssetBase* Asset : Mesh->GetMeshClothingAssets())
	{
		if (Asset && Asset->GetName().Equals(AssetName, ESearchCase::IgnoreCase))
		{
			return Asset;
		}
	}
	return nullptr;
}

bool ValidateMeshSection(USkeletalMesh* Mesh, int32 LodIndex, int32 SectionIndex, FString& OutError)
{
	if (!Mesh || !Mesh->GetImportedModel())
	{
		OutError = TEXT("cloth: skeletal mesh has no imported model");
		return false;
	}
	if (!Mesh->GetImportedModel()->LODModels.IsValidIndex(LodIndex))
	{
		OutError = FString::Printf(TEXT("cloth: lod_index %d is out of range"), LodIndex);
		return false;
	}
	const FSkeletalMeshLODModel& LodModel = Mesh->GetImportedModel()->LODModels[LodIndex];
	if (!LodModel.Sections.IsValidIndex(SectionIndex))
	{
		OutError = FString::Printf(TEXT("cloth: section_index %d is out of range for LOD %d"), SectionIndex, LodIndex);
		return false;
	}
	return true;
}

bool ParseSectionIndicesFromArgs(const TSharedPtr<FJsonObject>& Arguments, TArray<int32>& OutSectionIndices, FString& OutError)
{
	OutSectionIndices.Reset();
	if (!Arguments.IsValid())
	{
		OutError = TEXT("cloth: arguments are required");
		return false;
	}

	auto AddSectionIndex = [&OutSectionIndices](int32 SectionIndex)
	{
		OutSectionIndices.AddUnique(SectionIndex);
	};

	const TArray<TSharedPtr<FJsonValue>>* SectionIndexValues = nullptr;
	if (Arguments->TryGetArrayField(TEXT("section_indices"), SectionIndexValues) && SectionIndexValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *SectionIndexValues)
		{
			int32 SectionIndex = INDEX_NONE;
			if (!Value.IsValid() || !Value->TryGetNumber(SectionIndex))
			{
				OutError = TEXT("cloth: section_indices must contain integer values");
				return false;
			}
			AddSectionIndex(SectionIndex);
		}
	}

	int32 SectionIndex = INDEX_NONE;
	if (Arguments->TryGetNumberField(TEXT("section_index"), SectionIndex))
	{
		AddSectionIndex(SectionIndex);
	}

	if (OutSectionIndices.IsEmpty())
	{
		OutError = TEXT("skeletal_mesh, asset_name, and section_index or section_indices are required");
		return false;
	}

	return true;
}

bool ValidateMeshSections(USkeletalMesh* Mesh, int32 LodIndex, const TArray<int32>& SectionIndices, FString& OutError)
{
	for (int32 SectionIndex : SectionIndices)
	{
		if (!ValidateMeshSection(Mesh, LodIndex, SectionIndex, OutError))
		{
			return false;
		}
		const FSkeletalMeshLODModel& LodModel = Mesh->GetImportedModel()->LODModels[LodIndex];
		if (LodModel.Sections[SectionIndex].HasClothingData())
		{
			OutError = FString::Printf(TEXT("cloth: section_index %d already has clothing data"), SectionIndex);
			return false;
		}
	}
	return true;
}

bool SaveMeshIfRequested(USkeletalMesh* Mesh, bool bSave, TSharedPtr<FJsonObject>& Result, FString& OutError)
{
	FBridgeAssetModifier::MarkPackageDirty(Mesh);
	Result->SetBoolField(TEXT("needs_save"), true);
	if (!bSave)
	{
		Result->SetBoolField(TEXT("saved"), false);
		return true;
	}

	FString SaveError;
	if (!FBridgeAssetModifier::SaveAsset(Mesh, false, SaveError))
	{
		Result->SetStringField(TEXT("save_error"), SaveError);
		Result->SetBoolField(TEXT("saved"), false);
		OutError = SaveError;
		return false;
	}
	Result->SetBoolField(TEXT("needs_save"), false);
	Result->SetBoolField(TEXT("saved"), true);
	return true;
}

// UChaosClothAssetBase::HasDataflow() was only added in UE 5.8. It is defined
// there as GetDataflow() != nullptr, and GetDataflow() already exists in 5.7,
// so this spelling is correct on both versions with no version guard needed.
bool ClothAssetHasDataflow(const UChaosClothAsset* Asset)
{
	return Asset && Asset->GetDataflow() != nullptr;
}

// UE 5.8 added the per-section overload UnbindFromSkeletalMesh(Mesh, Lod, Section)
// and deprecated the LOD-only one; UE 5.7 only has the LOD-only overload.
void UnbindClothFromSkeletalMesh(
	UClothingAssetBase* Asset,
	USkeletalMesh* Mesh,
	int32 LodIndex,
	int32 SectionIndex)
{
	if (!Asset || !Mesh)
	{
		return;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	Asset->UnbindFromSkeletalMesh(Mesh, LodIndex, SectionIndex);
#else
	// 5.7 cannot unbind a single section. SectionIndex == INDEX_NONE already means
	// "all sections of this LOD", so those call sites are exactly equivalent; a
	// specific SectionIndex necessarily unbinds the whole LOD on 5.7.
	Asset->UnbindFromSkeletalMesh(Mesh, LodIndex);
#endif
}

void ClearOriginalSectionClothData(FSkelMeshSourceSectionUserData& OriginalSectionData)
{
	OriginalSectionData.CorrespondClothAssetIndex = INDEX_NONE;
	OriginalSectionData.ClothingData.AssetGuid = FGuid();
	OriginalSectionData.ClothingData.AssetLodIndex = INDEX_NONE;
}

bool BindClothAssetToSection(
	USkeletalMesh* Mesh,
	UClothingAssetBase* Asset,
	int32 LodIndex,
	int32 SectionIndex,
	int32 ClothLodIndex,
	FString& OutError,
	bool bClearExistingAssetBindings = true)
{
	if (!Mesh || !Mesh->GetImportedModel())
	{
		OutError = TEXT("cloth: skeletal mesh has no imported model");
		return false;
	}
	if (!Asset)
	{
		OutError = TEXT("cloth asset not found");
		return false;
	}
	if (!Mesh->GetImportedModel()->LODModels.IsValidIndex(LodIndex))
	{
		OutError = FString::Printf(TEXT("cloth: lod_index %d is out of range"), LodIndex);
		return false;
	}

	FSkeletalMeshLODModel& LodModel = Mesh->GetImportedModel()->LODModels[LodIndex];
	if (!LodModel.Sections.IsValidIndex(SectionIndex))
	{
		OutError = FString::Printf(TEXT("cloth: section_index %d is out of range for LOD %d"), SectionIndex, LodIndex);
		return false;
	}

	FScopedSkeletalMeshPostEditChange BindingPostEditChange(Mesh);

	if (UClothingAssetBase* CurrentAsset = Mesh->GetSectionClothingAsset(LodIndex, SectionIndex))
	{
		CurrentAsset->Modify();
		UnbindClothFromSkeletalMesh(CurrentAsset, Mesh, LodIndex, SectionIndex);
	}

	Asset->Modify();
	// Repair assets left with a populated LodMap but no section binding by older bridge versions.
	if (bClearExistingAssetBindings)
	{
		UnbindClothFromSkeletalMesh(Asset, Mesh, LodIndex, INDEX_NONE);
	}

	FSkelMeshSection& Section = Mesh->GetImportedModel()->LODModels[LodIndex].Sections[SectionIndex];
	FSkelMeshSourceSectionUserData& OriginalSectionData =
		Mesh->GetImportedModel()->LODModels[LodIndex].UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
	ClearOriginalSectionClothData(OriginalSectionData);

	if (!Asset->BindToSkeletalMesh(Mesh, LodIndex, SectionIndex, ClothLodIndex))
	{
		OutError = TEXT("cloth: BindToSkeletalMesh failed");
		return false;
	}

	int32 AssetIndex = INDEX_NONE;
	if (!Mesh->GetMeshClothingAssets().Find(Asset, AssetIndex))
	{
		OutError = TEXT("cloth: bound asset is not registered on skeletal mesh");
		return false;
	}

	OriginalSectionData.CorrespondClothAssetIndex = static_cast<int16>(AssetIndex);
	OriginalSectionData.ClothingData.AssetGuid = Asset->GetAssetGuid();
	OriginalSectionData.ClothingData.AssetLodIndex = ClothLodIndex;
	return true;
}

bool BuildMergedClothLodFromSections(
	USkeletalMesh* Mesh,
	UClothingAssetCommon* CommonAsset,
	int32 LodIndex,
	const TArray<int32>& SectionIndices,
	int32 ClothLodIndex,
	float WeldTolerance,
	FString& OutError)
{
	if (!Mesh || !Mesh->GetImportedModel())
	{
		OutError = TEXT("cloth: skeletal mesh has no imported model");
		return false;
	}
	if (!CommonAsset || !CommonAsset->LodData.IsValidIndex(ClothLodIndex))
	{
		OutError = TEXT("cloth: clothing asset has no editable cloth LOD");
		return false;
	}

	FSkeletalMeshLODModel& SourceLod = Mesh->GetImportedModel()->LODModels[LodIndex];
	TArray<FVector> UniquePositions;
	TArray<int32> UniquePositionSectionIndices;
	TArray<FVector3f> MergedVertices;
	TArray<FVector3f> MergedNormals;
	TArray<FColor> MergedColors;
	TArray<FClothVertBoneData> MergedBoneData;
	TArray<uint32> MergedIndices;
	TMap<int32, TArray<bool>> SourceSectionMemberships;
	int32 MaxBoneInfluences = 0;
	const float WeldToleranceSquared = FMath::Square(FMath::Max(0.0f, WeldTolerance));

	for (int32 SectionIndex : SectionIndices)
	{
		const FSkelMeshSection& SourceSection = SourceLod.Sections[SectionIndex];
		TArray<int32> SectionLocalToMerged;
		SectionLocalToMerged.SetNum(SourceSection.SoftVertices.Num());

		for (int32 LocalVertexIndex = 0; LocalVertexIndex < SourceSection.SoftVertices.Num(); ++LocalVertexIndex)
		{
			const FSoftSkinVertex& SourceVert = SourceSection.SoftVertices[LocalVertexIndex];
			const FVector SourcePosition(SourceVert.Position);
			int32 MergedVertexIndex = INDEX_NONE;
			for (int32 UniqueIndex = 0; UniqueIndex < UniquePositions.Num(); ++UniqueIndex)
			{
				const bool bSameSourceSection =
					UniquePositionSectionIndices.IsValidIndex(UniqueIndex)
					&& UniquePositionSectionIndices[UniqueIndex] == SectionIndex;
				const float PositionToleranceSquared = bSameSourceSection ? SMALL_NUMBER : WeldToleranceSquared;
				if (FVector::DistSquared(UniquePositions[UniqueIndex], SourcePosition) <= PositionToleranceSquared)
				{
					MergedVertexIndex = UniqueIndex;
					break;
				}
			}

			if (MergedVertexIndex == INDEX_NONE)
			{
				MergedVertexIndex = UniquePositions.Num();
				UniquePositions.Add(SourcePosition);
				UniquePositionSectionIndices.Add(SectionIndex);
				MergedVertices.Add(SourceVert.Position);
				MergedNormals.Add(SourceVert.TangentZ);
				MergedColors.Add(SourceVert.Color);

				FClothVertBoneData BoneData;
				for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
				{
					if (SourceVert.InfluenceWeights[InfluenceIndex] == 0)
					{
						continue;
					}
					const int32 BoneMapIndex = SourceVert.InfluenceBones[InfluenceIndex];
					if (!SourceSection.BoneMap.IsValidIndex(BoneMapIndex))
					{
						continue;
					}
					const int32 SourceBoneIndex = SourceSection.BoneMap[BoneMapIndex];
					if (SourceBoneIndex == INDEX_NONE || !Mesh->GetRefSkeleton().IsValidIndex(SourceBoneIndex))
					{
						continue;
					}

					const FName BoneName = Mesh->GetRefSkeleton().GetBoneName(SourceBoneIndex);
					BoneData.BoneIndices[InfluenceIndex] = CommonAsset->UsedBoneNames.AddUnique(BoneName);
					BoneData.BoneWeights[InfluenceIndex] =
						static_cast<float>(SourceVert.InfluenceWeights[InfluenceIndex]) / UE::AnimationCore::MaxRawBoneWeightFloat;
					++BoneData.NumInfluences;
				}
				MergedBoneData.Add(BoneData);
			}

			RecordBridgeSourceSectionMembership(
				SourceSectionMemberships,
				SectionIndex,
				MergedVertexIndex,
				UniquePositions.Num());
			SectionLocalToMerged[LocalVertexIndex] = MergedVertexIndex;
		}

		MaxBoneInfluences = FMath::Max(MaxBoneInfluences, static_cast<int32>(SourceSection.MaxBoneInfluences));
		for (uint32 SectionIndexOffset = 0; SectionIndexOffset < SourceSection.NumTriangles * 3; ++SectionIndexOffset)
		{
			const int32 SourceLocalVertexIndex =
				static_cast<int32>(SourceLod.IndexBuffer[SourceSection.BaseIndex + SectionIndexOffset]) - SourceSection.BaseVertexIndex;
			if (!SectionLocalToMerged.IsValidIndex(SourceLocalVertexIndex))
			{
				OutError = FString::Printf(TEXT("cloth: section_index %d has an out-of-range index buffer reference"), SectionIndex);
				return false;
			}
			MergedIndices.Add(static_cast<uint32>(SectionLocalToMerged[SourceLocalVertexIndex]));
		}
	}

	FClothLODDataCommon& LodData = CommonAsset->LodData[ClothLodIndex];
	FClothPhysicalMeshData& PhysMesh = LodData.PhysicalMeshData;
	PhysMesh.Reset(MergedVertices.Num(), MergedIndices.Num());
	LodData.PointWeightMaps.Reset();

	for (int32 VertexIndex = 0; VertexIndex < MergedVertices.Num(); ++VertexIndex)
	{
		PhysMesh.Vertices[VertexIndex] = MergedVertices[VertexIndex];
		PhysMesh.Normals[VertexIndex] = MergedNormals[VertexIndex];
#if WITH_EDITORONLY_DATA
		PhysMesh.VertexColors[VertexIndex] = MergedColors[VertexIndex];
#endif
		PhysMesh.BoneData[VertexIndex] = MergedBoneData[VertexIndex];
	}
	for (int32 IndexIndex = 0; IndexIndex < MergedIndices.Num(); ++IndexIndex)
	{
		PhysMesh.Indices[IndexIndex] = MergedIndices[IndexIndex];
	}

	FBridgeLegacyWeightMapTarget MaxDistanceTarget;
	FString TargetError;
	if (!ResolveBridgeLegacyWeightMapTarget(TEXT("max-distance"), MaxDistanceTarget, TargetError))
	{
		OutError = TargetError;
		return false;
	}
	const EWeightMapTargetCommon MaxDistanceKey = static_cast<EWeightMapTargetCommon>(MaxDistanceTarget.Id);
	FPointWeightMap& PhysMeshMaxDistances = PhysMesh.AddWeightMap(MaxDistanceKey);
	PhysMeshMaxDistances.Initialize(PhysMesh.Vertices.Num());
	ConfigureBridgeLegacyWeightMapMetadata(PhysMeshMaxDistances, MaxDistanceTarget);

	LodData.PointWeightMaps.AddDefaulted();
	FPointWeightMap& LodMaxDistances = LodData.PointWeightMaps.Last();
	LodMaxDistances.Initialize(PhysMeshMaxDistances, MaxDistanceKey);
	ConfigureBridgeLegacyWeightMapMetadata(LodMaxDistances, MaxDistanceTarget);
	WriteBridgeSourceSectionMaps(LodData, SourceSectionMemberships);

	PhysMesh.MaxBoneWeights = MaxBoneInfluences;
	PhysMesh.CalculateNumInfluences();

	const int32 NumTriangles = PhysMesh.Indices.Num() / 3;
	for (int32 TriIndex = 0; TriIndex < NumTriangles; ++TriIndex)
	{
		const FVector A(PhysMesh.Vertices[PhysMesh.Indices[TriIndex * 3 + 0]]);
		const FVector B(PhysMesh.Vertices[PhysMesh.Indices[TriIndex * 3 + 1]]);
		const FVector C(PhysMesh.Vertices[PhysMesh.Indices[TriIndex * 3 + 2]]);
		if (((B - A) ^ (C - A)).SizeSquared() <= SMALL_NUMBER)
		{
			OutError = FString::Printf(TEXT("cloth: merged section mesh contains a degenerate triangle at triangle %d"), TriIndex);
			return false;
		}
	}

	CommonAsset->RefreshBoneMapping(Mesh);
	CommonAsset->BuildLodTransitionData();
	CommonAsset->InvalidateAllCachedData();
	return true;
}

bool BindClothAssetToSections(
	USkeletalMesh* Mesh,
	UClothingAssetBase* Asset,
	int32 LodIndex,
	const TArray<int32>& SectionIndices,
	int32 ClothLodIndex,
	FString& OutError)
{
	UClothingAssetCommon* CommonAsset = Cast<UClothingAssetCommon>(Asset);
	if (!CommonAsset)
	{
		OutError = TEXT("cloth: multi-section bind requires a common clothing asset");
		return false;
	}

	for (int32 SectionIndex : SectionIndices)
	{
		if (CommonAsset->LodMap.IsValidIndex(LodIndex))
		{
			CommonAsset->LodMap[LodIndex] = INDEX_NONE;
		}
		if (!BindClothAssetToSection(Mesh, Asset, LodIndex, SectionIndex, ClothLodIndex, OutError, false))
		{
			return false;
		}
	}

	while (CommonAsset->LodMap.Num() <= LodIndex)
	{
		CommonAsset->LodMap.Add(INDEX_NONE);
	}
	CommonAsset->LodMap[LodIndex] = ClothLodIndex;
	return true;
}

TSharedPtr<FJsonObject> WeightMapStatsToJson(const FPointWeightMap* WeightMap)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!WeightMap)
	{
		Json->SetBoolField(TEXT("present"), false);
		Json->SetNumberField(TEXT("count"), 0);
		return Json;
	}

	float MinValue = 0.0f;
	float MaxValue = 0.0f;
	int32 NonZeroCount = 0;
	if (WeightMap->Values.Num() > 0)
	{
		MinValue = WeightMap->Values[0];
		MaxValue = WeightMap->Values[0];
		for (float Value : WeightMap->Values)
		{
			MinValue = FMath::Min(MinValue, Value);
			MaxValue = FMath::Max(MaxValue, Value);
			if (!FMath::IsNearlyZero(Value))
			{
				++NonZeroCount;
			}
		}
	}

	Json->SetBoolField(TEXT("present"), true);
	Json->SetNumberField(TEXT("count"), WeightMap->Values.Num());
	Json->SetNumberField(TEXT("min"), MinValue);
	Json->SetNumberField(TEXT("max"), MaxValue);
	Json->SetNumberField(TEXT("nonzero_count"), NonZeroCount);
	Json->SetNumberField(TEXT("coverage"), WeightMap->Values.Num() > 0 ? static_cast<double>(NonZeroCount) / WeightMap->Values.Num() : 0.0);
#if WITH_EDITORONLY_DATA
	Json->SetStringField(TEXT("name"), WeightMap->Name.ToString());
	Json->SetNumberField(TEXT("target"), WeightMap->CurrentTarget);
	Json->SetBoolField(TEXT("enabled"), WeightMap->bEnabled);
#endif
	return Json;
}

TSharedPtr<FJsonObject> FloatArrayStatsToJson(TConstArrayView<float> Values)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("present"), Values.Num() > 0);
	Json->SetNumberField(TEXT("count"), Values.Num());
	if (Values.IsEmpty())
	{
		Json->SetNumberField(TEXT("min"), 0.0);
		Json->SetNumberField(TEXT("max"), 0.0);
		Json->SetNumberField(TEXT("nonzero_count"), 0);
		Json->SetNumberField(TEXT("coverage"), 0.0);
		return Json;
	}

	float MinValue = Values[0];
	float MaxValue = Values[0];
	int32 NonZeroCount = 0;
	for (float Value : Values)
	{
		MinValue = FMath::Min(MinValue, Value);
		MaxValue = FMath::Max(MaxValue, Value);
		if (!FMath::IsNearlyZero(Value))
		{
			++NonZeroCount;
		}
	}

	Json->SetNumberField(TEXT("min"), MinValue);
	Json->SetNumberField(TEXT("max"), MaxValue);
	Json->SetNumberField(TEXT("nonzero_count"), NonZeroCount);
	Json->SetNumberField(TEXT("coverage"), static_cast<double>(NonZeroCount) / Values.Num());
	return Json;
}

TArray<TSharedPtr<FJsonValue>> Vector3fToJsonArray(const FVector3f& Vector)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
	Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
	Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
	return Values;
}

uint64 MakeVertexPairKey(int32 First, int32 Second)
{
	if (First > Second)
	{
		Swap(First, Second);
	}
	return (static_cast<uint64>(static_cast<uint32>(First)) << 32) | static_cast<uint32>(Second);
}

void AppendIntArrayJson(TArray<TSharedPtr<FJsonValue>>& OutValues, const TArray<int32>& Indices)
{
	for (int32 Index : Indices)
	{
		OutValues.Add(MakeShared<FJsonValueNumber>(Index));
	}
}

TSet<uint64> BuildStitchedSim3DPairs(const UE::Chaos::ClothAsset::FCollectionClothConstFacade& Cloth)
{
	TSet<uint64> StitchedPairs;
	TConstArrayView<int32> Vertex3DLookup = Cloth.GetSimVertex3DLookup();
	for (int32 SeamIndex = 0; SeamIndex < Cloth.GetNumSeams(); ++SeamIndex)
	{
		const UE::Chaos::ClothAsset::FCollectionClothSeamConstFacade Seam = Cloth.GetSeam(SeamIndex);
		for (const FIntVector2& Stitch2D : Seam.GetSeamStitch2DEndIndices())
		{
			if (Vertex3DLookup.IsValidIndex(Stitch2D.X) && Vertex3DLookup.IsValidIndex(Stitch2D.Y))
			{
				const int32 First3D = Vertex3DLookup[Stitch2D.X];
				const int32 Second3D = Vertex3DLookup[Stitch2D.Y];
				if (First3D != INDEX_NONE && Second3D != INDEX_NONE && First3D != Second3D)
				{
					StitchedPairs.Add(MakeVertexPairKey(First3D, Second3D));
				}
			}
		}
	}
	return StitchedPairs;
}

TSharedPtr<FJsonObject> BuildChaosStitchPairJson(
	const UE::Chaos::ClothAsset::FCollectionClothConstFacade& Cloth,
	const FIntVector2& Pair,
	const FString& IndexSpace)
{
	TSharedPtr<FJsonObject> PairJson = MakeShared<FJsonObject>();
	PairJson->SetNumberField(TEXT("first_vertex_2d"), Pair.X);
	PairJson->SetNumberField(TEXT("second_vertex_2d"), Pair.Y);
	PairJson->SetStringField(TEXT("distance_space"), IndexSpace);
	TConstArrayView<int32> Vertex3DLookup = Cloth.GetSimVertex3DLookup();
	TConstArrayView<FVector3f> Positions = Cloth.GetSimPosition3D();
	if (IndexSpace.Equals(TEXT("2d"), ESearchCase::IgnoreCase))
	{
		TConstArrayView<FVector2f> Positions2D = Cloth.GetSimPosition2D();
		if (Positions2D.IsValidIndex(Pair.X) && Positions2D.IsValidIndex(Pair.Y))
		{
			const float SelectionDistance = FVector2f::Distance(Positions2D[Pair.X], Positions2D[Pair.Y]);
			PairJson->SetNumberField(TEXT("selection_distance"), SelectionDistance);
			PairJson->SetNumberField(TEXT("distance"), SelectionDistance);
		}
	}
	if (Vertex3DLookup.IsValidIndex(Pair.X) && Vertex3DLookup.IsValidIndex(Pair.Y))
	{
		const int32 First3D = Vertex3DLookup[Pair.X];
		const int32 Second3D = Vertex3DLookup[Pair.Y];
		PairJson->SetNumberField(TEXT("first_vertex_3d"), First3D);
		PairJson->SetNumberField(TEXT("second_vertex_3d"), Second3D);
		if (Positions.IsValidIndex(First3D) && Positions.IsValidIndex(Second3D))
		{
			const float Distance3D = FVector3f::Distance(Positions[First3D], Positions[Second3D]);
			PairJson->SetNumberField(TEXT("distance_3d"), Distance3D);
			if (!IndexSpace.Equals(TEXT("2d"), ESearchCase::IgnoreCase))
			{
				PairJson->SetNumberField(TEXT("selection_distance"), Distance3D);
				PairJson->SetNumberField(TEXT("distance"), Distance3D);
			}
		}
	}
	return PairJson;
}

TSharedPtr<FJsonObject> BuildChaosClothGapDiagnostics(
	const UE::Chaos::ClothAsset::FCollectionClothConstFacade& Cloth,
	float GapTolerance,
	int32 GapLimit)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("gap_tolerance"), GapTolerance);
	Json->SetNumberField(TEXT("gap_limit"), GapLimit);

	TArray<TSharedPtr<FJsonValue>> GapCandidates;
	const TConstArrayView<FVector3f> Positions = Cloth.GetSimPosition3D();
	const TSet<uint64> StitchedPairs = BuildStitchedSim3DPairs(Cloth);
	const float ToleranceSq = GapTolerance * GapTolerance;
	constexpr int64 MaxChaosGapDistanceChecks = 250000;
	int64 DistanceCheckCount = 0;
	bool bTruncated = false;
	int32 TotalCandidateCount = 0;
	float MinDistance = TNumericLimits<float>::Max();
	float MaxDistance = 0.0f;
	for (int32 FirstIndex = 0; FirstIndex < Positions.Num() && !bTruncated; ++FirstIndex)
	{
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < Positions.Num(); ++SecondIndex)
		{
			if (StitchedPairs.Contains(MakeVertexPairKey(FirstIndex, SecondIndex)))
			{
				continue;
			}
			if (++DistanceCheckCount > MaxChaosGapDistanceChecks)
			{
				bTruncated = true;
				break;
			}
			const float DistanceSq = FVector3f::DistSquared(Positions[FirstIndex], Positions[SecondIndex]);
			if (DistanceSq > ToleranceSq)
			{
				continue;
			}
			const float Distance = FMath::Sqrt(DistanceSq);
			++TotalCandidateCount;
			MinDistance = FMath::Min(MinDistance, Distance);
			MaxDistance = FMath::Max(MaxDistance, Distance);
			if (GapLimit <= 0 || GapCandidates.Num() < GapLimit)
			{
				TSharedPtr<FJsonObject> Candidate = MakeShared<FJsonObject>();
				Candidate->SetNumberField(TEXT("first_vertex_3d"), FirstIndex);
				Candidate->SetNumberField(TEXT("second_vertex_3d"), SecondIndex);
				Candidate->SetNumberField(TEXT("distance"), Distance);
				Candidate->SetArrayField(TEXT("first_position"), Vector3fToJsonArray(Positions[FirstIndex]));
				Candidate->SetArrayField(TEXT("second_position"), Vector3fToJsonArray(Positions[SecondIndex]));
				GapCandidates.Add(MakeShared<FJsonValueObject>(Candidate));
			}
		}
	}
	Json->SetArrayField(TEXT("gap_candidates"), GapCandidates);
	Json->SetNumberField(TEXT("candidate_count"), TotalCandidateCount);
	Json->SetNumberField(TEXT("returned_count"), GapCandidates.Num());
	Json->SetNumberField(TEXT("min_distance"), TotalCandidateCount > 0 ? MinDistance : 0.0f);
	Json->SetNumberField(TEXT("max_distance"), TotalCandidateCount > 0 ? MaxDistance : 0.0f);
	Json->SetNumberField(TEXT("distance_check_count"), static_cast<double>(DistanceCheckCount));
	Json->SetNumberField(TEXT("max_distance_check_count"), static_cast<double>(MaxChaosGapDistanceChecks));
	Json->SetBoolField(TEXT("truncated"), bTruncated);
	return Json;
}

TSharedPtr<FJsonObject> DumpChaosClothWeightMapVertices(
	const UE::Chaos::ClothAsset::FCollectionClothConstFacade& Cloth,
	const FName& WeightMapName,
	int32 WeightLimit)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), WeightMapName.ToString());
	Json->SetBoolField(TEXT("present"), Cloth.HasWeightMap(WeightMapName));
	if (!Cloth.HasWeightMap(WeightMapName))
	{
		Json->SetNumberField(TEXT("count"), 0);
		Json->SetNumberField(TEXT("returned_count"), 0);
		Json->SetArrayField(TEXT("vertices"), {});
		return Json;
	}

	const TConstArrayView<float> Values = Cloth.GetWeightMap(WeightMapName);
	const TConstArrayView<FVector3f> Positions = Cloth.GetSimPosition3D();
	const int32 VertexCount = FMath::Min(Values.Num(), Positions.Num());
	TArray<TSharedPtr<FJsonValue>> Vertices;
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		if (WeightLimit > 0 && Vertices.Num() >= WeightLimit)
		{
			break;
		}
		TSharedPtr<FJsonObject> Vertex = MakeShared<FJsonObject>();
		Vertex->SetNumberField(TEXT("index"), Index);
		Vertex->SetNumberField(TEXT("value"), Values[Index]);
		Vertex->SetBoolField(TEXT("is_kinematic"), FMath::IsNearlyZero(Values[Index]));
		Vertex->SetArrayField(TEXT("position"), Vector3fToJsonArray(Positions[Index]));
		Vertices.Add(MakeShared<FJsonValueObject>(Vertex));
	}
	Json->SetObjectField(TEXT("summary"), FloatArrayStatsToJson(Values));
	Json->SetNumberField(TEXT("count"), VertexCount);
	Json->SetNumberField(TEXT("returned_count"), Vertices.Num());
	Json->SetArrayField(TEXT("vertices"), Vertices);
	return Json;
}

TSharedPtr<FJsonObject> ChaosClothCollectionToJson(
	const TSharedRef<const FManagedArrayCollection>& Collection,
	int32 LodIndex,
	bool bDumpWeights,
	FName DumpWeightMapName,
	int32 WeightLimit,
	float GapTolerance,
	int32 GapLimit)
{
	using namespace UE::Chaos::ClothAsset;

	FCollectionClothConstFacade Cloth(Collection);
	TSharedPtr<FJsonObject> LodJson = MakeShared<FJsonObject>();
	LodJson->SetNumberField(TEXT("lod_index"), LodIndex);
	LodJson->SetBoolField(TEXT("valid"), Cloth.IsValid());
	LodJson->SetBoolField(TEXT("has_simulation_data"), Cloth.HasValidSimulationData());
	LodJson->SetBoolField(TEXT("has_render_data"), Cloth.HasValidRenderData());
	LodJson->SetStringField(TEXT("physics_asset"), Cloth.GetPhysicsAssetSoftObjectPathName().ToString());
	LodJson->SetStringField(TEXT("skeletal_mesh"), Cloth.GetSkeletalMeshSoftObjectPathName().ToString());
	LodJson->SetStringField(TEXT("reference_bone"), Cloth.GetReferenceBoneName().ToString());

	TSharedPtr<FJsonObject> SimJson = MakeShared<FJsonObject>();
	SimJson->SetNumberField(TEXT("vertex_2d_count"), Cloth.GetNumSimVertices2D());
	SimJson->SetNumberField(TEXT("vertex_3d_count"), Cloth.GetNumSimVertices3D());
	SimJson->SetNumberField(TEXT("face_count"), Cloth.GetNumSimFaces());
	SimJson->SetNumberField(TEXT("pattern_count"), Cloth.GetNumSimPatterns());
	LodJson->SetObjectField(TEXT("simulation_mesh"), SimJson);

	TSharedPtr<FJsonObject> RenderJson = MakeShared<FJsonObject>();
	RenderJson->SetNumberField(TEXT("vertex_count"), Cloth.GetNumRenderVertices());
	RenderJson->SetNumberField(TEXT("face_count"), Cloth.GetNumRenderFaces());
	RenderJson->SetNumberField(TEXT("pattern_count"), Cloth.GetNumRenderPatterns());
	LodJson->SetObjectField(TEXT("render_mesh"), RenderJson);

	TArray<TSharedPtr<FJsonValue>> SeamValues;
	int32 StitchCount = 0;
	TConstArrayView<int32> Vertex3DLookup = Cloth.GetSimVertex3DLookup();
	for (int32 SeamIndex = 0; SeamIndex < Cloth.GetNumSeams(); ++SeamIndex)
	{
		FCollectionClothSeamConstFacade Seam = Cloth.GetSeam(SeamIndex);
		TSharedPtr<FJsonObject> SeamJson = MakeShared<FJsonObject>();
		int32 WeldedStitchCount = 0;
		for (const FIntVector2& Stitch2D : Seam.GetSeamStitch2DEndIndices())
		{
			if (Vertex3DLookup.IsValidIndex(Stitch2D.X) && Vertex3DLookup.IsValidIndex(Stitch2D.Y)
				&& Vertex3DLookup[Stitch2D.X] != INDEX_NONE
				&& Vertex3DLookup[Stitch2D.X] == Vertex3DLookup[Stitch2D.Y])
			{
				++WeldedStitchCount;
			}
		}
		SeamJson->SetNumberField(TEXT("seam_index"), SeamIndex);
		SeamJson->SetNumberField(TEXT("stitch_count"), Seam.GetNumSeamStitches());
		SeamJson->SetNumberField(TEXT("stitch_offset"), Seam.GetSeamStitchesOffset());
		SeamJson->SetNumberField(TEXT("welded_stitch_count"), WeldedStitchCount);
		SeamJson->SetNumberField(TEXT("unwelded_stitch_count"), Seam.GetNumSeamStitches() - WeldedStitchCount);
		SeamJson->SetNumberField(TEXT("weld_coverage"), Seam.GetNumSeamStitches() > 0 ? static_cast<double>(WeldedStitchCount) / Seam.GetNumSeamStitches() : 0.0);
		StitchCount += Seam.GetNumSeamStitches();
		SeamValues.Add(MakeShared<FJsonValueObject>(SeamJson));
	}
	LodJson->SetArrayField(TEXT("seams"), SeamValues);
	LodJson->SetNumberField(TEXT("seam_count"), SeamValues.Num());
	LodJson->SetNumberField(TEXT("stitch_count"), StitchCount);

	TArray<TSharedPtr<FJsonValue>> WeightMaps;
	for (const FName& WeightMapName : Cloth.GetWeightMapNames())
	{
		TSharedPtr<FJsonObject> WeightMapJson = FloatArrayStatsToJson(Cloth.GetWeightMap(WeightMapName));
		WeightMapJson->SetStringField(TEXT("name"), WeightMapName.ToString());
		WeightMaps.Add(MakeShared<FJsonValueObject>(WeightMapJson));
	}
	LodJson->SetArrayField(TEXT("weight_maps"), WeightMaps);
	LodJson->SetNumberField(TEXT("weight_map_count"), WeightMaps.Num());
	if (GapTolerance >= 0.0f)
	{
		LodJson->SetObjectField(TEXT("seam_diagnostics"), BuildChaosClothGapDiagnostics(Cloth, GapTolerance, GapLimit));
	}
	if (bDumpWeights)
	{
		LodJson->SetObjectField(TEXT("weight_vertices"), DumpChaosClothWeightMapVertices(Cloth, DumpWeightMapName, WeightLimit));
	}
	return LodJson;
}

TSharedPtr<FJsonObject> ChaosClothAssetToJson(
	UChaosClothAsset* Asset,
	const FString& ClothAssetPath,
	bool bIncludeNodes,
	bool bDumpWeights = false,
	FName DumpWeightMapName = FName(TEXT("MaxDistance")),
	int32 WeightLimit = 0,
	float GapTolerance = -1.0f,
	int32 GapLimit = 0)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("cloth_asset"), ClothAssetPath);
	Result->SetStringField(TEXT("name"), Asset ? Asset->GetName() : TEXT(""));
	if (!Asset)
	{
		return Result;
	}

	Result->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
	Result->SetStringField(TEXT("physics_asset"), Asset->GetPhysicsAsset() ? Asset->GetPhysicsAsset()->GetPathName() : TEXT(""));
	const FString SkeletonPath = Asset->GetSkeleton() ? Asset->GetSkeleton()->GetPathName() : FString();
	Result->SetStringField(TEXT("skeleton"), SkeletonPath);
	Result->SetBoolField(TEXT("has_valid_simulation_models"), Asset->HasValidClothSimulationModels());
	Result->SetNumberField(TEXT("simulation_model_count"), Asset->GetNumClothSimulationModels());
	Result->SetStringField(TEXT("guid"), Asset->GetAssetGuid(0).ToString(EGuidFormats::DigitsWithHyphens));

	TArray<TSharedPtr<FJsonValue>> LodValues;
	const TArray<TSharedRef<const FManagedArrayCollection>>& Collections = Asset->GetClothCollections();
	for (int32 LodIndex = 0; LodIndex < Collections.Num(); ++LodIndex)
	{
		LodValues.Add(MakeShared<FJsonValueObject>(ChaosClothCollectionToJson(Collections[LodIndex], LodIndex, bDumpWeights, DumpWeightMapName, WeightLimit, GapTolerance, GapLimit)));
	}
	Result->SetArrayField(TEXT("lods"), LodValues);
	Result->SetNumberField(TEXT("lod_count"), LodValues.Num());

	TSharedPtr<FJsonObject> DataflowJson = MakeShared<FJsonObject>();
	DataflowJson->SetBoolField(TEXT("present"), true);
	DataflowJson->SetStringField(TEXT("terminal"), Asset->GetDataflowInstance().GetDataflowTerminal().ToString());
	DataflowJson->SetBoolField(TEXT("include_nodes_requested"), bIncludeNodes);
	DataflowJson->SetArrayField(TEXT("nodes"), TArray<TSharedPtr<FJsonValue>>());
	DataflowJson->SetNumberField(TEXT("node_count"), 0);
	Result->SetObjectField(TEXT("dataflow"), DataflowJson);
	return Result;
}

UClothingAssetExporter* FindChaosClothAssetExporter()
{
	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("ChaosClothAssetTools"));
	const TArray<IClothingAssetExporterClassProvider*> ClassProviders =
		IModularFeatures::Get().GetModularFeatureImplementations<IClothingAssetExporterClassProvider>(
			IClothingAssetExporterClassProvider::FeatureName);
	for (IClothingAssetExporterClassProvider* ClassProvider : ClassProviders)
	{
		if (!ClassProvider)
		{
			continue;
		}
		if (const TSubclassOf<UClothingAssetExporter> ExporterClass = ClassProvider->GetClothingAssetExporterClass())
		{
			UClothingAssetExporter* Exporter = ExporterClass->GetDefaultObject<UClothingAssetExporter>();
			if (Exporter && Exporter->GetExportedType() == UChaosClothAsset::StaticClass())
			{
				return Exporter;
			}
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> BuildLegacyClothPreservationSummary(UClothingAssetBase* Asset)
{
	TSharedPtr<FJsonObject> PreservedJson = MakeShared<FJsonObject>();
	PreservedJson->SetBoolField(TEXT("physics_asset"), false);
	PreservedJson->SetNumberField(TEXT("config_count"), 0);
	PreservedJson->SetNumberField(TEXT("weight_map_count"), 0);
	if (UClothingAssetCommon* Common = Cast<UClothingAssetCommon>(Asset))
	{
		PreservedJson->SetBoolField(TEXT("physics_asset"), Common->PhysicsAsset != nullptr);
		PreservedJson->SetNumberField(TEXT("config_count"), Common->ClothConfigs.Num());
		int32 WeightMapCount = 0;
		for (const FClothLODDataCommon& LodData : Common->LodData)
		{
			for (const FPointWeightMap& PointWeightMap : LodData.PointWeightMaps)
			{
#if WITH_EDITORONLY_DATA
				if (!IsBridgeSourceSectionMap(PointWeightMap.Name))
				{
					++WeightMapCount;
				}
#else
				++WeightMapCount;
#endif
			}
			WeightMapCount += LodData.PhysicalMeshData.WeightMaps.Num();
		}
		PreservedJson->SetNumberField(TEXT("weight_map_count"), WeightMapCount);
	}
	return PreservedJson;
}

bool OutputChaosClothAssetExists(const FString& OutputAssetPath, const FString& PackageName)
{
	FString PackageFilename;
	return StaticFindObject(nullptr, nullptr, *OutputAssetPath) != nullptr
		|| FindPackage(nullptr, *PackageName) != nullptr
		|| FBridgeAssetModifier::AssetExists(OutputAssetPath)
		|| FPackageName::DoesPackageExist(PackageName, &PackageFilename);
}

bool HasChaosClothCollectionData(UChaosClothAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	using namespace UE::Chaos::ClothAsset;
	const TArray<TSharedRef<const FManagedArrayCollection>>& Collections = Asset->GetClothCollections();
	for (const TSharedRef<const FManagedArrayCollection>& Collection : Collections)
	{
		FCollectionClothConstFacade Cloth(Collection);
		if (Cloth.IsValid()
			&& (Cloth.HasValidSimulationData()
				|| Cloth.HasValidRenderData()
				|| Cloth.GetNumSimVertices3D() > 0
				|| Cloth.GetNumRenderVertices() > 0))
		{
			return true;
		}
	}
	return false;
}

bool ValidateConvertedChaosClothAsset(UChaosClothAsset* Asset, FString& OutError)
{
	if (!Asset)
	{
		OutError = TEXT("cloth-convert: exporter did not create a Chaos Cloth Asset");
		return false;
	}

	if (ClothAssetHasDataflow(Asset) || Asset->HasValidClothSimulationModels() || HasChaosClothCollectionData(Asset))
	{
		return true;
	}

	OutError = TEXT("cloth-convert: exporter produced an empty Chaos Cloth Asset");
	return false;
}

bool CloneChaosClothCollectionsForLod(
	UChaosClothAsset* Asset,
	int32 LodIndex,
	TArray<TSharedRef<const FManagedArrayCollection>>& OutCollections,
	TSharedPtr<FManagedArrayCollection>& OutMutableCollection,
	FString& OutError)
{
	if (!Asset)
	{
		OutError = TEXT("cloth_asset is required");
		return false;
	}

	const TArray<TSharedRef<const FManagedArrayCollection>>& Collections = Asset->GetClothCollections();
	if (!Collections.IsValidIndex(LodIndex))
	{
		OutError = FString::Printf(TEXT("lod_index %d is out of range"), LodIndex);
		return false;
	}

	OutCollections.Reset(Collections.Num());
	OutMutableCollection.Reset();
	for (int32 Index = 0; Index < Collections.Num(); ++Index)
	{
		if (Index == LodIndex)
		{
			OutMutableCollection = MakeShared<FManagedArrayCollection>(*Collections[Index]);
			const TSharedRef<const FManagedArrayCollection> ConstCollection = OutMutableCollection.ToSharedRef();
			OutCollections.Add(ConstCollection);
		}
		else
		{
			OutCollections.Add(Collections[Index]);
		}
	}
	return true;
}

bool RebuildChaosClothAsset(
	UChaosClothAsset* Asset,
	const TArray<TSharedRef<const FManagedArrayCollection>>& Collections,
	FString& OutError)
{
	const TArray<TSharedRef<const FManagedArrayCollection>> OriginalCollections = Asset->GetClothCollections();
	FText ErrorText;
	FText VerboseText;
	Asset->Build(Collections, nullptr, &ErrorText, &VerboseText);
	if (!ErrorText.IsEmpty())
	{
		OutError = VerboseText.IsEmpty() ? ErrorText.ToString() : FString::Printf(TEXT("%s: %s"), *ErrorText.ToString(), *VerboseText.ToString());
		FText RollbackErrorText;
		FText RollbackVerboseText;
		Asset->Build(OriginalCollections, nullptr, &RollbackErrorText, &RollbackVerboseText);
		if (!RollbackErrorText.IsEmpty())
		{
			const FString RollbackError = RollbackVerboseText.IsEmpty()
				? RollbackErrorText.ToString()
				: FString::Printf(TEXT("%s: %s"), *RollbackErrorText.ToString(), *RollbackVerboseText.ToString());
			OutError = FString::Printf(TEXT("%s; rollback failed: %s"), *OutError, *RollbackError);
		}
		return false;
	}
	FBridgeAssetModifier::MarkPackageDirty(Asset);
	return true;
}

bool SaveChaosClothAssetIfRequested(
	UChaosClothAsset* Asset,
	bool bSave,
	TSharedPtr<FJsonObject>& Result,
	FString& OutError)
{
	if (bSave)
	{
		if (!FBridgeAssetModifier::SaveAsset(Asset, false, OutError))
		{
			Result->SetBoolField(TEXT("saved"), false);
			Result->SetStringField(TEXT("save_error"), OutError);
			return false;
		}
		Result->SetBoolField(TEXT("saved"), true);
		Result->SetBoolField(TEXT("needs_save"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetBoolField(TEXT("needs_save"), true);
	}
	return true;
}

bool ParseJsonIntArrayField(
	const TSharedPtr<FJsonObject>& Arguments,
	const FString& FieldName,
	bool bRequired,
	TArray<int32>& OutValues,
	FString& OutError)
{
	OutValues.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(FieldName, Values) || !Values)
	{
		if (bRequired)
		{
			OutError = FString::Printf(TEXT("%s array is required"), *FieldName);
			return false;
		}
		return true;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		int32 Index = INDEX_NONE;
		if (!Value.IsValid() || !Value->TryGetNumber(Index))
		{
			OutError = FString::Printf(TEXT("%s must contain integer values"), *FieldName);
			return false;
		}
		OutValues.Add(Index);
	}
	return true;
}

bool ParseJsonVector3Field(
	const TSharedPtr<FJsonObject>& Arguments,
	const FString& FieldName,
	FVector3f& OutVector,
	FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(FieldName, Values) || !Values)
	{
		return false;
	}
	if (Values->Num() != 3)
	{
		OutError = FString::Printf(TEXT("%s must contain exactly three numbers"), *FieldName);
		return false;
	}
	double Components[3] = { 0.0, 0.0, 0.0 };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Components[Index]))
		{
			OutError = FString::Printf(TEXT("%s must contain exactly three numbers"), *FieldName);
			return false;
		}
	}
	OutVector = FVector3f(static_cast<float>(Components[0]), static_cast<float>(Components[1]), static_cast<float>(Components[2]));
	return true;
}

bool SelectChaosWeightMapVertices(
	const TSharedPtr<FJsonObject>& Arguments,
	UE::Chaos::ClothAsset::FCollectionClothFacade& Cloth,
	TArray<int32>& OutVertices,
	FString& OutError)
{
	OutVertices.Reset();
	TSet<int32> Selected;
	TArray<int32> ExplicitVertices;
	if (!ParseJsonIntArrayField(Arguments, TEXT("vertices"), false, ExplicitVertices, OutError))
	{
		return false;
	}
	for (int32 VertexIndex : ExplicitVertices)
	{
		if (VertexIndex < 0 || VertexIndex >= Cloth.GetNumSimVertices3D())
		{
			OutError = FString::Printf(TEXT("sim 3D vertex index %d is out of range"), VertexIndex);
			return false;
		}
		Selected.Add(VertexIndex);
	}

	double ZMin = 0.0;
	double ZMax = 0.0;
	const bool bHasZMin = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("z_min"), ZMin);
	const bool bHasZMax = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("z_max"), ZMax);
	FVector3f Center = FVector3f::ZeroVector;
	FString CenterError;
	const bool bHasCenter = ParseJsonVector3Field(Arguments, TEXT("center"), Center, CenterError);
	if (!CenterError.IsEmpty())
	{
		OutError = CenterError;
		return false;
	}
	double Radius = 0.0;
	const bool bHasRadius = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("radius"), Radius);
	if (bHasCenter != bHasRadius)
	{
		OutError = TEXT("center and radius must be provided together");
		return false;
	}
	if (bHasRadius && Radius < 0.0)
	{
		OutError = TEXT("radius must be non-negative");
		return false;
	}
	if (bHasZMin && bHasZMax && ZMax < ZMin)
	{
		OutError = TEXT("z_max must be greater than or equal to z_min");
		return false;
	}

	const bool bHasSpatialSelection = bHasZMin || bHasZMax || bHasCenter;
	if (bHasSpatialSelection)
	{
		const TConstArrayView<FVector3f> Positions = Cloth.GetSimPosition3D();
		const float RadiusSq = static_cast<float>(Radius * Radius);
		for (int32 VertexIndex = 0; VertexIndex < Positions.Num(); ++VertexIndex)
		{
			const FVector3f& Position = Positions[VertexIndex];
			if (bHasZMin && Position.Z < static_cast<float>(ZMin))
			{
				continue;
			}
			if (bHasZMax && Position.Z > static_cast<float>(ZMax))
			{
				continue;
			}
			if (bHasCenter && FVector3f::DistSquared(Position, Center) > RadiusSq)
			{
				continue;
			}
			Selected.Add(VertexIndex);
		}
	}

	if (Selected.IsEmpty())
	{
		OutError = TEXT("selection did not match any simulation 3D vertices");
		return false;
	}
	for (int32 VertexIndex : Selected)
	{
		OutVertices.Add(VertexIndex);
	}
	OutVertices.Sort();
	return true;
}

bool SetWeightMap(
	UE::Chaos::ClothAsset::FCollectionClothFacade& Cloth,
	const FName& WeightMapName,
	const TArray<int32>& Vertices,
	float Value,
	TArray<float>& OutBeforeValues,
	TArray<float>& OutAfterValues,
	FString& OutError)
{
	if (!Cloth.HasWeightMap(WeightMapName))
	{
		Cloth.AddWeightMap(WeightMapName);
	}
	TArrayView<float> WeightMapValues = Cloth.GetWeightMap(WeightMapName);
	if (WeightMapValues.Num() < Cloth.GetNumSimVertices3D())
	{
		OutError = FString::Printf(TEXT("weight map %s has %d values for %d sim 3D vertices"), *WeightMapName.ToString(), WeightMapValues.Num(), Cloth.GetNumSimVertices3D());
		return false;
	}
	OutBeforeValues = TArray<float>(WeightMapValues);
	for (int32 VertexIndex : Vertices)
	{
		if (!WeightMapValues.IsValidIndex(VertexIndex))
		{
			OutError = FString::Printf(TEXT("sim 3D vertex index %d is out of range for weight map %s"), VertexIndex, *WeightMapName.ToString());
			return false;
		}
		WeightMapValues[VertexIndex] = Value;
	}
	OutAfterValues = TArray<float>(WeightMapValues);
	return true;
}

bool ParseJsonVertexPairs(
	const TSharedPtr<FJsonObject>& Arguments,
	TArray<FIntVector2>& OutPairs,
	FString& OutError)
{
	OutPairs.Reset();
	const TArray<TSharedPtr<FJsonValue>>* PairValues = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("vertex_pairs"), PairValues) || !PairValues)
	{
		OutError = TEXT("vertex_pairs array is required for mode=pairs");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& PairValue : *PairValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		if (!PairValue.IsValid() || !PairValue->TryGetArray(Pair) || !Pair || Pair->Num() != 2)
		{
			OutError = TEXT("vertex_pairs must be an array of [a, b] pairs");
			return false;
		}

		int32 First = INDEX_NONE;
		int32 Second = INDEX_NONE;
		if (!(*Pair)[0].IsValid() || !(*Pair)[0]->TryGetNumber(First)
			|| !(*Pair)[1].IsValid() || !(*Pair)[1]->TryGetNumber(Second))
		{
			OutError = TEXT("vertex_pairs entries must be integer pairs");
			return false;
		}
		OutPairs.Add(FIntVector2(First, Second));
	}

	if (OutPairs.IsEmpty())
	{
		OutError = TEXT("vertex_pairs must contain at least one pair");
		return false;
	}
	return true;
}

bool ConvertChaosClothIndexTo2D(
	UE::Chaos::ClothAsset::FCollectionClothFacade& Cloth,
	const FString& IndexSpace,
	int32 InputIndex,
	int32& OutIndex2D,
	FString& OutError)
{
	if (IndexSpace.Equals(TEXT("2d"), ESearchCase::IgnoreCase))
	{
		if (InputIndex < 0 || InputIndex >= Cloth.GetNumSimVertices2D())
		{
			OutError = FString::Printf(TEXT("sim 2D vertex index %d is out of range"), InputIndex);
			return false;
		}
		OutIndex2D = InputIndex;
		return true;
	}

	if (IndexSpace.Equals(TEXT("3d"), ESearchCase::IgnoreCase))
	{
		if (InputIndex < 0 || InputIndex >= Cloth.GetNumSimVertices3D())
		{
			OutError = FString::Printf(TEXT("sim 3D vertex index %d is out of range"), InputIndex);
			return false;
		}
		TConstArrayView<TArray<int32>> SimVertex2DLookup = Cloth.GetSimVertex2DLookup();
		if (!SimVertex2DLookup.IsValidIndex(InputIndex) || SimVertex2DLookup[InputIndex].IsEmpty())
		{
			OutError = FString::Printf(TEXT("sim 3D vertex index %d has no 2D vertex lookup"), InputIndex);
			return false;
		}
		OutIndex2D = SimVertex2DLookup[InputIndex][0];
		return true;
	}

	OutError = TEXT("index_space must be 2d or 3d");
	return false;
}

bool GetChaosClothInputPosition(
	UE::Chaos::ClothAsset::FCollectionClothFacade& Cloth,
	const FString& IndexSpace,
	int32 InputIndex,
	FVector3f& OutPosition,
	FString& OutError)
{
	if (IndexSpace.Equals(TEXT("2d"), ESearchCase::IgnoreCase))
	{
		if (InputIndex < 0 || InputIndex >= Cloth.GetNumSimVertices2D())
		{
			OutError = FString::Printf(TEXT("sim 2D vertex index %d is out of range"), InputIndex);
			return false;
		}
		const FVector2f Position = Cloth.GetSimPosition2D()[InputIndex];
		OutPosition = FVector3f(Position.X, Position.Y, 0.0f);
		return true;
	}

	if (InputIndex < 0 || InputIndex >= Cloth.GetNumSimVertices3D())
	{
		OutError = FString::Printf(TEXT("sim 3D vertex index %d is out of range"), InputIndex);
		return false;
	}
	OutPosition = Cloth.GetSimPosition3D()[InputIndex];
	return true;
}

bool BuildChaosStitchPairs(
	const TSharedPtr<FJsonObject>& Arguments,
	UE::Chaos::ClothAsset::FCollectionClothFacade& Cloth,
	TArray<FIntVector2>& OutPairs,
	FString& OutError)
{
	FString Mode = TEXT("pairs");
	FString IndexSpace = TEXT("2d");
	if (Arguments.IsValid())
	{
		Arguments->TryGetStringField(TEXT("mode"), Mode);
		Arguments->TryGetStringField(TEXT("index_space"), IndexSpace);
	}
	OutPairs.Reset();

	if (Mode.Equals(TEXT("pairs"), ESearchCase::IgnoreCase))
	{
		TArray<FIntVector2> RawPairs;
		if (!ParseJsonVertexPairs(Arguments, RawPairs, OutError))
		{
			return false;
		}
		for (const FIntVector2& Pair : RawPairs)
		{
			int32 First2D = INDEX_NONE;
			int32 Second2D = INDEX_NONE;
			if (!ConvertChaosClothIndexTo2D(Cloth, IndexSpace, Pair.X, First2D, OutError)
				|| !ConvertChaosClothIndexTo2D(Cloth, IndexSpace, Pair.Y, Second2D, OutError))
			{
				return false;
			}
			OutPairs.Add(FIntVector2(First2D, Second2D));
		}
		return true;
	}

	if (!Mode.Equals(TEXT("proximity"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("mode must be pairs or proximity");
		return false;
	}

	TArray<int32> FirstVertices;
	TArray<int32> SecondVertices;
	if (!ParseJsonIntArrayField(Arguments, TEXT("first_vertices"), true, FirstVertices, OutError)
		|| !ParseJsonIntArrayField(Arguments, TEXT("second_vertices"), true, SecondVertices, OutError))
	{
		return false;
	}
	double ToleranceNumber = -1.0;
	if (Arguments.IsValid())
	{
		Arguments->TryGetNumberField(TEXT("tolerance"), ToleranceNumber);
	}
	const float Tolerance = static_cast<float>(ToleranceNumber);
	if (Tolerance < 0.0f)
	{
		OutError = TEXT("tolerance must be non-negative for mode=proximity");
		return false;
	}

	TSet<int32> UsedSecondPositions;
	const float ToleranceSq = Tolerance * Tolerance;
	for (int32 FirstIndex : FirstVertices)
	{
		FVector3f FirstPosition;
		if (!GetChaosClothInputPosition(Cloth, IndexSpace, FirstIndex, FirstPosition, OutError))
		{
			return false;
		}

		int32 BestSecondPosition = INDEX_NONE;
		float BestDistanceSq = ToleranceSq;
		for (int32 CandidatePosition = 0; CandidatePosition < SecondVertices.Num(); ++CandidatePosition)
		{
			if (UsedSecondPositions.Contains(CandidatePosition))
			{
				continue;
			}

			FVector3f SecondPosition;
			if (!GetChaosClothInputPosition(Cloth, IndexSpace, SecondVertices[CandidatePosition], SecondPosition, OutError))
			{
				return false;
			}

			const float DistanceSq = FVector3f::DistSquared(FirstPosition, SecondPosition);
			if (DistanceSq <= BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				BestSecondPosition = CandidatePosition;
			}
		}

		if (BestSecondPosition != INDEX_NONE)
		{
			int32 First2D = INDEX_NONE;
			int32 Second2D = INDEX_NONE;
			if (!ConvertChaosClothIndexTo2D(Cloth, IndexSpace, FirstIndex, First2D, OutError)
				|| !ConvertChaosClothIndexTo2D(Cloth, IndexSpace, SecondVertices[BestSecondPosition], Second2D, OutError))
			{
				return false;
			}
			OutPairs.Add(FIntVector2(First2D, Second2D));
			UsedSecondPositions.Add(BestSecondPosition);
		}
	}

	if (OutPairs.IsEmpty())
	{
		OutError = TEXT("proximity mode did not find any vertex pairs within tolerance");
		return false;
	}
	return true;
}

TSharedPtr<FJsonValue> ChaosConfigChangedValueToJson(const FString& PropertyName)
{
	return MakeShared<FJsonValueString>(PropertyName);
}

bool ApplyChaosConfigPropertyValue(
	Chaos::Softs::FCollectionPropertyFacade& PropertyFacade,
	const FString& PropertyName,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	const FName PropertyKey(*PropertyName);
	if (PropertyFacade.GetKeyNameIndex(PropertyKey) == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("config property not found on cloth collection: %s"), *PropertyName);
		return false;
	}

	if (!Value.IsValid())
	{
		OutError = FString::Printf(TEXT("config property value is invalid: %s"), *PropertyName);
		return false;
	}

	bool BoolValue = false;
	if (Value->TryGetBool(BoolValue))
	{
		PropertyFacade.SetValue<bool>(PropertyKey, BoolValue);
		return true;
	}

	double NumberValue = 0.0;
	if (Value->TryGetNumber(NumberValue))
	{
		const double Rounded = FMath::RoundToDouble(NumberValue);
		if (FMath::IsNearlyEqual(NumberValue, Rounded))
		{
			PropertyFacade.SetValue<int32>(PropertyKey, static_cast<int32>(Rounded));
		}
		else
		{
			PropertyFacade.SetValue<float>(PropertyKey, static_cast<float>(NumberValue));
		}
		return true;
	}

	FString StringValue;
	if (Value->TryGetString(StringValue))
	{
		PropertyFacade.SetStringValue(PropertyKey, StringValue);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
	if (Value->TryGetArray(ArrayValue) && ArrayValue)
	{
		if (ArrayValue->Num() == 2)
		{
			double Low = 0.0;
			double High = 0.0;
			if (!(*ArrayValue)[0].IsValid() || !(*ArrayValue)[0]->TryGetNumber(Low)
				|| !(*ArrayValue)[1].IsValid() || !(*ArrayValue)[1]->TryGetNumber(High))
			{
				OutError = FString::Printf(TEXT("weighted config property must contain numeric [low, high]: %s"), *PropertyName);
				return false;
			}
			PropertyFacade.SetWeightedFloatValue(PropertyKey, FVector2f(static_cast<float>(Low), static_cast<float>(High)));
			return true;
		}
		if (ArrayValue->Num() == 3)
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if (!(*ArrayValue)[0].IsValid() || !(*ArrayValue)[0]->TryGetNumber(X)
				|| !(*ArrayValue)[1].IsValid() || !(*ArrayValue)[1]->TryGetNumber(Y)
				|| !(*ArrayValue)[2].IsValid() || !(*ArrayValue)[2]->TryGetNumber(Z))
			{
				OutError = FString::Printf(TEXT("vector config property must contain numeric [x, y, z]: %s"), *PropertyName);
				return false;
			}
			PropertyFacade.SetValue<FVector3f>(PropertyKey, FVector3f(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z)));
			return true;
		}

		OutError = FString::Printf(TEXT("array config property values must have 2 or 3 elements: %s"), *PropertyName);
		return false;
	}

	const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
	if (ObjectValue.IsValid())
	{
		bool bApplied = false;
		double Low = 0.0;
		double High = 0.0;
		if (ObjectValue->TryGetNumberField(TEXT("low"), Low))
		{
			PropertyFacade.SetLowValue<float>(PropertyKey, static_cast<float>(Low));
			bApplied = true;
		}
		if (ObjectValue->TryGetNumberField(TEXT("high"), High))
		{
			PropertyFacade.SetHighValue<float>(PropertyKey, static_cast<float>(High));
			bApplied = true;
		}
		if (bApplied)
		{
			return true;
		}
	}

	OutError = FString::Printf(TEXT("unsupported config property JSON value for %s"), *PropertyName);
	return false;
}

TSharedPtr<FJsonObject> ClothAssetToJson(UClothingAssetBase* Asset)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!Asset)
	{
		return Json;
	}

	Json->SetStringField(TEXT("name"), Asset->GetName());
	Json->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
	Json->SetStringField(TEXT("guid"), Asset->GetAssetGuid().ToString(EGuidFormats::DigitsWithHyphens));
	Json->SetNumberField(TEXT("num_lods"), Asset->GetNumLods());

	if (UClothingAssetCommon* Common = Cast<UClothingAssetCommon>(Asset))
	{
		Json->SetStringField(
			TEXT("physics_asset"),
			Common->PhysicsAsset ? Common->PhysicsAsset->GetPathName() : TEXT(""));

		TArray<TSharedPtr<FJsonValue>> Configs;
		for (const TPair<FName, TObjectPtr<UClothConfigBase>>& Pair : Common->ClothConfigs)
		{
			TSharedPtr<FJsonObject> ConfigJson = MakeShared<FJsonObject>();
			ConfigJson->SetStringField(TEXT("key"), Pair.Key.ToString());
			ConfigJson->SetStringField(TEXT("class"), Pair.Value ? Pair.Value->GetClass()->GetName() : TEXT(""));
			Configs.Add(MakeShared<FJsonValueObject>(ConfigJson));
		}
		Json->SetArrayField(TEXT("configs"), Configs);

		TArray<TSharedPtr<FJsonValue>> BoneNames;
		for (const FName& BoneName : Common->UsedBoneNames)
		{
			BoneNames.Add(MakeShared<FJsonValueString>(BoneName.ToString()));
		}
		Json->SetArrayField(TEXT("used_bone_names"), BoneNames);

		TArray<TSharedPtr<FJsonValue>> Lods;
		for (int32 LodIndex = 0; LodIndex < Common->LodData.Num(); ++LodIndex)
		{
			const FClothLODDataCommon& LodData = Common->LodData[LodIndex];
			const FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
			TSharedPtr<FJsonObject> LodJson = MakeShared<FJsonObject>();
			LodJson->SetNumberField(TEXT("lod_index"), LodIndex);
			LodJson->SetNumberField(TEXT("vertex_count"), PhysicalMesh.Vertices.Num());
			LodJson->SetNumberField(TEXT("index_count"), PhysicalMesh.Indices.Num());
			LodJson->SetObjectField(
				TEXT("max_distance"),
				WeightMapStatsToJson(PhysicalMesh.FindWeightMap(EWeightMapTargetCommon::MaxDistance)));
			Lods.Add(MakeShared<FJsonValueObject>(LodJson));
		}
		Json->SetArrayField(TEXT("lods"), Lods);
	}

	return Json;
}

TArray<TSharedPtr<FJsonValue>> BuildBindingArray(
	const TArray<BridgeClothBindings::FBindingRecord>& Bindings)
{
	TArray<TSharedPtr<FJsonValue>> BindingValues;
	for (const BridgeClothBindings::FBindingRecord& Binding : Bindings)
	{
		TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
		BindingJson->SetStringField(TEXT("asset_name"), Binding.Asset ? Binding.Asset->GetName() : TEXT(""));
		BindingJson->SetNumberField(TEXT("lod_index"), Binding.LodIndex);
		BindingJson->SetNumberField(TEXT("section_index"), Binding.SectionIndex);
		BindingJson->SetNumberField(TEXT("asset_lod_index"), Binding.AssetLodIndex);
		BindingValues.Add(MakeShared<FJsonValueObject>(BindingJson));
	}
	return BindingValues;
}

TSharedPtr<FJsonObject> BuildQueryResult(
	USkeletalMesh* Mesh,
	const FString& SkeletalMeshPath,
	const FString& FilterAssetName = TEXT(""),
	int32 LodFilter = INDEX_NONE)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);

	TArray<TSharedPtr<FJsonValue>> AssetValues;
	for (UClothingAssetBase* Asset : Mesh->GetMeshClothingAssets())
	{
		if (!Asset)
		{
			continue;
		}
		if (!FilterAssetName.IsEmpty() && !Asset->GetName().Equals(FilterAssetName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		AssetValues.Add(MakeShared<FJsonValueObject>(ClothAssetToJson(Asset)));
	}
	Result->SetArrayField(TEXT("cloth_assets"), AssetValues);
	Result->SetNumberField(TEXT("cloth_asset_count"), AssetValues.Num());

	const BridgeClothBindings::FBindingQueryResult BindingResult = BridgeClothBindings::Collect(Mesh, LodFilter);
	TArray<TSharedPtr<FJsonValue>> Bindings = BuildBindingArray(BindingResult.Bindings);
	Result->SetArrayField(TEXT("bindings"), Bindings);
	Result->SetNumberField(TEXT("binding_count"), Bindings.Num());
	TArray<TSharedPtr<FJsonValue>> BindingWarnings;
	for (const BridgeClothBindings::FBindingWarning& Warning : BindingResult.Warnings)
	{
		TSharedPtr<FJsonObject> WarningJson = MakeShared<FJsonObject>();
		WarningJson->SetNumberField(TEXT("lod_index"), Warning.LodIndex);
		WarningJson->SetNumberField(TEXT("section_index"), Warning.SectionIndex);
		WarningJson->SetStringField(TEXT("reason"), Warning.Reason);
		if (Warning.AssetGuid.IsSet())
		{
			WarningJson->SetStringField(TEXT("asset_guid"), Warning.AssetGuid.GetValue().ToString(EGuidFormats::DigitsWithHyphens));
		}
		BindingWarnings.Add(MakeShared<FJsonValueObject>(WarningJson));
	}
	Result->SetArrayField(TEXT("binding_warnings"), BindingWarnings);
	Result->SetNumberField(TEXT("binding_warning_count"), BindingWarnings.Num());
	if (LodFilter != INDEX_NONE)
	{
		Result->SetNumberField(TEXT("lod_index"), LodFilter);
	}
	return Result;
}

UClothConfigBase* ResolveClothConfig(UClothingAssetCommon* Asset, const FString& ConfigClass)
{
	if (!Asset)
	{
		return nullptr;
	}

	if (ConfigClass.IsEmpty())
	{
		for (const TPair<FName, TObjectPtr<UClothConfigBase>>& Pair : Asset->ClothConfigs)
		{
			if (Pair.Value)
			{
				return Pair.Value;
			}
		}
		return nullptr;
	}

	for (const TPair<FName, TObjectPtr<UClothConfigBase>>& Pair : Asset->ClothConfigs)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const FString Key = Pair.Key.ToString();
		const FString ClassName = Pair.Value->GetClass()->GetName();
		const FString ClassPath = Pair.Value->GetClass()->GetPathName();
		if (Key.Equals(ConfigClass, ESearchCase::IgnoreCase)
			|| ClassName.Equals(ConfigClass, ESearchCase::IgnoreCase)
			|| ClassPath.Equals(ConfigClass, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}
	return nullptr;
}

float VertexColorChannelToFloat(const FColor& Color, const FString& Channel)
{
	if (Channel.Equals(TEXT("green"), ESearchCase::IgnoreCase))
	{
		return Color.G / 255.0f;
	}
	if (Channel.Equals(TEXT("blue"), ESearchCase::IgnoreCase))
	{
		return Color.B / 255.0f;
	}
	if (Channel.Equals(TEXT("alpha"), ESearchCase::IgnoreCase))
	{
		return Color.A / 255.0f;
	}
	return Color.R / 255.0f;
}

bool ResolveRefBoneLocation(USkeletalMesh* Mesh, const FString& RootBone, FVector& OutLocation, FString& OutError)
{
	if (!Mesh || RootBone.IsEmpty())
	{
		OutError = TEXT("cloth: root_bone is required for bone-distance weight maps");
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*RootBone));
	if (BoneIndex == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("cloth: root_bone does not exist on skeletal mesh: %s"), *RootBone);
		return false;
	}

	FTransform ComponentTransform = RefSkeleton.GetRefBonePose()[BoneIndex];
	for (int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		ParentIndex != INDEX_NONE;
		ParentIndex = RefSkeleton.GetParentIndex(ParentIndex))
	{
		ComponentTransform = ComponentTransform * RefSkeleton.GetRefBonePose()[ParentIndex];
	}

	OutLocation = ComponentTransform.GetLocation();
	return true;
}

bool IsSupportedFalloffCurve(const FString& Curve)
{
	return Curve.Equals(TEXT("linear"), ESearchCase::IgnoreCase)
		|| Curve.Equals(TEXT("smooth"), ESearchCase::IgnoreCase)
		|| Curve.Equals(TEXT("ease"), ESearchCase::IgnoreCase);
}

float ApplyFalloffCurve(float Alpha, const FString& Curve)
{
	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (Curve.Equals(TEXT("smooth"), ESearchCase::IgnoreCase))
	{
		return Alpha * Alpha * (3.0f - 2.0f * Alpha);
	}
	if (Curve.Equals(TEXT("ease"), ESearchCase::IgnoreCase))
	{
		return 0.5f - 0.5f * FMath::Cos(Alpha * 3.14159265358979323846f);
	}
	return Alpha;
}

FVector PhysicalVertexToVector(const FVector3f& Vertex)
{
	return FVector(static_cast<double>(Vertex.X), static_cast<double>(Vertex.Y), static_cast<double>(Vertex.Z));
}

bool BuildBoneDistanceFalloffValues(
	const TArray<float>& BoneDistances,
	float MinDistance,
	float MaxDistance,
	const FString& Curve,
	bool bInvert,
	TArray<float>& OutValues,
	FString& OutError)
{
	OutValues.Reset();
	if (BoneDistances.IsEmpty())
	{
		OutError = TEXT("cloth: bone-distance rule requires at least one cloth vertex");
		return false;
	}
	if (MinDistance < 0.0f || MaxDistance < 0.0f || MaxDistance < MinDistance)
	{
		OutError = TEXT("cloth: min_distance and max_distance must be non-negative, with max_distance >= min_distance");
		return false;
	}
	if (!IsSupportedFalloffCurve(Curve))
	{
		OutError = TEXT("cloth: curve must be linear, smooth, or ease");
		return false;
	}

	float NearestBoneDistance = BoneDistances[0];
	float FarthestBoneDistance = BoneDistances[0];
	for (float BoneDistance : BoneDistances)
	{
		NearestBoneDistance = FMath::Min(NearestBoneDistance, BoneDistance);
		FarthestBoneDistance = FMath::Max(FarthestBoneDistance, BoneDistance);
	}

	const float BoneDistanceRange = FarthestBoneDistance - NearestBoneDistance;
	if (FMath::IsNearlyZero(BoneDistanceRange))
	{
		OutError = TEXT("cloth: bone-distance rule requires non-uniform distances from root_bone");
		return false;
	}

	OutValues.SetNum(BoneDistances.Num());
	for (int32 Index = 0; Index < BoneDistances.Num(); ++Index)
	{
		float Alpha = (BoneDistances[Index] - NearestBoneDistance) / BoneDistanceRange;
		if (bInvert)
		{
			Alpha = 1.0f - Alpha;
		}
		Alpha = ApplyFalloffCurve(Alpha, Curve);
		OutValues[Index] = FMath::Lerp(MinDistance, MaxDistance, Alpha);
	}
	return true;
}

struct FLegacyClothWeldResult
{
	int32 OriginalVertexCount = 0;
	int32 FinalVertexCount = 0;
	int32 SelectedVertexCount = 0;
	int32 WeldedVertexCount = 0;
	int32 WeldGroupCount = 0;
	int32 RemovedDegenerateTriangleCount = 0;
};

struct FLegacyClothSectionMapping
{
	int32 MeshLodIndex = INDEX_NONE;
	int32 SectionIndex = INDEX_NONE;
	TArray<FMeshToMeshVertData> MappingData;
};

bool BuildLegacyClothWeldSelection(
	const TSharedPtr<FJsonObject>& Arguments,
	const FClothPhysicalMeshData& PhysicalMesh,
	TArray<bool>& OutSelected,
	FString& OutError)
{
	OutSelected.Init(false, PhysicalMesh.Vertices.Num());

	double ZMin = 0.0;
	double ZMax = 0.0;
	const bool bHasZMin = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("z_min"), ZMin);
	const bool bHasZMax = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("z_max"), ZMax);
	FVector3f Center = FVector3f::ZeroVector;
	FString CenterError;
	const bool bHasCenter = ParseJsonVector3Field(Arguments, TEXT("center"), Center, CenterError);
	if (!CenterError.IsEmpty())
	{
		OutError = CenterError;
		return false;
	}
	double Radius = 0.0;
	const bool bHasRadius = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("radius"), Radius);
	if (bHasCenter != bHasRadius)
	{
		OutError = TEXT("center and radius must be provided together");
		return false;
	}
	if (bHasRadius && Radius < 0.0)
	{
		OutError = TEXT("radius must be non-negative");
		return false;
	}
	if (bHasZMin && bHasZMax && ZMax < ZMin)
	{
		OutError = TEXT("z_max must be greater than or equal to z_min");
		return false;
	}

	const bool bHasSpatialSelection = bHasZMin || bHasZMax || bHasCenter;
	const float RadiusSq = static_cast<float>(Radius * Radius);
	for (int32 VertexIndex = 0; VertexIndex < PhysicalMesh.Vertices.Num(); ++VertexIndex)
	{
		const FVector3f& Position = PhysicalMesh.Vertices[VertexIndex];
		bool bSelected = true;
		if (bHasZMin && Position.Z < static_cast<float>(ZMin))
		{
			bSelected = false;
		}
		if (bHasZMax && Position.Z > static_cast<float>(ZMax))
		{
			bSelected = false;
		}
		if (bHasCenter && FVector3f::DistSquared(Position, Center) > RadiusSq)
		{
			bSelected = false;
		}
		OutSelected[VertexIndex] = bHasSpatialSelection ? bSelected : true;
	}
	return true;
}

bool BuildSpatialWeightMapValues(
	const TSharedPtr<FJsonObject>& Arguments,
	const FClothLODDataCommon& LodData,
	const FBridgeLegacyWeightMapTarget& Target,
	TArray<float>& OutValues,
	int32& OutSelectedVertexCount,
	FString& OutError)
{
	const FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
	OutSelectedVertexCount = 0;
	ReadBridgeLegacyWeightMapValues(LodData, Target, OutValues);

	TArray<bool> SelectedVertices;
	if (!BuildLegacyClothWeldSelection(Arguments, PhysicalMesh, SelectedVertices, OutError))
	{
		return false;
	}
	for (bool bSelected : SelectedVertices)
	{
		if (bSelected)
		{
			++OutSelectedVertexCount;
		}
	}
	if (OutSelectedVertexCount == 0)
	{
		OutError = TEXT("cloth: spatial weight selection did not match any physical mesh vertices");
		return false;
	}

	double ConstantValue = 0.0;
	const bool bHasConstantValue = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("value"), ConstantValue);
	double MinValue = 0.0;
	double MaxValue = 0.0;
	const bool bHasMinValue = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("min_value"), MinValue);
	const bool bHasMaxValue = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("max_value"), MaxValue);
	if (bHasConstantValue && (bHasMinValue || bHasMaxValue))
	{
		OutError = TEXT("cloth: spatial rule cannot combine value with min_value/max_value");
		return false;
	}
	if (bHasMinValue != bHasMaxValue)
	{
		OutError = TEXT("cloth: spatial rule requires min_value and max_value together");
		return false;
	}
	if (!bHasConstantValue && !bHasMinValue)
	{
		OutError = TEXT("cloth: spatial rule requires value or min_value/max_value");
		return false;
	}

	if (bHasConstantValue)
	{
		for (int32 Index = 0; Index < SelectedVertices.Num(); ++Index)
		{
			if (SelectedVertices[Index])
			{
				OutValues[Index] = static_cast<float>(ConstantValue);
			}
		}
		return true;
	}

	FString Curve = TEXT("linear");
	if (Arguments.IsValid())
	{
		Arguments->TryGetStringField(TEXT("curve"), Curve);
	}
	if (!IsSupportedFalloffCurve(Curve))
	{
		OutError = TEXT("cloth: curve must be linear, smooth, or ease");
		return false;
	}
	double ExplicitZMin = 0.0;
	double ExplicitZMax = 0.0;
	const bool bHasZMin = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("z_min"), ExplicitZMin);
	const bool bHasZMax = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("z_max"), ExplicitZMax);

	float RampZMin = 0.0f;
	float RampZMax = 0.0f;
	bool bFoundRampRange = false;
	if (bHasZMin && bHasZMax)
	{
		RampZMin = static_cast<float>(ExplicitZMin);
		RampZMax = static_cast<float>(ExplicitZMax);
		bFoundRampRange = true;
	}
	else
	{
		for (int32 Index = 0; Index < SelectedVertices.Num(); ++Index)
		{
			if (!SelectedVertices[Index])
			{
				continue;
			}
			const float Z = PhysicalMesh.Vertices[Index].Z;
			if (!bFoundRampRange)
			{
				RampZMin = Z;
				RampZMax = Z;
				bFoundRampRange = true;
			}
			else
			{
				RampZMin = FMath::Min(RampZMin, Z);
				RampZMax = FMath::Max(RampZMax, Z);
			}
		}
	}

	const float RampRange = RampZMax - RampZMin;
	if (FMath::IsNearlyZero(RampRange))
	{
		OutError = TEXT("cloth: spatial ramp requires a non-zero Z range");
		return false;
	}

	for (int32 Index = 0; Index < SelectedVertices.Num(); ++Index)
	{
		if (!SelectedVertices[Index])
		{
			continue;
		}
		float Alpha = (PhysicalMesh.Vertices[Index].Z - RampZMin) / RampRange;
		Alpha = ApplyFalloffCurve(Alpha, Curve);
		OutValues[Index] = FMath::Lerp(static_cast<float>(MinValue), static_cast<float>(MaxValue), Alpha);
	}
	return true;
}

void RemapLegacyClothWeightMaps(
	TMap<uint32, FPointWeightMap>& WeightMaps,
	const TArray<TArray<int32>>& NewVertexToOldVertices)
{
	for (TPair<uint32, FPointWeightMap>& Pair : WeightMaps)
	{
		const TArray<float> OldValues = Pair.Value.Values;
		TArray<float> NewValues;
		NewValues.SetNum(NewVertexToOldVertices.Num());
		for (int32 NewIndex = 0; NewIndex < NewVertexToOldVertices.Num(); ++NewIndex)
		{
			float Value = 0.0f;
			bool bHasValue = false;
			for (int32 OldIndex : NewVertexToOldVertices[NewIndex])
			{
				if (!OldValues.IsValidIndex(OldIndex))
				{
					continue;
				}
				Value = bHasValue ? FMath::Min(Value, OldValues[OldIndex]) : OldValues[OldIndex];
				bHasValue = true;
			}
			NewValues[NewIndex] = Value;
		}
		Pair.Value.Values = MoveTemp(NewValues);
	}
}

void RemapLegacyClothPointWeightMaps(
	TArray<FPointWeightMap>& PointWeightMaps,
	const TArray<TArray<int32>>& NewVertexToOldVertices)
{
	for (FPointWeightMap& WeightMap : PointWeightMaps)
	{
		const bool bSourceSectionMap = IsBridgeSourceSectionMap(WeightMap.Name);
		const TArray<float> OldValues = WeightMap.Values;
		TArray<float> NewValues;
		NewValues.SetNum(NewVertexToOldVertices.Num());
		for (int32 NewIndex = 0; NewIndex < NewVertexToOldVertices.Num(); ++NewIndex)
		{
			float Value = 0.0f;
			bool bHasValue = false;
			for (int32 OldIndex : NewVertexToOldVertices[NewIndex])
			{
				if (!OldValues.IsValidIndex(OldIndex))
				{
					continue;
				}
				Value = bHasValue
					? (bSourceSectionMap ? FMath::Max(Value, OldValues[OldIndex]) : FMath::Min(Value, OldValues[OldIndex]))
					: OldValues[OldIndex];
				bHasValue = true;
			}
			NewValues[NewIndex] = Value;
		}
		WeightMap.Values = MoveTemp(NewValues);
	}
}

void RestorePhysicalOnlyLegacyClothWeightMaps(
	FClothPhysicalMeshData& PhysicalMesh,
	const TMap<uint32, FPointWeightMap>& RemappedWeightMaps)
{
	for (const TPair<uint32, FPointWeightMap>& Pair : RemappedWeightMaps)
	{
		if (!PhysicalMesh.WeightMaps.Contains(Pair.Key))
		{
			PhysicalMesh.WeightMaps.Add(Pair.Key, Pair.Value);
		}
	}
}

bool WeldLegacyPhysicalMeshVertices(
	FClothLODDataCommon& LodData,
	float Tolerance,
	const TArray<bool>& SelectedVertices,
	FLegacyClothWeldResult& OutResult,
	FString& OutError)
{
	FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
	const int32 VertexCount = PhysicalMesh.Vertices.Num();
	OutResult.OriginalVertexCount = VertexCount;
	OutResult.FinalVertexCount = VertexCount;
	if (VertexCount <= 0)
	{
		OutError = TEXT("cloth: physical mesh has no vertices");
		return false;
	}
	if (SelectedVertices.Num() != VertexCount)
	{
		OutError = TEXT("cloth: weld selection does not match physical mesh vertex count");
		return false;
	}

	TArray<int32> Parent;
	Parent.SetNum(VertexCount);
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		Parent[Index] = Index;
		if (SelectedVertices[Index])
		{
			++OutResult.SelectedVertexCount;
		}
	}
	if (OutResult.SelectedVertexCount == 0)
	{
		OutError = TEXT("cloth: weld selection did not match any physical mesh vertices");
		return false;
	}

	auto FindRoot = [&Parent](int32 Index)
	{
		int32 Root = Index;
		while (Parent[Root] != Root)
		{
			Root = Parent[Root];
		}
		while (Parent[Index] != Index)
		{
			const int32 Next = Parent[Index];
			Parent[Index] = Root;
			Index = Next;
		}
		return Root;
	};
	auto UnionVertices = [&Parent, &FindRoot](int32 A, int32 B)
	{
		const int32 RootA = FindRoot(A);
		const int32 RootB = FindRoot(B);
		if (RootA == RootB)
		{
			return;
		}
		const int32 NewRoot = FMath::Min(RootA, RootB);
		Parent[RootA] = NewRoot;
		Parent[RootB] = NewRoot;
	};

	const float ToleranceSq = FMath::Square(Tolerance);
	const float CellSize = FMath::Max(Tolerance, KINDA_SMALL_NUMBER);
	TMap<FIntVector, TArray<int32>> SpatialBuckets;
	auto MakeBucket = [CellSize](const FVector3f& Position)
	{
		return FIntVector(
			FMath::FloorToInt(Position.X / CellSize),
			FMath::FloorToInt(Position.Y / CellSize),
			FMath::FloorToInt(Position.Z / CellSize));
	};

	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		if (!SelectedVertices[VertexIndex])
		{
			continue;
		}
		const FIntVector Bucket = MakeBucket(PhysicalMesh.Vertices[VertexIndex]);
		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 Z = -1; Z <= 1; ++Z)
				{
					const FIntVector NeighborBucket(Bucket.X + X, Bucket.Y + Y, Bucket.Z + Z);
					if (const TArray<int32>* Candidates = SpatialBuckets.Find(NeighborBucket))
					{
						for (int32 CandidateIndex : *Candidates)
						{
							if (FVector3f::DistSquared(PhysicalMesh.Vertices[VertexIndex], PhysicalMesh.Vertices[CandidateIndex]) <= ToleranceSq)
							{
								UnionVertices(VertexIndex, CandidateIndex);
							}
						}
					}
				}
			}
		}
		SpatialBuckets.FindOrAdd(Bucket).Add(VertexIndex);
	}

	TMap<int32, TArray<int32>> RootToOldVertices;
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		RootToOldVertices.FindOrAdd(FindRoot(VertexIndex)).Add(VertexIndex);
	}

	TArray<int32> SortedRoots;
	RootToOldVertices.GetKeys(SortedRoots);
	SortedRoots.Sort();

	TMap<int32, int32> RootToNewVertex;
	TArray<TArray<int32>> NewVertexToOldVertices;
	TArray<int32> OldToNewVertex;
	OldToNewVertex.SetNum(VertexCount);
	for (int32 NewIndex = 0; NewIndex < SortedRoots.Num(); ++NewIndex)
	{
		const int32 Root = SortedRoots[NewIndex];
		RootToNewVertex.Add(Root, NewIndex);
		NewVertexToOldVertices.Add(RootToOldVertices[Root]);
		if (NewVertexToOldVertices.Last().Num() > 1)
		{
			++OutResult.WeldGroupCount;
		}
		for (int32 OldIndex : NewVertexToOldVertices.Last())
		{
			OldToNewVertex[OldIndex] = NewIndex;
		}
	}

	if (OutResult.WeldGroupCount == 0)
	{
		return true;
	}

	TArray<FVector3f> NewVertices;
	TArray<FVector3f> NewNormals;
	TArray<FClothVertBoneData> NewBoneData;
	TArray<float> NewInverseMasses;
#if WITH_EDITORONLY_DATA
	TArray<FColor> NewVertexColors;
#endif
	NewVertices.SetNum(NewVertexToOldVertices.Num());
	NewNormals.SetNum(NewVertexToOldVertices.Num());
	NewBoneData.SetNum(NewVertexToOldVertices.Num());
	NewInverseMasses.SetNum(NewVertexToOldVertices.Num());
#if WITH_EDITORONLY_DATA
	NewVertexColors.SetNum(PhysicalMesh.VertexColors.Num() == VertexCount ? NewVertexToOldVertices.Num() : 0);
#endif

	for (int32 NewIndex = 0; NewIndex < NewVertexToOldVertices.Num(); ++NewIndex)
	{
		const int32 RepresentativeIndex = NewVertexToOldVertices[NewIndex][0];
		NewVertices[NewIndex] = PhysicalMesh.Vertices[RepresentativeIndex];
		NewNormals[NewIndex] = PhysicalMesh.Normals.IsValidIndex(RepresentativeIndex) ? PhysicalMesh.Normals[RepresentativeIndex] : FVector3f::UnitZ();
		NewBoneData[NewIndex] = PhysicalMesh.BoneData.IsValidIndex(RepresentativeIndex) ? PhysicalMesh.BoneData[RepresentativeIndex] : FClothVertBoneData();
		NewInverseMasses[NewIndex] = PhysicalMesh.InverseMasses.IsValidIndex(RepresentativeIndex) ? PhysicalMesh.InverseMasses[RepresentativeIndex] : 0.0f;
#if WITH_EDITORONLY_DATA
		if (NewVertexColors.Num() > 0)
		{
			NewVertexColors[NewIndex] = PhysicalMesh.VertexColors[RepresentativeIndex];
		}
#endif
	}

	TArray<uint32> NewIndices;
	if (PhysicalMesh.Indices.Num() % 3 != 0)
	{
		OutError = TEXT("cloth: physical mesh index count is not divisible by 3");
		return false;
	}
	for (int32 Index = 0; Index < PhysicalMesh.Indices.Num(); Index += 3)
	{
		const int32 OldA = static_cast<int32>(PhysicalMesh.Indices[Index + 0]);
		const int32 OldB = static_cast<int32>(PhysicalMesh.Indices[Index + 1]);
		const int32 OldC = static_cast<int32>(PhysicalMesh.Indices[Index + 2]);
		if (!OldToNewVertex.IsValidIndex(OldA) || !OldToNewVertex.IsValidIndex(OldB) || !OldToNewVertex.IsValidIndex(OldC))
		{
			OutError = TEXT("cloth: physical mesh contains an out-of-range index buffer reference");
			return false;
		}
		const int32 A = OldToNewVertex[OldA];
		const int32 B = OldToNewVertex[OldB];
		const int32 C = OldToNewVertex[OldC];
		if (A == B || B == C || A == C)
		{
			++OutResult.RemovedDegenerateTriangleCount;
			continue;
		}
		NewIndices.Add(static_cast<uint32>(A));
		NewIndices.Add(static_cast<uint32>(B));
		NewIndices.Add(static_cast<uint32>(C));
	}
	if (NewIndices.Num() == 0)
	{
		OutError = TEXT("cloth: weld removed all physical mesh triangles");
		return false;
	}

	TSet<int32> NewSelfCollisionVertices;
	for (int32 OldIndex : PhysicalMesh.SelfCollisionVertexSet)
	{
		if (OldToNewVertex.IsValidIndex(OldIndex))
		{
			NewSelfCollisionVertices.Add(OldToNewVertex[OldIndex]);
		}
	}

	PhysicalMesh.Vertices = MoveTemp(NewVertices);
	PhysicalMesh.Normals = MoveTemp(NewNormals);
	PhysicalMesh.BoneData = MoveTemp(NewBoneData);
	PhysicalMesh.InverseMasses = MoveTemp(NewInverseMasses);
#if WITH_EDITORONLY_DATA
	PhysicalMesh.VertexColors = MoveTemp(NewVertexColors);
#endif
	PhysicalMesh.Indices = MoveTemp(NewIndices);
	PhysicalMesh.SelfCollisionVertexSet = MoveTemp(NewSelfCollisionVertices);
	RemapLegacyClothWeightMaps(PhysicalMesh.WeightMaps, NewVertexToOldVertices);
#if WITH_EDITORONLY_DATA
	RemapLegacyClothPointWeightMaps(LodData.PointWeightMaps, NewVertexToOldVertices);
#endif
	PhysicalMesh.CalculateInverseMasses();
	PhysicalMesh.CalculateNumInfluences();

	OutResult.FinalVertexCount = PhysicalMesh.Vertices.Num();
	OutResult.WeldedVertexCount = OutResult.OriginalVertexCount - OutResult.FinalVertexCount;
	return true;
}

bool BuildLegacyRenderMeshDescForSection(
	const FSkeletalMeshLODModel& LodModel,
	const FSkelMeshSection& Section,
	TArray<FVector3f>& OutPositions,
	TArray<FVector3f>& OutNormals,
	TArray<FVector3f>& OutTangents,
	TArray<uint32>& OutIndices,
	FString& OutError)
{
	OutPositions.Reset();
	OutNormals.Reset();
	OutTangents.Reset();
	OutIndices.Reset();
	OutPositions.Reserve(Section.SoftVertices.Num());
	OutNormals.Reserve(Section.SoftVertices.Num());
	OutTangents.Reserve(Section.SoftVertices.Num());
	for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
	{
		OutPositions.Add(Vertex.Position);
		OutNormals.Add(Vertex.TangentZ);
		OutTangents.Add(Vertex.TangentX);
	}
	for (uint32 SectionIndexOffset = 0; SectionIndexOffset < Section.NumTriangles * 3; ++SectionIndexOffset)
	{
		if (!LodModel.IndexBuffer.IsValidIndex(Section.BaseIndex + SectionIndexOffset))
		{
			OutError = TEXT("cloth: bound section has an out-of-range base index");
			return false;
		}
		const int32 SourceLocalVertexIndex =
			static_cast<int32>(LodModel.IndexBuffer[Section.BaseIndex + SectionIndexOffset]) - static_cast<int32>(Section.BaseVertexIndex);
		if (!OutPositions.IsValidIndex(SourceLocalVertexIndex))
		{
			OutError = TEXT("cloth: bound section has an out-of-range index buffer reference");
			return false;
		}
		OutIndices.Add(static_cast<uint32>(SourceLocalVertexIndex));
	}
	if (OutPositions.Num() == 0 || OutIndices.Num() == 0)
	{
		OutError = TEXT("cloth: bound section has no render geometry");
		return false;
	}
	return true;
}

bool GenerateLegacyClothRenderMappings(
	USkeletalMesh* Mesh,
	const UClothingAssetCommon* Asset,
	const FClothLODDataCommon& ClothLodData,
	int32 ClothLodIndex,
	int32 SectionIndexFilter,
	TArray<FLegacyClothSectionMapping>& OutMappings,
	FString& OutError)
{
	OutMappings.Reset();
	if (!Mesh || !Mesh->GetImportedModel())
	{
		OutError = TEXT("cloth: skeletal mesh has no imported model");
		return false;
	}
	if (!Asset || !Asset->LodData.IsValidIndex(ClothLodIndex))
	{
		OutError = TEXT("cloth: clothing asset has no editable cloth LOD");
		return false;
	}
	if (ClothLodData.PhysicalMeshData.Vertices.Num() == 0 || ClothLodData.PhysicalMeshData.Indices.Num() == 0)
	{
		OutError = TEXT("cloth: physical mesh has no source geometry for render mapping");
		return false;
	}
	if (ClothLodData.PhysicalMeshData.Indices.Num() % 3 != 0)
	{
		OutError = TEXT("cloth: physical mesh index count is not divisible by 3");
		return false;
	}
	const ClothingMeshUtils::ClothMeshDesc SourceMesh(
		ClothLodData.PhysicalMeshData.Vertices,
		ClothLodData.PhysicalMeshData.Indices);
	const FPointWeightMap* const MaxDistances =
		ClothLodData.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);

	TIndirectArray<FSkeletalMeshLODModel>& LODModels = Mesh->GetImportedModel()->LODModels;
	for (int32 MeshLodIndex = 0; MeshLodIndex < LODModels.Num(); ++MeshLodIndex)
	{
		FSkeletalMeshLODModel& LodModel = LODModels[MeshLodIndex];
		for (int32 SectionIndex = 0; SectionIndex < LodModel.Sections.Num(); ++SectionIndex)
		{
			if (SectionIndexFilter != INDEX_NONE && SectionIndex != SectionIndexFilter)
			{
				continue;
			}
			FSkelMeshSection& Section = LodModel.Sections[SectionIndex];
			if (Section.ClothingData.AssetGuid != Asset->GetAssetGuid()
				|| Section.ClothingData.AssetLodIndex != ClothLodIndex)
			{
				continue;
			}

			TArray<FVector3f> RenderPositions;
			TArray<FVector3f> RenderNormals;
			TArray<FVector3f> RenderTangents;
			TArray<uint32> RenderIndices;
			if (!BuildLegacyRenderMeshDescForSection(
				LodModel,
				Section,
				RenderPositions,
				RenderNormals,
				RenderTangents,
				RenderIndices,
				OutError))
			{
				return false;
			}

			const ClothingMeshUtils::ClothMeshDesc TargetMesh(RenderPositions, RenderNormals, RenderTangents, RenderIndices);
			TArray<FMeshToMeshVertData> MappingData;
			ClothingMeshUtils::GenerateMeshToMeshVertData(
				MappingData,
				TargetMesh,
				SourceMesh,
				MaxDistances,
				ClothLodData.bSmoothTransition,
				ClothLodData.bUseMultipleInfluences,
				ClothLodData.SkinningKernelRadius);
			if (MappingData.Num() != RenderPositions.Num())
			{
				OutError = FString::Printf(
					TEXT("cloth: generated render mapping for section_index %d has %d entries for %d render vertices"),
					SectionIndex,
					MappingData.Num(),
					RenderPositions.Num());
				return false;
			}
			FLegacyClothSectionMapping& Generated = OutMappings.AddDefaulted_GetRef();
			Generated.MeshLodIndex = MeshLodIndex;
			Generated.SectionIndex = SectionIndex;
			Generated.MappingData = MoveTemp(MappingData);
		}
	}

	if (SectionIndexFilter != INDEX_NONE && OutMappings.Num() == 0)
	{
		OutError = FString::Printf(TEXT("cloth: section_index %d is not bound to clothing asset LOD %d"), SectionIndexFilter, ClothLodIndex);
		return false;
	}
	if (OutMappings.Num() == 0)
	{
		OutError = FString::Printf(TEXT("cloth: no skeletal mesh sections are bound to clothing asset LOD %d"), ClothLodIndex);
		return false;
	}
	return true;
}

int32 CountLegacyClothBoundSections(USkeletalMesh* Mesh, const UClothingAssetCommon* Asset, int32 ClothLodIndex)
{
	if (!Mesh || !Mesh->GetImportedModel() || !Asset)
	{
		return 0;
	}
	int32 BoundSectionCount = 0;
	const TIndirectArray<FSkeletalMeshLODModel>& LODModels = Mesh->GetImportedModel()->LODModels;
	for (const FSkeletalMeshLODModel& LodModel : LODModels)
	{
		for (const FSkelMeshSection& Section : LodModel.Sections)
		{
			if (Section.ClothingData.AssetGuid == Asset->GetAssetGuid()
				&& Section.ClothingData.AssetLodIndex == ClothLodIndex)
			{
				++BoundSectionCount;
			}
		}
	}
	return BoundSectionCount;
}

void ApplyLegacyClothRenderMappings(
	USkeletalMesh* Mesh,
	UClothingAssetCommon* Asset,
	const TArray<FLegacyClothSectionMapping>& Mappings,
	bool bUpdateLodBiasMappings)
{
	TIndirectArray<FSkeletalMeshLODModel>& LODModels = Mesh->GetImportedModel()->LODModels;
	for (const FLegacyClothSectionMapping& Mapping : Mappings)
	{
		if (!LODModels.IsValidIndex(Mapping.MeshLodIndex)
			|| !LODModels[Mapping.MeshLodIndex].Sections.IsValidIndex(Mapping.SectionIndex))
		{
			continue;
		}
		FSkelMeshSection& Section = LODModels[Mapping.MeshLodIndex].Sections[Mapping.SectionIndex];
		Section.ClothMappingDataLODs.SetNum(1);
		Section.ClothMappingDataLODs[0] = Mapping.MappingData;
	}
	if (bUpdateLodBiasMappings)
	{
		Asset->UpdateAllLODBiasMappings(Mesh);
	}
}

FBridgeToolResult LoadMeshAndAsset(
	const TSharedPtr<FJsonObject>& Arguments,
	USkeletalMesh*& OutMesh,
	UClothingAssetBase*& OutAsset,
	FString& OutSkeletalMeshPath,
	FString& OutAssetName)
{
	OutSkeletalMeshPath.Reset();
	OutAssetName.Reset();
	if (Arguments.IsValid())
	{
		Arguments->TryGetStringField(TEXT("skeletal_mesh"), OutSkeletalMeshPath);
		Arguments->TryGetStringField(TEXT("asset_name"), OutAssetName);
	}
	if (OutSkeletalMeshPath.IsEmpty() || OutAssetName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("skeletal_mesh and asset_name are required"));
	}

	FString LoadError;
	OutMesh = LoadSkeletalMesh(OutSkeletalMeshPath, LoadError);
	if (!OutMesh)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	OutAsset = FindClothingAsset(OutMesh, OutAssetName);
	if (!OutAsset)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth asset not found on skeletal mesh: %s"), *OutAssetName));
	}
	return FBridgeToolResult();
}
}

FString UClothQueryTool::GetToolDescription() const
{
	return TEXT("Inspect Chaos Cloth assets and skeletal mesh section bindings on a SkeletalMesh asset.");
}

TMap<FString, FBridgeSchemaProperty> UClothQueryTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Optional clothing asset name filter")));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Optional LOD index for client-side filtering")));
	return Schema;
}

TArray<FString> UClothQueryTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh") };
}

FBridgeToolResult UClothQueryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	const FString SkeletalMeshPath = GetStringArgOrDefault(Arguments, TEXT("skeletal_mesh"));
	const FString AssetName = GetStringArgOrDefault(Arguments, TEXT("asset_name"));
	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), INDEX_NONE);
	if (SkeletalMeshPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("skeletal_mesh is required"));
	}

	FString LoadError;
	USkeletalMesh* Mesh = LoadSkeletalMesh(SkeletalMeshPath, LoadError);
	if (!Mesh)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	const bool bLodExplicitlyRequested = Arguments.IsValid() && Arguments->HasField(TEXT("lod_index"));
	if (bLodExplicitlyRequested && Mesh->GetImportedModel()
		&& !Mesh->GetImportedModel()->LODModels.IsValidIndex(LodIndex))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth: lod_index %d is out of range"), LodIndex));
	}
	return FBridgeToolResult::Json(BuildQueryResult(Mesh, SkeletalMeshPath, AssetName, LodIndex));
}

FString UClothChaosQueryTool::GetToolDescription() const
{
	return TEXT("Report Dataflow-based Chaos Cloth Asset LOD, mesh, seam, and weight-map state.");
}

TMap<FString, FBridgeSchemaProperty> UClothChaosQueryTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("cloth_asset"), ClothSchemaProperty(TEXT("string"), TEXT("Chaos Cloth Asset path"), true));
	Schema.Add(TEXT("include_nodes"), ClothSchemaProperty(TEXT("boolean"), TEXT("Include Dataflow graph/node metadata when available")));
	Schema.Add(TEXT("gap_tolerance"), ClothSchemaProperty(TEXT("number"), TEXT("Report unwelded near-vertex gap candidates within this distance")));
	Schema.Add(TEXT("gap_limit"), ClothSchemaProperty(TEXT("integer"), TEXT("Maximum gap candidates to return")));
	Schema.Add(TEXT("dump_weights"), ClothSchemaProperty(TEXT("boolean"), TEXT("Include per-simulation-vertex weight map values")));
	Schema.Add(TEXT("weight_map"), ClothSchemaProperty(TEXT("string"), TEXT("Weight map name for per-vertex dumps")));
	Schema.Add(TEXT("weight_limit"), ClothSchemaProperty(TEXT("integer"), TEXT("Maximum per-vertex weight entries to return")));
	return Schema;
}

TArray<FString> UClothChaosQueryTool::GetRequiredParams() const
{
	return { TEXT("cloth_asset") };
}

FBridgeToolResult UClothChaosQueryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	const FString ClothAssetPath = GetStringArgOrDefault(Arguments, TEXT("cloth_asset"));
	const bool bIncludeNodes = GetBoolArgOrDefault(Arguments, TEXT("include_nodes"), false);
	const bool bDumpWeights = GetBoolArgOrDefault(Arguments, TEXT("dump_weights"), false);
	const FName WeightMapName(*GetStringArgOrDefault(Arguments, TEXT("weight_map"), TEXT("MaxDistance")));
	const int32 WeightLimit = GetIntArgOrDefault(Arguments, TEXT("weight_limit"), 0);
	const int32 GapLimit = GetIntArgOrDefault(Arguments, TEXT("gap_limit"), 0);
	double GapToleranceNumber = -1.0;
	if (Arguments.IsValid())
	{
		Arguments->TryGetNumberField(TEXT("gap_tolerance"), GapToleranceNumber);
	}
	if (ClothAssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("cloth_asset is required"));
	}
	if (GapToleranceNumber < 0.0 && !FMath::IsNearlyEqual(GapToleranceNumber, -1.0))
	{
		return FBridgeToolResult::Error(TEXT("gap_tolerance must be non-negative"));
	}

	FString LoadError;
	UChaosClothAsset* Asset = FBridgeAssetModifier::LoadAssetByPath<UChaosClothAsset>(ClothAssetPath, LoadError);
	if (!Asset)
	{
		return FBridgeToolResult::Error(LoadError.IsEmpty() ? FString::Printf(TEXT("cloth chaos-query: asset is not a UChaosClothAsset: %s"), *ClothAssetPath) : LoadError);
	}

	return FBridgeToolResult::Json(ChaosClothAssetToJson(Asset, ClothAssetPath, bIncludeNodes, bDumpWeights, WeightMapName, WeightLimit, static_cast<float>(GapToleranceNumber), GapLimit));
}

FString UClothConvertTool::GetToolDescription() const
{
	return TEXT("Convert a legacy in-mesh clothing asset on a SkeletalMesh into a Dataflow-based Chaos Cloth Asset.");
}

TMap<FString, FBridgeSchemaProperty> UClothConvertTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Legacy clothing asset object name"), true));
	Schema.Add(TEXT("output_asset"), ClothSchemaProperty(TEXT("string"), TEXT("Output Chaos Cloth Asset path"), true));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the created Chaos Cloth Asset package")));
	return Schema;
}

TArray<FString> UClothConvertTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh"), TEXT("asset_name"), TEXT("output_asset") };
}

FBridgeToolResult UClothConvertTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	const FString SkeletalMeshPath = GetStringArgOrDefault(Arguments, TEXT("skeletal_mesh"));
	const FString AssetName = GetStringArgOrDefault(Arguments, TEXT("asset_name"));
	const FString OutputAssetPath = GetStringArgOrDefault(Arguments, TEXT("output_asset"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	if (SkeletalMeshPath.IsEmpty() || AssetName.IsEmpty() || OutputAssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("skeletal_mesh, asset_name, and output_asset are required"));
	}

	FString LoadError;
	USkeletalMesh* Mesh = LoadSkeletalMesh(SkeletalMeshPath, LoadError);
	if (!Mesh)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	UClothingAssetBase* SourceAsset = FindClothingAsset(Mesh, AssetName);
	if (!SourceAsset)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth asset not found on skeletal mesh: %s"), *AssetName));
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(OutputAssetPath);
	FText PackageNameReason;
	if (!FPackageName::IsValidLongPackageName(PackageName, false, &PackageNameReason))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("invalid output_asset package path: %s"), *PackageNameReason.ToString()));
	}
	if (OutputChaosClothAssetExists(OutputAssetPath, PackageName))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("output_asset already exists: %s"), *OutputAssetPath));
	}

	UClothingAssetExporter* Exporter = FindChaosClothAssetExporter();
	if (!Exporter)
	{
		return FBridgeToolResult::Error(TEXT("cloth-convert: Chaos Cloth Asset exporter is unavailable; enable the ChaosClothAsset plugin"));
	}

	const FString OutputAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("failed to create package: %s"), *PackageName));
	}

	UChaosClothAsset* NewAsset = NewObject<UChaosClothAsset>(Package, UChaosClothAsset::StaticClass(), FName(*OutputAssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!NewAsset)
	{
		return FBridgeToolResult::Error(TEXT("failed to create Chaos Cloth Asset"));
	}

	NewAsset->MarkPackageDirty();
	Exporter->Export(SourceAsset, NewAsset);

	FString ValidationError;
	if (!ValidateConvertedChaosClothAsset(NewAsset, ValidationError))
	{
		NewAsset->ClearFlags(RF_Public | RF_Standalone);
		NewAsset->MarkAsGarbage();
		Package->ClearDirtyFlag();
		return FBridgeToolResult::Error(ValidationError);
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	NewAsset->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = ChaosClothAssetToJson(NewAsset, OutputAssetPath, false);
	Result->SetBoolField(TEXT("converted"), true);
	const bool bNewAssetHasDataflow = ClothAssetHasDataflow(NewAsset);
	Result->SetBoolField(TEXT("dataflow_based"), bNewAssetHasDataflow);
	Result->SetStringField(TEXT("conversion_mode"), bNewAssetHasDataflow ? TEXT("dataflow") : TEXT("legacy_collection"));
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("source_asset_name"), SourceAsset->GetName());
	Result->SetStringField(TEXT("output_asset"), OutputAssetPath);
	Result->SetObjectField(TEXT("preserved"), BuildLegacyClothPreservationSummary(SourceAsset));

	if (bSave)
	{
		FString SaveError;
		if (!FBridgeAssetModifier::SaveAsset(NewAsset, false, SaveError))
		{
			Result->SetBoolField(TEXT("saved"), false);
			Result->SetStringField(TEXT("save_error"), SaveError);
			return FBridgeToolResult::Error(SaveError);
		}
		Result->SetBoolField(TEXT("saved"), true);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetBoolField(TEXT("needs_save"), true);
	}

	return FBridgeToolResult::Json(Result);
}

FString UClothChaosStitchTool::GetToolDescription() const
{
	return TEXT("Add a seam/stitch chain to a Chaos Cloth Asset simulation mesh.");
}

TMap<FString, FBridgeSchemaProperty> UClothChaosStitchTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("cloth_asset"), ClothSchemaProperty(TEXT("string"), TEXT("Chaos Cloth Asset path"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Chaos Cloth Asset LOD index")));
	Schema.Add(TEXT("mode"), ClothSchemaProperty(TEXT("string"), TEXT("Stitch pairing mode"), false, { TEXT("pairs"), TEXT("proximity") }));
	Schema.Add(TEXT("index_space"), ClothSchemaProperty(TEXT("string"), TEXT("Input vertex index space"), false, { TEXT("2d"), TEXT("3d") }));
	Schema.Add(TEXT("vertex_pairs"), ClothSchemaProperty(TEXT("array"), TEXT("Array of [a, b] vertex pairs for mode=pairs")));
	Schema.Add(TEXT("first_vertices"), ClothSchemaProperty(TEXT("array"), TEXT("First boundary vertex indices for mode=proximity")));
	Schema.Add(TEXT("second_vertices"), ClothSchemaProperty(TEXT("array"), TEXT("Second boundary vertex indices for mode=proximity")));
	Schema.Add(TEXT("tolerance"), ClothSchemaProperty(TEXT("number"), TEXT("Maximum pairing distance for mode=proximity")));
	Schema.Add(TEXT("dry_run"), ClothSchemaProperty(TEXT("boolean"), TEXT("Report candidate stitch pairs without mutation")));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the Chaos Cloth Asset after mutation")));
	return Schema;
}

TArray<FString> UClothChaosStitchTool::GetRequiredParams() const
{
	return { TEXT("cloth_asset") };
}

FBridgeToolResult UClothChaosStitchTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	const FString ClothAssetPath = GetStringArgOrDefault(Arguments, TEXT("cloth_asset"));
	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	if (ClothAssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("cloth_asset is required"));
	}

	FString LoadError;
	UChaosClothAsset* Asset = FBridgeAssetModifier::LoadAssetByPath<UChaosClothAsset>(ClothAssetPath, LoadError);
	if (!Asset)
	{
		return FBridgeToolResult::Error(LoadError.IsEmpty() ? FString::Printf(TEXT("cloth-chaos-stitch: asset is not a UChaosClothAsset: %s"), *ClothAssetPath) : LoadError);
	}

	TArray<TSharedRef<const FManagedArrayCollection>> Collections;
	TSharedPtr<FManagedArrayCollection> MutableCollection;
	FString Error;
	if (!CloneChaosClothCollectionsForLod(Asset, LodIndex, Collections, MutableCollection, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	using namespace UE::Chaos::ClothAsset;
	FCollectionClothFacade Cloth(MutableCollection.ToSharedRef());
	if (!Cloth.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth-chaos-stitch: LOD %d does not contain a valid cloth collection"), LodIndex));
	}

	TArray<FIntVector2> StitchPairs;
	if (!BuildChaosStitchPairs(Arguments, Cloth, StitchPairs, Error))
	{
		return FBridgeToolResult::Error(Error);
	}
	if (bDryRun)
	{
		const FString IndexSpace = GetStringArgOrDefault(Arguments, TEXT("index_space"), TEXT("2d"));
		TArray<TSharedPtr<FJsonValue>> CandidatePairs;
		for (const FIntVector2& Pair : StitchPairs)
		{
			CandidatePairs.Add(MakeShared<FJsonValueObject>(BuildChaosStitchPairJson(Cloth, Pair, IndexSpace)));
		}
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetStringField(TEXT("cloth_asset"), ClothAssetPath);
		Result->SetNumberField(TEXT("lod_index"), LodIndex);
		Result->SetStringField(TEXT("index_space"), IndexSpace);
		Result->SetStringField(TEXT("mode"), GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("pairs")));
		Result->SetArrayField(TEXT("candidate_pairs"), CandidatePairs);
		Result->SetNumberField(TEXT("candidate_pair_count"), CandidatePairs.Num());
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetBoolField(TEXT("mutated"), false);
		return FBridgeToolResult::Json(Result);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothChaosStitch", "Add Chaos cloth stitch to {0}"),
			FText::FromString(ClothAssetPath)));
	FBridgeAssetModifier::MarkModified(Asset);

	const int32 BeforeSeamCount = Cloth.GetNumSeams();
	const int32 BeforeVertex3DCount = Cloth.GetNumSimVertices3D();
	FCollectionClothSeamFacade Seam = Cloth.AddGetSeam();
	Seam.Initialize(TConstArrayView<FIntVector2>(StitchPairs));
	FClothGeometryTools::CleanupAndCompactMesh(MutableCollection.ToSharedRef());
	const int32 AfterVertex3DCount = Cloth.GetNumSimVertices3D();
	const int32 WeldedVertexCount = FMath::Max(0, BeforeVertex3DCount - AfterVertex3DCount);

	if (!RebuildChaosClothAsset(Asset, Collections, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("cloth_asset"), ClothAssetPath);
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetNumberField(TEXT("seam_index"), BeforeSeamCount);
	Result->SetNumberField(TEXT("stitches_created"), StitchPairs.Num());
	Result->SetNumberField(TEXT("sim_vertex_3d_count_before"), BeforeVertex3DCount);
	Result->SetNumberField(TEXT("sim_vertex_3d_count_after"), AfterVertex3DCount);
	Result->SetNumberField(TEXT("welded_sim_vertex_count"), WeldedVertexCount);
	Result->SetStringField(TEXT("index_space"), GetStringArgOrDefault(Arguments, TEXT("index_space"), TEXT("2d")));
	Result->SetStringField(TEXT("mode"), GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("pairs")));

	FString SaveError;
	if (!SaveChaosClothAssetIfRequested(Asset, bSave, Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	return FBridgeToolResult::Json(Result);
}

FString UClothChaosSetConfigTool::GetToolDescription() const
{
	return TEXT("Set Chaos Cloth Asset simulation config properties stored on the cloth collection.");
}

TMap<FString, FBridgeSchemaProperty> UClothChaosSetConfigTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("cloth_asset"), ClothSchemaProperty(TEXT("string"), TEXT("Chaos Cloth Asset path"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Chaos Cloth Asset LOD index")));
	Schema.Add(TEXT("properties"), ClothSchemaProperty(TEXT("object"), TEXT("Simulation property name to JSON value map"), true));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the Chaos Cloth Asset after mutation")));
	return Schema;
}

TArray<FString> UClothChaosSetConfigTool::GetRequiredParams() const
{
	return { TEXT("cloth_asset"), TEXT("properties") };
}

FBridgeToolResult UClothChaosSetConfigTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	const FString ClothAssetPath = GetStringArgOrDefault(Arguments, TEXT("cloth_asset"));
	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	if (ClothAssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("cloth_asset is required"));
	}

	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetObjectField(TEXT("properties"), PropertiesPtr) || !PropertiesPtr || !PropertiesPtr->IsValid())
	{
		return FBridgeToolResult::Error(TEXT("properties object is required"));
	}
	const TSharedPtr<FJsonObject>& Properties = *PropertiesPtr;

	FString LoadError;
	UChaosClothAsset* Asset = FBridgeAssetModifier::LoadAssetByPath<UChaosClothAsset>(ClothAssetPath, LoadError);
	if (!Asset)
	{
		return FBridgeToolResult::Error(LoadError.IsEmpty() ? FString::Printf(TEXT("cloth-chaos-set-config: asset is not a UChaosClothAsset: %s"), *ClothAssetPath) : LoadError);
	}

	TArray<TSharedRef<const FManagedArrayCollection>> Collections;
	TSharedPtr<FManagedArrayCollection> MutableCollection;
	FString Error;
	if (!CloneChaosClothCollectionsForLod(Asset, LodIndex, Collections, MutableCollection, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	Chaos::Softs::FCollectionPropertyFacade PropertyFacade(MutableCollection);
	if (!PropertyFacade.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth-chaos-set-config: LOD %d has no simulation property collection"), LodIndex));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothChaosSetConfig", "Set Chaos cloth config on {0}"),
			FText::FromString(ClothAssetPath)));
	FBridgeAssetModifier::MarkModified(Asset);

	TArray<TSharedPtr<FJsonValue>> Changed;
	TArray<FString> PropertyNames;
	SoftUE::JsonObjectUtils::GetFieldNames(Properties, PropertyNames);
	for (const FString& PropertyName : PropertyNames)
	{
		const TSharedPtr<FJsonValue> Value = SoftUE::JsonObjectUtils::FindField(Properties, PropertyName);
		if (!ApplyChaosConfigPropertyValue(PropertyFacade, PropertyName, Value, Error))
		{
			return FBridgeToolResult::Error(Error);
		}
		Changed.Add(ChaosConfigChangedValueToJson(PropertyName));
	}

	if (Changed.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("properties object must contain at least one property"));
	}
	if (!RebuildChaosClothAsset(Asset, Collections, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("cloth_asset"), ClothAssetPath);
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetArrayField(TEXT("changed_properties"), Changed);
	Result->SetNumberField(TEXT("changed_property_count"), Changed.Num());

	FString SaveError;
	if (!SaveChaosClothAssetIfRequested(Asset, bSave, Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	return FBridgeToolResult::Json(Result);
}

FString UClothChaosSetWeightMapTool::GetToolDescription() const
{
	return TEXT("Set Chaos Cloth Asset weight map values by simulation vertex index or spatial selection.");
}

TMap<FString, FBridgeSchemaProperty> UClothChaosSetWeightMapTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("cloth_asset"), ClothSchemaProperty(TEXT("string"), TEXT("Chaos Cloth Asset path"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Chaos Cloth Asset LOD index")));
	Schema.Add(TEXT("weight_map"), ClothSchemaProperty(TEXT("string"), TEXT("Weight map name")));
	Schema.Add(TEXT("vertices"), ClothSchemaProperty(TEXT("array"), TEXT("Simulation 3D vertex indices to edit")));
	Schema.Add(TEXT("z_min"), ClothSchemaProperty(TEXT("number"), TEXT("Select vertices with local Z greater than or equal to this value")));
	Schema.Add(TEXT("z_max"), ClothSchemaProperty(TEXT("number"), TEXT("Select vertices with local Z less than or equal to this value")));
	Schema.Add(TEXT("center"), ClothSchemaProperty(TEXT("array"), TEXT("Sphere center [X, Y, Z] for spatial selection")));
	Schema.Add(TEXT("radius"), ClothSchemaProperty(TEXT("number"), TEXT("Sphere radius for spatial selection")));
	Schema.Add(TEXT("value"), ClothSchemaProperty(TEXT("number"), TEXT("Weight map value to assign"), true));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the Chaos Cloth Asset after mutation")));
	return Schema;
}

TArray<FString> UClothChaosSetWeightMapTool::GetRequiredParams() const
{
	return { TEXT("cloth_asset"), TEXT("value") };
}

FBridgeToolResult UClothChaosSetWeightMapTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	const FString ClothAssetPath = GetStringArgOrDefault(Arguments, TEXT("cloth_asset"));
	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	const FName WeightMapName(*GetStringArgOrDefault(Arguments, TEXT("weight_map"), TEXT("MaxDistance")));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	float Value = 0.0f;
	if (ClothAssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("cloth_asset is required"));
	}
	if (!GetFloatArg(Arguments, TEXT("value"), Value))
	{
		return FBridgeToolResult::Error(TEXT("value is required"));
	}

	FString LoadError;
	UChaosClothAsset* Asset = FBridgeAssetModifier::LoadAssetByPath<UChaosClothAsset>(ClothAssetPath, LoadError);
	if (!Asset)
	{
		return FBridgeToolResult::Error(LoadError.IsEmpty() ? FString::Printf(TEXT("cloth-chaos-set-weightmap: asset is not a UChaosClothAsset: %s"), *ClothAssetPath) : LoadError);
	}

	TArray<TSharedRef<const FManagedArrayCollection>> Collections;
	TSharedPtr<FManagedArrayCollection> MutableCollection;
	FString Error;
	if (!CloneChaosClothCollectionsForLod(Asset, LodIndex, Collections, MutableCollection, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	using namespace UE::Chaos::ClothAsset;
	FCollectionClothFacade Cloth(MutableCollection.ToSharedRef());
	if (!Cloth.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth-chaos-set-weightmap: LOD %d does not contain a valid cloth collection"), LodIndex));
	}

	TArray<int32> SelectedVertices;
	if (!SelectChaosWeightMapVertices(Arguments, Cloth, SelectedVertices, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothChaosSetWeightMap", "Set Chaos cloth weight map on {0}"),
			FText::FromString(ClothAssetPath)));
	FBridgeAssetModifier::MarkModified(Asset);

	TArray<float> BeforeValues;
	TArray<float> AfterValues;
	if (!SetWeightMap(Cloth, WeightMapName, SelectedVertices, Value, BeforeValues, AfterValues, Error))
	{
		return FBridgeToolResult::Error(Error);
	}
	if (!RebuildChaosClothAsset(Asset, Collections, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TArray<TSharedPtr<FJsonValue>> SelectedVertexValues;
	AppendIntArrayJson(SelectedVertexValues, SelectedVertices);
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("cloth_asset"), ClothAssetPath);
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetStringField(TEXT("weight_map"), WeightMapName.ToString());
	Result->SetNumberField(TEXT("value"), Value);
	Result->SetArrayField(TEXT("selected_vertices"), SelectedVertexValues);
	Result->SetNumberField(TEXT("changed_vertex_count"), SelectedVertices.Num());
	Result->SetObjectField(TEXT("before"), FloatArrayStatsToJson(BeforeValues));
	Result->SetObjectField(TEXT("after"), FloatArrayStatsToJson(AfterValues));

	FString SaveError;
	if (!SaveChaosClothAssetIfRequested(Asset, bSave, Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	return FBridgeToolResult::Json(Result);
}

FString UClothCreateTool::GetToolDescription() const
{
	return TEXT("Create a Chaos Cloth asset from one or more SkeletalMesh LOD sections, optionally bind it immediately, and save the mesh.");
}

TMap<FString, FBridgeSchemaProperty> UClothCreateTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("New clothing asset name"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Source skeletal mesh LOD index")));
	Schema.Add(TEXT("section_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Source skeletal mesh section index")));
	Schema.Add(TEXT("section_indices"), ClothSchemaProperty(TEXT("array"), TEXT("Source skeletal mesh section indices to merge into one cloth asset")));
	Schema.Add(TEXT("weld_tolerance"), ClothSchemaProperty(TEXT("number"), TEXT("Position tolerance in Unreal centimeters for welding coincident sim vertices across merged sections")));
	Schema.Add(TEXT("physics_asset"), ClothSchemaProperty(TEXT("string"), TEXT("Optional PhysicsAsset path for collision extraction")));
	Schema.Add(TEXT("remove_from_mesh"), ClothSchemaProperty(TEXT("boolean"), TEXT("Remove the render section after creating the cloth data")));
	Schema.Add(TEXT("bind"), ClothSchemaProperty(TEXT("boolean"), TEXT("Bind the new cloth asset to the source section")));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the SkeletalMesh after mutation")));
	return Schema;
}

TArray<FString> UClothCreateTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh"), TEXT("asset_name") };
}

FBridgeToolResult UClothCreateTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	const FString SkeletalMeshPath = GetStringArgOrDefault(Arguments, TEXT("skeletal_mesh"));
	const FString AssetName = GetStringArgOrDefault(Arguments, TEXT("asset_name"));
	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	const FString PhysicsAssetPath = GetStringArgOrDefault(Arguments, TEXT("physics_asset"));
	const float WeldTolerance = GetFloatArgOrDefault(Arguments, TEXT("weld_tolerance"), DefaultClothSectionWeldTolerance);
	const bool bRemoveFromMesh = GetBoolArgOrDefault(Arguments, TEXT("remove_from_mesh"), false);
	const bool bBind = GetBoolArgOrDefault(Arguments, TEXT("bind"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);

	TArray<int32> SectionIndices;
	FString SectionParseError;
	if (!ParseSectionIndicesFromArgs(Arguments, SectionIndices, SectionParseError))
	{
		return FBridgeToolResult::Error(SectionParseError);
	}
	const int32 SectionIndex = SectionIndices[0];

	if (SkeletalMeshPath.IsEmpty() || AssetName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("skeletal_mesh and asset_name are required"));
	}
	if (bRemoveFromMesh && bBind)
	{
		return FBridgeToolResult::Error(TEXT("cloth: remove_from_mesh cannot be combined with bind; create the cloth asset first, then bind it in a separate step"));
	}
	if (bRemoveFromMesh && SectionIndices.Num() > 1)
	{
		return FBridgeToolResult::Error(TEXT("cloth: remove_from_mesh cannot be combined with multiple section indices"));
	}
	if (WeldTolerance < 0.0f)
	{
		return FBridgeToolResult::Error(TEXT("cloth: weld_tolerance must be non-negative"));
	}

	FString LoadError;
	USkeletalMesh* Mesh = LoadSkeletalMesh(SkeletalMeshPath, LoadError);
	if (!Mesh)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (FindClothingAsset(Mesh, AssetName))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth asset already exists on skeletal mesh: %s"), *AssetName));
	}

	FString ValidationError;
	if (!ValidateMeshSections(Mesh, LodIndex, SectionIndices, ValidationError))
	{
		return FBridgeToolResult::Error(ValidationError);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothCreate", "Create cloth {0} on {1}"),
			FText::FromString(AssetName),
			FText::FromString(SkeletalMeshPath)));
	FBridgeAssetModifier::MarkModified(Mesh);

	FClothingSystemEditorInterfaceModule& ClothingEditorModule =
		FModuleManager::LoadModuleChecked<FClothingSystemEditorInterfaceModule>(TEXT("ClothingSystemEditorInterface"));
	UClothingAssetFactoryBase* Factory = ClothingEditorModule.GetClothingAssetFactory();
	if (!Factory)
	{
		return FBridgeToolResult::Error(TEXT("cloth: no ClothingAssetFactory is available"));
	}

	FSkeletalMeshClothBuildParams Params;
	Params.AssetName = AssetName;
	Params.LodIndex = LodIndex;
	Params.SourceSection = SectionIndex;
	Params.bRemoveFromMesh = bRemoveFromMesh;
	if (!PhysicsAssetPath.IsEmpty())
	{
		Params.PhysicsAsset = TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(PhysicsAssetPath));
	}

	UClothingAssetBase* NewAsset = Factory->CreateFromSkeletalMesh(Mesh, Params);
	if (!NewAsset)
	{
		return FBridgeToolResult::Error(TEXT("cloth: CreateFromSkeletalMesh failed"));
	}

	if (SectionIndices.Num() > 1)
	{
		UClothingAssetCommon* NewCommonAsset = Cast<UClothingAssetCommon>(NewAsset);
		FString MergeError;
		if (!BuildMergedClothLodFromSections(Mesh, NewCommonAsset, LodIndex, SectionIndices, 0, WeldTolerance, MergeError))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("cloth: CreateFromSkeletalMesh succeeded but multi-section merge failed: %s"), *MergeError));
		}
	}

	Mesh->AddClothingAsset(NewAsset);
	FBridgeAssetModifier::MarkModified(NewAsset);

	bool bBound = false;
	if (bBind)
	{
		FString BindError;
		bBound = SectionIndices.Num() > 1
			? BindClothAssetToSections(Mesh, NewAsset, LodIndex, SectionIndices, 0, BindError)
			: BindClothAssetToSection(Mesh, NewAsset, LodIndex, SectionIndex, 0, BindError);
		if (!bBound)
		{
			return FBridgeToolResult::Error(
				BindError.Equals(TEXT("cloth: BindToSkeletalMesh failed"))
					? TEXT("cloth: created asset but BindToSkeletalMesh failed")
					: FString::Printf(TEXT("cloth: created asset but %s"), *BindError));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("asset_name"), NewAsset->GetName());
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetNumberField(TEXT("section_index"), SectionIndex);
	if (SectionIndices.Num() > 1)
	{
		TArray<TSharedPtr<FJsonValue>> SectionIndexValues;
		for (int32 MergedSectionIndex : SectionIndices)
		{
			SectionIndexValues.Add(MakeShared<FJsonValueNumber>(MergedSectionIndex));
		}
		Result->SetArrayField(TEXT("section_indices"), SectionIndexValues);
		Result->SetNumberField(TEXT("weld_tolerance"), WeldTolerance);
	}
	Result->SetBoolField(TEXT("bound"), bBound);
	FString SaveError;
	if (!SaveMeshIfRequested(Mesh, bSave, Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	Result->SetObjectField(TEXT("asset"), ClothAssetToJson(NewAsset));
	return FBridgeToolResult::Json(Result);
}

FString UClothBindTool::GetToolDescription() const
{
	return TEXT("Bind an existing clothing asset LOD to a SkeletalMesh LOD section.");
}

TMap<FString, FBridgeSchemaProperty> UClothBindTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Existing clothing asset name"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Target skeletal mesh LOD index")));
	Schema.Add(TEXT("section_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Target skeletal mesh section index"), true));
	Schema.Add(TEXT("cloth_lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Source clothing asset LOD index")));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the SkeletalMesh after mutation")));
	return Schema;
}

TArray<FString> UClothBindTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh"), TEXT("asset_name"), TEXT("section_index") };
}

FBridgeToolResult UClothBindTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	USkeletalMesh* Mesh = nullptr;
	UClothingAssetBase* Asset = nullptr;
	FString SkeletalMeshPath;
	FString AssetName;
	FBridgeToolResult LoadResult = LoadMeshAndAsset(Arguments, Mesh, Asset, SkeletalMeshPath, AssetName);
	if (LoadResult.bIsError)
	{
		return LoadResult;
	}

	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	const int32 SectionIndex = GetIntArgOrDefault(Arguments, TEXT("section_index"), INDEX_NONE);
	const int32 ClothLodIndex = GetIntArgOrDefault(Arguments, TEXT("cloth_lod_index"), 0);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);

	FString ValidationError;
	if (SectionIndex == INDEX_NONE || !ValidateMeshSection(Mesh, LodIndex, SectionIndex, ValidationError))
	{
		return FBridgeToolResult::Error(SectionIndex == INDEX_NONE ? TEXT("section_index is required") : ValidationError);
	}
	if (!Asset->IsValidLod(ClothLodIndex))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("cloth_lod_index %d is out of range"), ClothLodIndex));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothBind", "Bind cloth {0} on {1}"),
			FText::FromString(AssetName),
			FText::FromString(SkeletalMeshPath)));
	FBridgeAssetModifier::MarkModified(Mesh);
	FBridgeAssetModifier::MarkModified(Asset);

	FString BindError;
	const bool bBound = BindClothAssetToSection(Mesh, Asset, LodIndex, SectionIndex, ClothLodIndex, BindError);
	if (!bBound)
	{
		return FBridgeToolResult::Error(BindError);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetNumberField(TEXT("section_index"), SectionIndex);
	Result->SetNumberField(TEXT("cloth_lod_index"), ClothLodIndex);
	FString SaveError;
	if (!SaveMeshIfRequested(Mesh, bSave, Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	Result->SetArrayField(TEXT("bindings"), BuildBindingArray(BridgeClothBindings::Collect(Mesh).Bindings));
	return FBridgeToolResult::Json(Result);
}

FString UClothSetConfigTool::GetToolDescription() const
{
	return TEXT("Patch properties on a clothing config object by JSON property path.");
}

TMap<FString, FBridgeSchemaProperty> UClothSetConfigTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Existing clothing asset name"), true));
	Schema.Add(TEXT("config_class"), ClothSchemaProperty(TEXT("string"), TEXT("Optional config key, class name, or class path")));
	Schema.Add(TEXT("properties"), ClothSchemaProperty(TEXT("object"), TEXT("Property path to JSON value map"), true));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the SkeletalMesh after mutation")));
	return Schema;
}

TArray<FString> UClothSetConfigTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh"), TEXT("asset_name"), TEXT("properties") };
}

FBridgeToolResult UClothSetConfigTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	USkeletalMesh* Mesh = nullptr;
	UClothingAssetBase* AssetBase = nullptr;
	FString SkeletalMeshPath;
	FString AssetName;
	FBridgeToolResult LoadResult = LoadMeshAndAsset(Arguments, Mesh, AssetBase, SkeletalMeshPath, AssetName);
	if (LoadResult.bIsError)
	{
		return LoadResult;
	}

	UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(AssetBase);
	if (!Asset)
	{
		return FBridgeToolResult::Error(TEXT("cloth-set-config requires a UClothingAssetCommon asset"));
	}

	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetObjectField(TEXT("properties"), PropertiesPtr) || !PropertiesPtr || !PropertiesPtr->IsValid())
	{
		return FBridgeToolResult::Error(TEXT("properties object is required"));
	}
	const TSharedPtr<FJsonObject>& Properties = *PropertiesPtr;

	UClothConfigBase* Config = ResolveClothConfig(Asset, GetStringArgOrDefault(Arguments, TEXT("config_class")));
	if (!Config)
	{
		return FBridgeToolResult::Error(TEXT("cloth: matching cloth config not found"));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothSetConfig", "Set cloth config {0} on {1}"),
			FText::FromString(AssetName),
			FText::FromString(SkeletalMeshPath)));
	FBridgeAssetModifier::MarkModified(Mesh);
	FBridgeAssetModifier::MarkModified(Asset);
	FBridgeAssetModifier::MarkModified(Config);

	TArray<TSharedPtr<FJsonValue>> Changed;
	TArray<FString> PropertyNames;
	SoftUE::JsonObjectUtils::GetFieldNames(Properties, PropertyNames);
	for (const FString& PropertyName : PropertyNames)
	{
		const TSharedPtr<FJsonValue> Value = SoftUE::JsonObjectUtils::FindField(Properties, PropertyName);
		FProperty* Property = nullptr;
		void* Container = nullptr;
		FString Error;
		if (!FBridgeAssetModifier::FindPropertyByPath(Config, PropertyName, Property, Container, Error))
		{
			return FBridgeToolResult::Error(Error);
		}
		if (!FBridgeAssetModifier::SetPropertyFromJson(Property, Container, Value, Error))
		{
			return FBridgeToolResult::Error(Error);
		}
		Changed.Add(MakeShared<FJsonValueString>(PropertyName));
	}

	Asset->InvalidateAllCachedData();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetStringField(TEXT("config_class"), Config->GetClass()->GetName());
	Result->SetArrayField(TEXT("changed_properties"), Changed);
	Result->SetNumberField(TEXT("changed_property_count"), Changed.Num());
	FString SaveError;
	if (!SaveMeshIfRequested(Mesh, GetBoolArgOrDefault(Arguments, TEXT("save"), false), Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	return FBridgeToolResult::Json(Result);
}

FString UClothApplyWeightMapTool::GetToolDescription() const
{
	return TEXT("Apply a legacy cloth weight map from a constant value, imported vertex color channel, root-bone distance falloff, or spatial selection, optionally restricted to merged source sections.");
}

TMap<FString, FBridgeSchemaProperty> UClothApplyWeightMapTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Existing clothing asset name"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Clothing asset LOD index")));
	Schema.Add(TEXT("target"), ClothSchemaProperty(TEXT("string"), TEXT("Runtime-supported Chaos weight map target"), false, GetBridgeLegacyWeightMapTargetNames()));
	Schema.Add(TEXT("section_indices"), ClothSchemaProperty(TEXT("array"), TEXT("Merged source SkeletalMesh section indices whose vertices may be updated")));
	Schema.Add(TEXT("rule"), ClothSchemaProperty(TEXT("string"), TEXT("Weight map generation rule"), true, { TEXT("constant"), TEXT("vertex-color"), TEXT("bone-distance"), TEXT("spatial") }));
	Schema.Add(TEXT("value"), ClothSchemaProperty(TEXT("number"), TEXT("Constant value, or selected vertex value for spatial")));
	Schema.Add(TEXT("channel"), ClothSchemaProperty(TEXT("string"), TEXT("Vertex-color channel"), false, { TEXT("red"), TEXT("green"), TEXT("blue"), TEXT("alpha") }));
	Schema.Add(TEXT("scale"), ClothSchemaProperty(TEXT("number"), TEXT("Vertex-color scale multiplier")));
	Schema.Add(TEXT("root_bone"), ClothSchemaProperty(TEXT("string"), TEXT("Root bone used by the bone-distance falloff rule")));
	Schema.Add(TEXT("min_distance"), ClothSchemaProperty(TEXT("number"), TEXT("Output weight value at the nearest cloth vertices")));
	Schema.Add(TEXT("max_distance"), ClothSchemaProperty(TEXT("number"), TEXT("Output weight value at the farthest cloth vertices")));
	Schema.Add(TEXT("min_value"), ClothSchemaProperty(TEXT("number"), TEXT("Spatial ramp value at z_min or the lowest selected cloth vertices")));
	Schema.Add(TEXT("max_value"), ClothSchemaProperty(TEXT("number"), TEXT("Spatial ramp value at z_max or the highest selected cloth vertices")));
	Schema.Add(TEXT("z_min"), ClothSchemaProperty(TEXT("number"), TEXT("Select vertices with local Z greater than or equal to this value")));
	Schema.Add(TEXT("z_max"), ClothSchemaProperty(TEXT("number"), TEXT("Select vertices with local Z less than or equal to this value")));
	Schema.Add(TEXT("center"), ClothSchemaProperty(TEXT("array"), TEXT("Sphere center [X, Y, Z] for spatial selection")));
	Schema.Add(TEXT("radius"), ClothSchemaProperty(TEXT("number"), TEXT("Sphere radius for spatial selection")));
	Schema.Add(TEXT("curve"), ClothSchemaProperty(TEXT("string"), TEXT("Bone-distance or spatial ramp falloff curve"), false, { TEXT("linear"), TEXT("smooth"), TEXT("ease") }));
	Schema.Add(TEXT("invert"), ClothSchemaProperty(TEXT("boolean"), TEXT("Invert the bone-distance falloff")));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the SkeletalMesh after mutation")));
	return Schema;
}

TArray<FString> UClothApplyWeightMapTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh"), TEXT("asset_name"), TEXT("rule") };
}

FBridgeToolResult UClothApplyWeightMapTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	USkeletalMesh* Mesh = nullptr;
	UClothingAssetBase* AssetBase = nullptr;
	FString SkeletalMeshPath;
	FString AssetName;
	FBridgeToolResult LoadResult = LoadMeshAndAsset(Arguments, Mesh, AssetBase, SkeletalMeshPath, AssetName);
	if (LoadResult.bIsError)
	{
		return LoadResult;
	}

	UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(AssetBase);
	if (!Asset)
	{
		return FBridgeToolResult::Error(TEXT("cloth-apply-weightmap requires a UClothingAssetCommon asset"));
	}

	const FString Target = GetStringArgOrDefault(Arguments, TEXT("target"), TEXT("max-distance"));
	FBridgeLegacyWeightMapTarget WeightMapTarget;
	FString TargetError;
	if (!ResolveBridgeLegacyWeightMapTarget(Target, WeightMapTarget, TargetError))
	{
		return FBridgeToolResult::Error(TargetError);
	}

	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	if (!Asset->LodData.IsValidIndex(LodIndex))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("lod_index %d is out of range"), LodIndex));
	}

	const FString Rule = GetStringArgOrDefault(Arguments, TEXT("rule"));
	if (!Rule.Equals(TEXT("constant"), ESearchCase::IgnoreCase)
		&& !Rule.Equals(TEXT("vertex-color"), ESearchCase::IgnoreCase)
		&& !Rule.Equals(TEXT("bone-distance"), ESearchCase::IgnoreCase)
		&& !Rule.Equals(TEXT("spatial"), ESearchCase::IgnoreCase))
	{
		return FBridgeToolResult::Error(TEXT("rule must be constant, vertex-color, bone-distance, or spatial"));
	}

	FClothLODDataCommon& LodData = Asset->LodData[LodIndex];
	FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
	const int32 VertexCount = PhysicalMesh.Vertices.Num();
	if (VertexCount <= 0)
	{
		return FBridgeToolResult::Error(TEXT("cloth: physical mesh has no vertices"));
	}

	TArray<int32> SectionIndices;
	TArray<bool> SectionSelection;
	int32 MultiSectionVertexCount = 0;
	const bool bHasSectionSelection = Arguments.IsValid()
		&& (Arguments->HasField(TEXT("section_indices")) || Arguments->HasField(TEXT("section_index")));
	if (bHasSectionSelection)
	{
		FString SectionError;
		if (!ParseSectionIndicesFromArgs(Arguments, SectionIndices, SectionError)
			|| !ReadBridgeSourceSectionSelection(LodData, SectionIndices, SectionSelection, SectionError))
		{
			return FBridgeToolResult::Error(SectionError);
		}
	}

	TArray<float> Values;
	Values.SetNum(VertexCount);
	TArray<float> ExistingValues;
	if (bHasSectionSelection)
	{
		ReadBridgeLegacyWeightMapValues(LodData, WeightMapTarget, ExistingValues);
	}
	int32 SpatialSelectedVertexCount = 0;
	if (Rule.Equals(TEXT("constant"), ESearchCase::IgnoreCase))
	{
		const float Value = GetFloatArgOrDefault(Arguments, TEXT("value"), 0.0f);
		for (float& Entry : Values)
		{
			Entry = Value;
		}
	}
	else if (Rule.Equals(TEXT("vertex-color"), ESearchCase::IgnoreCase))
	{
#if WITH_EDITORONLY_DATA
		if (PhysicalMesh.VertexColors.Num() != VertexCount)
		{
			return FBridgeToolResult::Error(TEXT("cloth: vertex-color rule requires imported vertex colors for every cloth vertex"));
		}
		const FString Channel = GetStringArgOrDefault(Arguments, TEXT("channel"), TEXT("red"));
		const float Scale = GetFloatArgOrDefault(Arguments, TEXT("scale"), 1.0f);
		for (int32 Index = 0; Index < VertexCount; ++Index)
		{
			Values[Index] = VertexColorChannelToFloat(PhysicalMesh.VertexColors[Index], Channel) * Scale;
		}
#else
		return FBridgeToolResult::Error(TEXT("cloth: vertex-color rule requires editor-only vertex color data"));
#endif
	}
	else if (Rule.Equals(TEXT("bone-distance"), ESearchCase::IgnoreCase))
	{
		const FString RootBone = GetStringArgOrDefault(Arguments, TEXT("root_bone"));
		FVector RootLocation;
		FString BoneError;
		if (!ResolveRefBoneLocation(Mesh, RootBone, RootLocation, BoneError))
		{
			return FBridgeToolResult::Error(BoneError);
		}

		float MaxDistance = 0.0f;
		if (!GetFloatArg(Arguments, TEXT("max_distance"), MaxDistance))
		{
			return FBridgeToolResult::Error(TEXT("cloth: max_distance is required for bone-distance weight maps"));
		}
		const float MinDistance = GetFloatArgOrDefault(Arguments, TEXT("min_distance"), 0.0f);
		if (MinDistance < 0.0f || MaxDistance < 0.0f || MaxDistance < MinDistance)
		{
			return FBridgeToolResult::Error(TEXT("cloth: min_distance and max_distance must be non-negative, with max_distance >= min_distance"));
		}

		const FString Curve = GetStringArgOrDefault(Arguments, TEXT("curve"), TEXT("linear"));
		const bool bInvert = GetBoolArgOrDefault(Arguments, TEXT("invert"), false);

		TArray<float> BoneDistances;
		BoneDistances.SetNum(VertexCount);
		for (int32 Index = 0; Index < VertexCount; ++Index)
		{
			const float BoneDistance = static_cast<float>(FVector::Distance(
				PhysicalVertexToVector(PhysicalMesh.Vertices[Index]),
				RootLocation));
			BoneDistances[Index] = BoneDistance;
		}

		FString FalloffError;
		if (!BuildBoneDistanceFalloffValues(BoneDistances, MinDistance, MaxDistance, Curve, bInvert, Values, FalloffError))
		{
			return FBridgeToolResult::Error(FalloffError);
		}
	}
	else
	{
		FString SpatialError;
		if (!BuildSpatialWeightMapValues(Arguments, LodData, WeightMapTarget, Values, SpatialSelectedVertexCount, SpatialError))
		{
			return FBridgeToolResult::Error(SpatialError);
		}
	}

	int32 SelectedVertexCount = bHasSectionSelection ? 0 : SpatialSelectedVertexCount;
	TArray<bool> FinalSelection;
	if (bHasSectionSelection)
	{
		TArray<bool> SpatialSelection;
		if (Rule.Equals(TEXT("spatial"), ESearchCase::IgnoreCase))
		{
			FString SpatialSelectionError;
			if (!BuildLegacyClothWeldSelection(Arguments, PhysicalMesh, SpatialSelection, SpatialSelectionError))
			{
				return FBridgeToolResult::Error(SpatialSelectionError);
			}
		}
		FString SelectionError;
		TArray<float> CommittedValues;
		if (!ApplyBridgeLegacySectionSelection(
			ExistingValues,
			Values,
			SectionSelection,
			SpatialSelection,
			CommittedValues,
			FinalSelection,
			SelectionError))
		{
			return FBridgeToolResult::Error(SelectionError);
		}
		Values = MoveTemp(CommittedValues);
		for (bool bSelected : FinalSelection)
		{
			SelectedVertexCount += bSelected ? 1 : 0;
		}
		MultiSectionVertexCount = CountBridgeSelectedMultiSectionVertices(LodData, FinalSelection);
	}

	FClothLODDataCommon PreviewLodData = LodData;
	ApplyBridgeLegacyWeightMapToLodData(PreviewLodData, Values, WeightMapTarget);
	TArray<FLegacyClothSectionMapping> GeneratedMappings;
	FString MappingError;
	if (CountLegacyClothBoundSections(Mesh, Asset, LodIndex) > 0
		&& !GenerateLegacyClothRenderMappings(Mesh, Asset, PreviewLodData, LodIndex, INDEX_NONE, GeneratedMappings, MappingError))
	{
		return FBridgeToolResult::Error(MappingError);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothApplyWeightMap", "Apply cloth weight map {0} on {1}"),
			FText::FromString(AssetName),
			FText::FromString(SkeletalMeshPath)));
	FBridgeAssetModifier::MarkModified(Mesh);
	FBridgeAssetModifier::MarkModified(Asset);

	LodData = MoveTemp(PreviewLodData);
	const TMap<uint32, FPointWeightMap> RemappedPhysicalWeightMaps = LodData.PhysicalMeshData.WeightMaps;
	if (GeneratedMappings.Num() > 0)
	{
		ApplyLegacyClothRenderMappings(Mesh, Asset, GeneratedMappings, false);
	}
	Asset->ApplyParameterMasks(false);
	RestorePhysicalOnlyLegacyClothWeightMaps(LodData.PhysicalMeshData, RemappedPhysicalWeightMaps);
	Asset->InvalidateAllCachedData();

	FPointWeightMap* PhysicalWeightMap = LodData.PhysicalMeshData.FindWeightMap(static_cast<EWeightMapTargetCommon>(WeightMapTarget.Id));
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetStringField(TEXT("target"), WeightMapTarget.CliName);
	Result->SetStringField(TEXT("rule"), Rule);
	if (bHasSectionSelection)
	{
		TArray<TSharedPtr<FJsonValue>> SectionValues;
		for (int32 SectionIndex : SectionIndices)
		{
			SectionValues.Add(MakeShared<FJsonValueNumber>(SectionIndex));
		}
		Result->SetArrayField(TEXT("section_indices"), SectionValues);
		Result->SetNumberField(TEXT("selected_vertex_count"), SelectedVertexCount);
		Result->SetNumberField(TEXT("multi_section_vertex_count"), MultiSectionVertexCount);
	}
	if (Rule.Equals(TEXT("bone-distance"), ESearchCase::IgnoreCase))
	{
		Result->SetStringField(TEXT("root_bone"), GetStringArgOrDefault(Arguments, TEXT("root_bone")));
		Result->SetNumberField(TEXT("min_distance"), GetFloatArgOrDefault(Arguments, TEXT("min_distance"), 0.0f));
		Result->SetNumberField(TEXT("max_distance"), GetFloatArgOrDefault(Arguments, TEXT("max_distance"), 0.0f));
		Result->SetStringField(TEXT("curve"), GetStringArgOrDefault(Arguments, TEXT("curve"), TEXT("linear")));
		Result->SetBoolField(TEXT("invert"), GetBoolArgOrDefault(Arguments, TEXT("invert"), false));
	}
	else if (Rule.Equals(TEXT("spatial"), ESearchCase::IgnoreCase))
	{
		Result->SetNumberField(TEXT("selected_vertex_count"), bHasSectionSelection ? SelectedVertexCount : SpatialSelectedVertexCount);
		if (Arguments.IsValid())
		{
			double Value = 0.0;
			if (Arguments->TryGetNumberField(TEXT("value"), Value))
			{
				Result->SetNumberField(TEXT("value"), Value);
			}
			double MinValue = 0.0;
			if (Arguments->TryGetNumberField(TEXT("min_value"), MinValue))
			{
				Result->SetNumberField(TEXT("min_value"), MinValue);
			}
			double MaxValue = 0.0;
			if (Arguments->TryGetNumberField(TEXT("max_value"), MaxValue))
			{
				Result->SetNumberField(TEXT("max_value"), MaxValue);
			}
			double ZMin = 0.0;
			if (Arguments->TryGetNumberField(TEXT("z_min"), ZMin))
			{
				Result->SetNumberField(TEXT("z_min"), ZMin);
			}
			double ZMax = 0.0;
			if (Arguments->TryGetNumberField(TEXT("z_max"), ZMax))
			{
				Result->SetNumberField(TEXT("z_max"), ZMax);
			}
		}
	}
	Result->SetObjectField(TEXT("weight_map"), WeightMapStatsToJson(PhysicalWeightMap));
	FString SaveError;
	if (!SaveMeshIfRequested(Mesh, GetBoolArgOrDefault(Arguments, TEXT("save"), false), Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	return FBridgeToolResult::Json(Result);
}

FString UClothWeldTool::GetToolDescription() const
{
	return TEXT("Weld coincident simulation vertices in a legacy in-mesh clothing asset physical mesh.");
}

TMap<FString, FBridgeSchemaProperty> UClothWeldTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Existing clothing asset name"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Clothing asset LOD index")));
	Schema.Add(TEXT("section_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Optional bound skeletal mesh section index to validate and remap")));
	Schema.Add(TEXT("tolerance"), ClothSchemaProperty(TEXT("number"), TEXT("Maximum physical mesh vertex distance to weld"), true));
	Schema.Add(TEXT("z_min"), ClothSchemaProperty(TEXT("number"), TEXT("Select vertices with local Z greater than or equal to this value")));
	Schema.Add(TEXT("z_max"), ClothSchemaProperty(TEXT("number"), TEXT("Select vertices with local Z less than or equal to this value")));
	Schema.Add(TEXT("center"), ClothSchemaProperty(TEXT("array"), TEXT("Sphere center [X, Y, Z] for spatial selection")));
	Schema.Add(TEXT("radius"), ClothSchemaProperty(TEXT("number"), TEXT("Sphere radius for spatial selection")));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the SkeletalMesh after mutation")));
	return Schema;
}

TArray<FString> UClothWeldTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh"), TEXT("asset_name"), TEXT("tolerance") };
}

FBridgeToolResult UClothWeldTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	USkeletalMesh* Mesh = nullptr;
	UClothingAssetBase* AssetBase = nullptr;
	FString SkeletalMeshPath;
	FString AssetName;
	FBridgeToolResult LoadResult = LoadMeshAndAsset(Arguments, Mesh, AssetBase, SkeletalMeshPath, AssetName);
	if (LoadResult.bIsError)
	{
		return LoadResult;
	}

	UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(AssetBase);
	if (!Asset)
	{
		return FBridgeToolResult::Error(TEXT("cloth-weld requires a UClothingAssetCommon asset"));
	}

	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	if (!Asset->LodData.IsValidIndex(LodIndex))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("lod_index %d is out of range"), LodIndex));
	}

	float Tolerance = 0.0f;
	if (!GetFloatArg(Arguments, TEXT("tolerance"), Tolerance))
	{
		return FBridgeToolResult::Error(TEXT("cloth: tolerance is required"));
	}
	if (Tolerance < 0.0f)
	{
		return FBridgeToolResult::Error(TEXT("cloth: tolerance must be non-negative"));
	}

	FClothLODDataCommon& LodData = Asset->LodData[LodIndex];
	TArray<bool> SelectedVertices;
	FString Error;
	if (!BuildLegacyClothWeldSelection(Arguments, LodData.PhysicalMeshData, SelectedVertices, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	const int32 SectionIndex = GetIntArgOrDefault(Arguments, TEXT("section_index"), INDEX_NONE);
	FClothLODDataCommon PreviewLodData = LodData;
	FLegacyClothWeldResult WeldResult;
	if (!WeldLegacyPhysicalMeshVertices(PreviewLodData, Tolerance, SelectedVertices, WeldResult, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TArray<FLegacyClothSectionMapping> GeneratedMappings;
	if (!GenerateLegacyClothRenderMappings(Mesh, Asset, PreviewLodData, LodIndex, SectionIndex, GeneratedMappings, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothWeld", "Weld cloth physical mesh {0} on {1}"),
			FText::FromString(AssetName),
			FText::FromString(SkeletalMeshPath)));
	FScopedSkeletalMeshPostEditChange WeldPostEditChange(Mesh);
	FBridgeAssetModifier::MarkModified(Mesh);
	FBridgeAssetModifier::MarkModified(Asset);

	LodData = MoveTemp(PreviewLodData);
	const TMap<uint32, FPointWeightMap> RemappedPhysicalWeightMaps = LodData.PhysicalMeshData.WeightMaps;
	ApplyLegacyClothRenderMappings(Mesh, Asset, GeneratedMappings, true);

	Asset->RefreshBoneMapping(Mesh);
	Asset->BuildLodTransitionData();
	Asset->ApplyParameterMasks(true);
	RestorePhysicalOnlyLegacyClothWeightMaps(LodData.PhysicalMeshData, RemappedPhysicalWeightMaps);
	Asset->InvalidateAllCachedData();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetNumberField(TEXT("section_index"), SectionIndex);
	Result->SetNumberField(TEXT("tolerance"), Tolerance);
	Result->SetNumberField(TEXT("original_vertex_count"), WeldResult.OriginalVertexCount);
	Result->SetNumberField(TEXT("final_vertex_count"), WeldResult.FinalVertexCount);
	Result->SetNumberField(TEXT("selected_vertex_count"), WeldResult.SelectedVertexCount);
	Result->SetNumberField(TEXT("welded_vertex_count"), WeldResult.WeldedVertexCount);
	Result->SetNumberField(TEXT("weld_group_count"), WeldResult.WeldGroupCount);
	Result->SetNumberField(TEXT("removed_degenerate_triangle_count"), WeldResult.RemovedDegenerateTriangleCount);
	Result->SetNumberField(TEXT("remapped_section_count"), GeneratedMappings.Num());

	FString SaveError;
	if (!SaveMeshIfRequested(Mesh, GetBoolArgOrDefault(Arguments, TEXT("save"), false), Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	return FBridgeToolResult::Json(Result);
}

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(
	FClothWeightMapFalloffSpec,
	"SoftUEBridge.Cloth.WeightMapFalloff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FClothWeightMapFalloffSpec)

void FClothWeightMapFalloffSpec::Define()
{
	Describe("BuildBoneDistanceFalloffValues", [this]()
	{
		It("maps nearest vertices to min_distance and farthest vertices to max_distance", [this]()
		{
			TArray<float> Distances;
			Distances.Add(10.0f);
			Distances.Add(20.0f);
			Distances.Add(30.0f);

			TArray<float> Values;
			FString Error;
			const bool bBuilt = BuildBoneDistanceFalloffValues(
				Distances,
				0.0f,
				80.0f,
				TEXT("linear"),
				false,
				Values,
				Error);

			TestTrue(TEXT("falloff built"), bBuilt);
			TestEqual(TEXT("value count"), Values.Num(), 3);
			TestEqual(TEXT("nearest value"), Values[0], 0.0f);
			TestEqual(TEXT("middle value"), Values[1], 40.0f);
			TestEqual(TEXT("farthest value"), Values[2], 80.0f);
		});

		It("inverts the falloff when requested", [this]()
		{
			TArray<float> Distances;
			Distances.Add(10.0f);
			Distances.Add(20.0f);
			Distances.Add(30.0f);

			TArray<float> Values;
			FString Error;
			const bool bBuilt = BuildBoneDistanceFalloffValues(
				Distances,
				0.0f,
				80.0f,
				TEXT("linear"),
				true,
				Values,
				Error);

			TestTrue(TEXT("falloff built"), bBuilt);
			TestEqual(TEXT("nearest inverted value"), Values[0], 80.0f);
			TestEqual(TEXT("middle inverted value"), Values[1], 40.0f);
			TestEqual(TEXT("farthest inverted value"), Values[2], 0.0f);
		});

		It("rejects invalid ranges, invalid curves, and uniform root distances", [this]()
		{
			TArray<float> Distances;
			Distances.Add(10.0f);
			Distances.Add(20.0f);

			TArray<float> Values;
			FString Error;
			TestFalse(
				TEXT("max below min rejected"),
				BuildBoneDistanceFalloffValues(Distances, 80.0f, 0.0f, TEXT("linear"), false, Values, Error));
			TestTrue(TEXT("range error reported"), Error.Contains(TEXT("max_distance >= min_distance")));

			Error.Reset();
			TestFalse(
				TEXT("invalid curve rejected"),
				BuildBoneDistanceFalloffValues(Distances, 0.0f, 80.0f, TEXT("bad"), false, Values, Error));
			TestTrue(TEXT("curve error reported"), Error.Contains(TEXT("curve must be linear")));

			TArray<float> UniformDistances;
			UniformDistances.Add(10.0f);
			UniformDistances.Add(10.0f);
			Error.Reset();
			TestFalse(
				TEXT("uniform distances rejected"),
				BuildBoneDistanceFalloffValues(UniformDistances, 0.0f, 80.0f, TEXT("linear"), false, Values, Error));
			TestTrue(TEXT("uniform error reported"), Error.Contains(TEXT("non-uniform distances")));
		});

		It("builds spatial ramp values while preserving unselected vertices", [this]()
		{
			const FBridgeLegacyWeightMapTarget MaxDistanceTarget = MakeTestLegacyWeightMapTarget(EWeightMapTargetCommon::MaxDistance, TEXT("max-distance"), TEXT("MaxDistance"));
			FClothLODDataCommon LodData;
			FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
			PhysicalMesh.Vertices = {
				FVector3f(0.0f, 0.0f, 0.0f),
				FVector3f(0.0f, 0.0f, 50.0f),
				FVector3f(0.0f, 0.0f, 100.0f),
			};
			FPointWeightMap& Existing = PhysicalMesh.FindOrAddWeightMap(EWeightMapTargetCommon::MaxDistance);
			Existing.Values = { 3.0f, 4.0f, 5.0f };

			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetNumberField(TEXT("z_min"), 50.0);
			Args->SetNumberField(TEXT("z_max"), 100.0);
			Args->SetNumberField(TEXT("min_value"), 0.0);
			Args->SetNumberField(TEXT("max_value"), 20.0);

			TArray<float> Values;
			int32 SelectedVertexCount = 0;
			FString Error;
			const bool bBuilt = BuildSpatialWeightMapValues(Args, LodData, MaxDistanceTarget, Values, SelectedVertexCount, Error);

			TestTrue(TEXT("spatial values built"), bBuilt);
			TestEqual(TEXT("value count"), Values.Num(), 3);
			TestEqual(TEXT("selected count"), SelectedVertexCount, 2);
			TestEqual(TEXT("unselected value preserved"), Values[0], 3.0f);
			TestEqual(TEXT("z-min value"), Values[1], 0.0f);
			TestEqual(TEXT("z-max value"), Values[2], 20.0f);
		});

		It("preserves existing values for the selected legacy target", [this]()
		{
			const FBridgeLegacyWeightMapTarget AnimDriveTarget = MakeTestLegacyWeightMapTarget(EWeightMapTargetCommon::AnimDriveStiffness, TEXT("anim-drive-stiffness"), TEXT("AnimDriveStiffness"));
			FClothLODDataCommon LodData;
			FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
			PhysicalMesh.Vertices = {
				FVector3f(0.0f, 0.0f, 0.0f),
				FVector3f(0.0f, 0.0f, 50.0f),
			};
			FPointWeightMap& MaxDistance = PhysicalMesh.FindOrAddWeightMap(EWeightMapTargetCommon::MaxDistance);
			MaxDistance.Values = { 3.0f, 4.0f };
			FPointWeightMap& AnimDrive = PhysicalMesh.FindOrAddWeightMap(EWeightMapTargetCommon::AnimDriveStiffness);
			AnimDrive.Values = { 0.25f, 0.5f };

			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetNumberField(TEXT("z_min"), 50.0);
			Args->SetNumberField(TEXT("z_max"), 50.0);
			Args->SetNumberField(TEXT("value"), 1.0);

			TArray<float> Values;
			int32 SelectedVertexCount = 0;
			FString Error;
			const bool bBuilt = BuildSpatialWeightMapValues(Args, LodData, AnimDriveTarget, Values, SelectedVertexCount, Error);

			TestTrue(TEXT("spatial values built"), bBuilt);
			TestEqual(TEXT("value count"), Values.Num(), 2);
			TestEqual(TEXT("selected count"), SelectedVertexCount, 1);
			TestEqual(TEXT("unselected anim-drive value preserved"), Values[0], 0.25f);
			TestEqual(TEXT("selected anim-drive value changed"), Values[1], 1.0f);
		});

		It("uses the effective duplicate mask values for spatial preservation", [this]()
		{
			const FBridgeLegacyWeightMapTarget AnimDriveTarget = MakeTestLegacyWeightMapTarget(EWeightMapTargetCommon::AnimDriveStiffness, TEXT("anim-drive-stiffness"), TEXT("AnimDriveStiffness"));
			FClothLODDataCommon LodData;
			FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
			PhysicalMesh.Vertices = {
				FVector3f(0.0f, 0.0f, 0.0f),
				FVector3f(0.0f, 0.0f, 50.0f),
			};

			FPointWeightMap& FirstAnimDrive = LodData.PointWeightMaps.AddDefaulted_GetRef();
			FirstAnimDrive.Values = { 0.25f, 0.5f };
			ConfigureBridgeLegacyWeightMapMetadata(FirstAnimDrive, AnimDriveTarget);
			FPointWeightMap& SecondAnimDrive = LodData.PointWeightMaps.AddDefaulted_GetRef();
			SecondAnimDrive.Values = { 0.75f, 0.9f };
			ConfigureBridgeLegacyWeightMapMetadata(SecondAnimDrive, AnimDriveTarget);

			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetNumberField(TEXT("z_min"), 50.0);
			Args->SetNumberField(TEXT("z_max"), 50.0);
			Args->SetNumberField(TEXT("value"), 1.0);

			TArray<float> Values;
			int32 SelectedVertexCount = 0;
			FString Error;
			const bool bBuilt = BuildSpatialWeightMapValues(Args, LodData, AnimDriveTarget, Values, SelectedVertexCount, Error);

			TestTrue(TEXT("spatial values built"), bBuilt);
			TestEqual(TEXT("unselected effective duplicate value preserved"), Values[0], 0.75f);

			ApplyBridgeLegacyWeightMapToLodData(LodData, Values, AnimDriveTarget);

			int32 MatchingMaskCount = 0;
			for (const FPointWeightMap& PointWeightMap : LodData.PointWeightMaps)
			{
				if (PointWeightMap.CurrentTarget == static_cast<uint8>(EWeightMapTargetCommon::AnimDriveStiffness))
				{
					++MatchingMaskCount;
				}
			}
			TestEqual(TEXT("duplicate target masks collapsed"), MatchingMaskCount, 1);
			const FPointWeightMap* PhysicalAnimDrive = LodData.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::AnimDriveStiffness);
			TestNotNull(TEXT("physical anim-drive map exists"), PhysicalAnimDrive);
			if (PhysicalAnimDrive)
			{
				TestEqual(TEXT("physical unselected value"), PhysicalAnimDrive->Values[0], 0.75f);
				TestEqual(TEXT("physical selected value"), PhysicalAnimDrive->Values[1], 1.0f);
			}
		});

		It("remaps source-section provenance with union semantics", [this]()
		{
			TArray<FPointWeightMap> PointWeightMaps;
			FPointWeightMap& Provenance = PointWeightMaps.AddDefaulted_GetRef();
			Provenance.Name = FName(TEXT("SoftUESourceSection_2"));
			Provenance.CurrentTarget = static_cast<uint8>(EWeightMapTargetCommon::None);
			Provenance.bEnabled = false;
			Provenance.Values = { 0.0f, 1.0f, 0.0f };

			TArray<TArray<int32>> NewVertexToOldVertices;
			NewVertexToOldVertices.Add(TArray<int32>{ 0, 1 });
			NewVertexToOldVertices.Add(TArray<int32>{ 2 });
			RemapLegacyClothPointWeightMaps(PointWeightMaps, NewVertexToOldVertices);

			TestEqual(TEXT("welded membership uses logical OR"), PointWeightMaps[0].Values[0], 1.0f);
			TestEqual(TEXT("unrelated membership remains false"), PointWeightMaps[0].Values[1], 0.0f);
			TestFalse(TEXT("provenance remains disabled"), PointWeightMaps[0].bEnabled);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS

FString UClothSetCollisionTool::GetToolDescription() const
{
	return TEXT("Assign the PhysicsAsset used by an existing clothing asset for collision extraction.");
}

TMap<FString, FBridgeSchemaProperty> UClothSetCollisionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Existing clothing asset name"), true));
	Schema.Add(TEXT("physics_asset"), ClothSchemaProperty(TEXT("string"), TEXT("PhysicsAsset asset path"), true));
	Schema.Add(TEXT("save"), ClothSchemaProperty(TEXT("boolean"), TEXT("Save the SkeletalMesh after mutation")));
	return Schema;
}

TArray<FString> UClothSetCollisionTool::GetRequiredParams() const
{
	return { TEXT("skeletal_mesh"), TEXT("asset_name"), TEXT("physics_asset") };
}

FBridgeToolResult UClothSetCollisionTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	(void)Context;
	USkeletalMesh* Mesh = nullptr;
	UClothingAssetBase* AssetBase = nullptr;
	FString SkeletalMeshPath;
	FString AssetName;
	FBridgeToolResult LoadResult = LoadMeshAndAsset(Arguments, Mesh, AssetBase, SkeletalMeshPath, AssetName);
	if (LoadResult.bIsError)
	{
		return LoadResult;
	}

	UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(AssetBase);
	if (!Asset)
	{
		return FBridgeToolResult::Error(TEXT("cloth-set-collision requires a UClothingAssetCommon asset"));
	}

	const FString PhysicsAssetPath = GetStringArgOrDefault(Arguments, TEXT("physics_asset"));
	if (PhysicsAssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("physics_asset is required"));
	}

	FString LoadError;
	UPhysicsAsset* PhysicsAsset = FBridgeAssetModifier::LoadAssetByPath<UPhysicsAsset>(PhysicsAssetPath, LoadError);
	if (!PhysicsAsset)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothSetCollision", "Set cloth collision {0} on {1}"),
			FText::FromString(AssetName),
			FText::FromString(SkeletalMeshPath)));
	FBridgeAssetModifier::MarkModified(Mesh);
	FBridgeAssetModifier::MarkModified(Asset);
	Asset->PhysicsAsset = PhysicsAsset;
	Asset->InvalidateAllCachedData();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetStringField(TEXT("physics_asset"), PhysicsAsset->GetPathName());
	FString SaveError;
	if (!SaveMeshIfRequested(Mesh, GetBoolArgOrDefault(Arguments, TEXT("save"), false), Result, SaveError))
	{
		return FBridgeToolResult::Error(SaveError);
	}
	return FBridgeToolResult::Json(Result);
}
