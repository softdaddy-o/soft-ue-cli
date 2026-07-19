// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Cloth/ClothTools.h"

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
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgeJsonObjectUtils.h"

#if WITH_DEV_AUTOMATION_TESTS
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

void ClearOriginalSectionClothData(FSkelMeshSourceSectionUserData& OriginalSectionData)
{
	OriginalSectionData.CorrespondClothAssetIndex = INDEX_NONE;
	OriginalSectionData.ClothingData.AssetGuid = FGuid();
	OriginalSectionData.ClothingData.AssetLodIndex = INDEX_NONE;
}

void ConfigureWeightMapMetadata(FPointWeightMap& WeightMap, EWeightMapTargetCommon Target);

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
		CurrentAsset->UnbindFromSkeletalMesh(Mesh, LodIndex, SectionIndex);
	}

	Asset->Modify();
	// Repair assets left with a populated LodMap but no section binding by older bridge versions.
	if (bClearExistingAssetBindings)
	{
		Asset->UnbindFromSkeletalMesh(Mesh, LodIndex, INDEX_NONE);
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

	FPointWeightMap& PhysMeshMaxDistances = PhysMesh.AddWeightMap(EWeightMapTargetCommon::MaxDistance);
	PhysMeshMaxDistances.Initialize(PhysMesh.Vertices.Num());
	ConfigureWeightMapMetadata(PhysMeshMaxDistances, EWeightMapTargetCommon::MaxDistance);

	LodData.PointWeightMaps.AddDefaulted();
	FPointWeightMap& LodMaxDistances = LodData.PointWeightMaps.Last();
	LodMaxDistances.Initialize(PhysMeshMaxDistances, EWeightMapTargetCommon::MaxDistance);
	ConfigureWeightMapMetadata(LodMaxDistances, EWeightMapTargetCommon::MaxDistance);

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

void ConfigureWeightMapMetadata(FPointWeightMap& WeightMap, EWeightMapTargetCommon Target)
{
#if WITH_EDITORONLY_DATA
	WeightMap.Name = FName(TEXT("MaxDistance"));
	WeightMap.CurrentTarget = static_cast<uint8>(Target);
	WeightMap.bEnabled = true;
#endif
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

TSharedPtr<FJsonObject> ChaosClothCollectionToJson(
	const TSharedRef<const FManagedArrayCollection>& Collection,
	int32 LodIndex)
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
	for (int32 SeamIndex = 0; SeamIndex < Cloth.GetNumSeams(); ++SeamIndex)
	{
		FCollectionClothSeamConstFacade Seam = Cloth.GetSeam(SeamIndex);
		TSharedPtr<FJsonObject> SeamJson = MakeShared<FJsonObject>();
		SeamJson->SetNumberField(TEXT("seam_index"), SeamIndex);
		SeamJson->SetNumberField(TEXT("stitch_count"), Seam.GetNumSeamStitches());
		SeamJson->SetNumberField(TEXT("stitch_offset"), Seam.GetSeamStitchesOffset());
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
	return LodJson;
}

TSharedPtr<FJsonObject> ChaosClothAssetToJson(
	UChaosClothAsset* Asset,
	const FString& ClothAssetPath,
	bool bIncludeNodes)
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
		LodValues.Add(MakeShared<FJsonValueObject>(ChaosClothCollectionToJson(Collections[LodIndex], LodIndex)));
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
			WeightMapCount += LodData.PointWeightMaps.Num();
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

	if (Asset->HasDataflow() || Asset->HasValidClothSimulationModels() || HasChaosClothCollectionData(Asset))
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

TArray<TSharedPtr<FJsonValue>> BuildBindingArray(USkeletalMesh* Mesh, int32 LodFilter = INDEX_NONE)
{
	TArray<TSharedPtr<FJsonValue>> BindingValues;
#if WITH_EDITOR
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> Bindings;
	if (LodFilter == INDEX_NONE)
	{
		ClothingAssetUtils::GetAllMeshClothingAssetBindings(Mesh, Bindings);
	}
	else
	{
		ClothingAssetUtils::GetAllLodMeshClothingAssetBindings(Mesh, Bindings, LodFilter);
	}
	for (const ClothingAssetUtils::FClothingAssetMeshBinding& Binding : Bindings)
	{
		TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
		BindingJson->SetStringField(TEXT("asset_name"), Binding.Asset ? Binding.Asset->GetName() : TEXT(""));
		BindingJson->SetNumberField(TEXT("lod_index"), Binding.LODIndex);
		BindingJson->SetNumberField(TEXT("section_index"), Binding.SectionIndex);
		BindingJson->SetNumberField(TEXT("asset_lod_index"), Binding.AssetInternalLodIndex);
		BindingValues.Add(MakeShared<FJsonValueObject>(BindingJson));
	}
#endif
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

	TArray<TSharedPtr<FJsonValue>> Bindings = BuildBindingArray(Mesh, LodFilter);
	Result->SetArrayField(TEXT("bindings"), Bindings);
	Result->SetNumberField(TEXT("binding_count"), Bindings.Num());
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
	if (ClothAssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("cloth_asset is required"));
	}

	FString LoadError;
	UChaosClothAsset* Asset = FBridgeAssetModifier::LoadAssetByPath<UChaosClothAsset>(ClothAssetPath, LoadError);
	if (!Asset)
	{
		return FBridgeToolResult::Error(LoadError.IsEmpty() ? FString::Printf(TEXT("cloth chaos-query: asset is not a UChaosClothAsset: %s"), *ClothAssetPath) : LoadError);
	}

	return FBridgeToolResult::Json(ChaosClothAssetToJson(Asset, ClothAssetPath, bIncludeNodes));
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
	Result->SetBoolField(TEXT("dataflow_based"), NewAsset->HasDataflow());
	Result->SetStringField(TEXT("conversion_mode"), NewAsset->HasDataflow() ? TEXT("dataflow") : TEXT("legacy_collection"));
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
	Result->SetArrayField(TEXT("bindings"), BuildBindingArray(Mesh));
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
	return TEXT("Apply a max-distance cloth weight map from a constant value, imported vertex color channel, or root-bone distance falloff.");
}

TMap<FString, FBridgeSchemaProperty> UClothApplyWeightMapTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("skeletal_mesh"), ClothSchemaProperty(TEXT("string"), TEXT("SkeletalMesh asset path"), true));
	Schema.Add(TEXT("asset_name"), ClothSchemaProperty(TEXT("string"), TEXT("Existing clothing asset name"), true));
	Schema.Add(TEXT("lod_index"), ClothSchemaProperty(TEXT("integer"), TEXT("Clothing asset LOD index")));
	Schema.Add(TEXT("target"), ClothSchemaProperty(TEXT("string"), TEXT("Weight map target"), false, { TEXT("max-distance") }));
	Schema.Add(TEXT("rule"), ClothSchemaProperty(TEXT("string"), TEXT("Weight map generation rule"), true, { TEXT("constant"), TEXT("vertex-color"), TEXT("bone-distance") }));
	Schema.Add(TEXT("value"), ClothSchemaProperty(TEXT("number"), TEXT("Constant rule value")));
	Schema.Add(TEXT("channel"), ClothSchemaProperty(TEXT("string"), TEXT("Vertex-color channel"), false, { TEXT("red"), TEXT("green"), TEXT("blue"), TEXT("alpha") }));
	Schema.Add(TEXT("scale"), ClothSchemaProperty(TEXT("number"), TEXT("Vertex-color scale multiplier")));
	Schema.Add(TEXT("root_bone"), ClothSchemaProperty(TEXT("string"), TEXT("Root bone used by the bone-distance falloff rule")));
	Schema.Add(TEXT("min_distance"), ClothSchemaProperty(TEXT("number"), TEXT("Output max-distance value at the nearest cloth vertices")));
	Schema.Add(TEXT("max_distance"), ClothSchemaProperty(TEXT("number"), TEXT("Output max-distance value at the farthest cloth vertices")));
	Schema.Add(TEXT("curve"), ClothSchemaProperty(TEXT("string"), TEXT("Bone-distance falloff curve"), false, { TEXT("linear"), TEXT("smooth"), TEXT("ease") }));
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
	if (!Target.Equals(TEXT("max-distance"), ESearchCase::IgnoreCase))
	{
		return FBridgeToolResult::Error(TEXT("cloth: only target=max-distance is supported"));
	}

	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	if (!Asset->LodData.IsValidIndex(LodIndex))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("lod_index %d is out of range"), LodIndex));
	}

	const FString Rule = GetStringArgOrDefault(Arguments, TEXT("rule"));
	if (!Rule.Equals(TEXT("constant"), ESearchCase::IgnoreCase)
		&& !Rule.Equals(TEXT("vertex-color"), ESearchCase::IgnoreCase)
		&& !Rule.Equals(TEXT("bone-distance"), ESearchCase::IgnoreCase))
	{
		return FBridgeToolResult::Error(TEXT("rule must be constant, vertex-color, or bone-distance"));
	}

	FClothLODDataCommon& LodData = Asset->LodData[LodIndex];
	FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
	const int32 VertexCount = PhysicalMesh.Vertices.Num();
	if (VertexCount <= 0)
	{
		return FBridgeToolResult::Error(TEXT("cloth: physical mesh has no vertices"));
	}

	TArray<float> Values;
	Values.SetNum(VertexCount);
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
	else
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

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "ClothApplyWeightMap", "Apply cloth weight map {0} on {1}"),
			FText::FromString(AssetName),
			FText::FromString(SkeletalMeshPath)));
	FBridgeAssetModifier::MarkModified(Mesh);
	FBridgeAssetModifier::MarkModified(Asset);

	FPointWeightMap& PhysicalWeightMap = PhysicalMesh.FindOrAddWeightMap(EWeightMapTargetCommon::MaxDistance);
	PhysicalWeightMap.Values = Values;
	ConfigureWeightMapMetadata(PhysicalWeightMap, EWeightMapTargetCommon::MaxDistance);

#if WITH_EDITORONLY_DATA
	FPointWeightMap* PointWeightMap = nullptr;
	for (FPointWeightMap& Candidate : LodData.PointWeightMaps)
	{
		if (Candidate.CurrentTarget == static_cast<uint8>(EWeightMapTargetCommon::MaxDistance))
		{
			PointWeightMap = &Candidate;
			break;
		}
	}
	if (!PointWeightMap)
	{
		PointWeightMap = &LodData.PointWeightMaps.AddDefaulted_GetRef();
	}
	PointWeightMap->Values = Values;
	ConfigureWeightMapMetadata(*PointWeightMap, EWeightMapTargetCommon::MaxDistance);
#endif

	LodData.PushWeightsToMesh();
	Asset->ApplyParameterMasks(true);
	Asset->InvalidateAllCachedData();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetStringField(TEXT("target"), TEXT("max-distance"));
	Result->SetStringField(TEXT("rule"), Rule);
	if (Rule.Equals(TEXT("bone-distance"), ESearchCase::IgnoreCase))
	{
		Result->SetStringField(TEXT("root_bone"), GetStringArgOrDefault(Arguments, TEXT("root_bone")));
		Result->SetNumberField(TEXT("min_distance"), GetFloatArgOrDefault(Arguments, TEXT("min_distance"), 0.0f));
		Result->SetNumberField(TEXT("max_distance"), GetFloatArgOrDefault(Arguments, TEXT("max_distance"), 0.0f));
		Result->SetStringField(TEXT("curve"), GetStringArgOrDefault(Arguments, TEXT("curve"), TEXT("linear")));
		Result->SetBoolField(TEXT("invert"), GetBoolArgOrDefault(Arguments, TEXT("invert"), false));
	}
	Result->SetObjectField(TEXT("weight_map"), WeightMapStatsToJson(&PhysicalWeightMap));
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
