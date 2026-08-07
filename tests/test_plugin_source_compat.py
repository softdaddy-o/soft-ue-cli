from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLUGIN_SOURCE = ROOT / "soft_ue_cli" / "plugin_data" / "SoftUEBridge" / "Source"


def _read(relative_path: str) -> str:
    return (PLUGIN_SOURCE / relative_path).read_text(encoding="utf-8")


def test_cloth_query_uses_safe_section_binding_enumerator():
    query_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/ClothTools.cpp")
    utility_header = _read("SoftUEBridgeEditor/Private/Tools/Cloth/BridgeClothBindings.h")
    utility_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/BridgeClothBindings.cpp")

    query_block = query_source.split("TSharedPtr<FJsonObject> BuildQueryResult(", 1)[1].split(
        "UClothConfigBase* ResolveClothConfig", 1
    )[0]
    assert "BridgeClothBindings::Collect" in query_block
    assert "GetAllMeshClothingAssetBindings" not in query_block
    assert "GetAllLodMeshClothingAssetBindings" not in query_block
    assert 'SetArrayField(TEXT("binding_warnings")' in query_block
    assert 'SetNumberField(TEXT("binding_warning_count")' in query_block

    assert "struct FBindingRecord" in utility_header
    assert "struct FBindingWarning" in utility_header
    assert "ValidateResolvedAssetLod" in utility_header
    assert "FSkeletalMeshModel" in utility_source
    assert "LODModels" in utility_source
    assert "Sections" in utility_source
    assert "Section.HasClothingData()" in utility_source
    assert "unsupported_clothing_asset_type" in utility_source
    assert "GetAllMeshClothingAssetBindings" not in utility_source
    assert "GetAllLodMeshClothingAssetBindings" not in utility_source

    spec_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/BridgeClothBindingsSpec.cpp")
    assert "ClothMappingDataLODs" in spec_source
    assert "ignores metadata without cloth mapping data" in spec_source
    assert "warns when cloth mapping has a default asset GUID" in spec_source
    assert "rejects a resolved non-common clothing asset type" in spec_source
    assert "unsupported_clothing_asset_type" in spec_source

    assert "BuildBindingArray(USkeletalMesh* Mesh)" not in query_source
    assert query_source.count("TArray<TSharedPtr<FJsonValue>> BuildBindingArray(") == 1


def test_anim_montage_segment_helpers_are_unique_for_unity_builds():
    inspect_source = _read("SoftUEBridgeEditor/Private/Tools/Animation/AnimMontageInspectTool.cpp")
    slot_source = _read("SoftUEBridgeEditor/Private/Tools/Animation/AnimMontageSlotTool.cpp")

    assert "TSharedPtr<FJsonObject> SegmentToJson(" not in inspect_source
    assert "TSharedPtr<FJsonObject> SegmentToJson(" not in slot_source
    assert "MontageInspectSegmentToJson" in inspect_source
    assert "MontageSlotSegmentToJson" in slot_source


def test_editor_module_uses_ue58_post_engine_init_accessor():
    source = _read("SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp")

    assert "ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8" in source
    assert "FCoreDelegates::GetOnPostEngineInit().AddRaw" in source
    assert "FCoreDelegates::GetOnPostEngineInit().Remove" in source


def test_editor_tools_avoid_ue58_deprecated_object_and_package_apis():
    object_sources = {
        "SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp":
            "EGetObjectsFlags::IncludeNestedObjects",
        "SoftUEBridgeEditor/Private/Tools/Asset/MutableIntrospectionUtils.cpp":
            "EGetObjectsFlags::IncludeNestedObjects",
        "SoftUEBridgeEditor/Private/Tools/Write/SetNodePositionTool.cpp":
            "EGetObjectsFlags::IncludeNestedObjects",
    }

    for relative_path, required_text in object_sources.items():
        source = _read(relative_path)
        assert "ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8" in source
        assert required_text in source

    wire_source = _read("SoftUEBridgeEditor/Private/Tools/Widget/WireWidgetNavigationTool.cpp")
    assert "UE::IsSavingPackage()" in wire_source
    assert "GIsSavingPackage" not in wire_source


def test_skeletal_mesh_loaders_are_unique_for_unity_builds():
    cloth_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/ClothTools.cpp")
    socket_source = _read("SoftUEBridgeEditor/Private/Tools/Asset/SkeletalMeshSocketTool.cpp")

    # Both files defined LoadSkeletalMesh in an anonymous namespace. Anonymous
    # namespaces do not isolate translation units under a unity build - both
    # files land in one .cpp and the second definition is a redefinition error.
    assert "USkeletalMesh* LoadSkeletalMesh(" not in cloth_source
    assert "USkeletalMesh* LoadSkeletalMesh(" not in socket_source
    assert "ClothLoadSkeletalMesh" in cloth_source
    assert "SocketToolLoadSkeletalMesh" in socket_source


def test_rewind_helper_avoids_removed_trace_file_loaded_check():
    source = _read("SoftUEBridgeEditor/Private/Tools/Rewind/RewindHelper.cpp")

    assert "IsTraceFileLoaded()" not in source
    assert "Debugger->IsRecording()" in source


def test_set_node_property_supports_nested_inner_anim_node_paths_and_struct_arrays():
    set_node_source = _read("SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp")
    anim_props_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAnimNodeProperties.cpp")
    modifier_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAssetModifier.cpp")
    serializer_source = _read("SoftUEBridgeEditor/Private/Utils/BridgePropertySerializer.cpp")

    assert "FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath" in set_node_source
    assert "ResolvePropertyPathAgainstStruct" in anim_props_source
    assert 'InnerPath.StartsWith(TEXT("Node."), ESearchCase::IgnoreCase)' in anim_props_source
    assert "FArrayProperty* ArrayProp = CastField<FArrayProperty>" in modifier_source
    assert "DeserializeArrayProperty" in serializer_source
    assert "FBridgePropertySerializer::DeserializePropertyValue(ArrayProp->Inner, ElementPtr, ElementValue" in serializer_source


def test_inner_anim_node_lookup_is_not_hardcoded_to_a_member_named_node():
    anim_props_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAnimNodeProperties.cpp")

    # The inner FAnimNode_* member is conventionally named "Node", but that is not
    # guaranteed - discovery must fall back to a type scan.
    assert "FAnimNode_Base::StaticStruct()" in anim_props_source
    assert "TFieldIterator<FStructProperty>" in anim_props_source

    for relative_path in (
        "SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp",
        "SoftUEBridgeEditor/Private/Tools/Write/AddGraphNodeTool.cpp",
    ):
        source = _read(relative_path)
        assert 'FindPropertyByName(TEXT("Node"))' not in source


def test_node_property_tools_report_unresolved_properties_with_node_context():
    anim_props_header = _read("SoftUEBridgeEditor/Public/Utils/BridgeAnimNodeProperties.h")
    anim_props_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAnimNodeProperties.cpp")
    assert "FBridgePropertyPathFailure" in anim_props_header
    assert "SetPathFailure" in anim_props_source
    assert "UStruct* SegmentContext = CurrentStruct" in anim_props_source
    assert "WalkPathTypes" not in anim_props_source
    assert 'ParseIntoArray(Segments, TEXT("."), false)' in anim_props_source
    assert "Preflight every segment's syntax before traversing" in anim_props_source
    assert "BracketEnd != Segment.Len() - 1" in anim_props_source
    assert 'Segment.Contains(TEXT("]"))' in anim_props_source
    assert "ParseStrictNonNegativeInt32" in anim_props_source
    assert "FCString::IsNumeric" not in anim_props_source
    assert "FCString::Atoi" not in anim_props_source
    assert "MAX_int32 - Digit" in anim_props_source
    assert 'TEXT("(empty segment)")' in anim_props_source
    assert "OutError.Reset()" in anim_props_source
    assert 'TEXT(" - stopped at \'%s\'")' in anim_props_source
    assert 'TEXT(" - settable pins: %s")' in anim_props_source
    assert "if (PinNames.Num() > 0)" not in anim_props_source

    for relative_path in (
        "SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp",
        "SoftUEBridgeEditor/Private/Tools/Write/AddGraphNodeTool.cpp",
    ):
        source = _read(relative_path)

        assert "FBridgeAnimNodeProperties::DescribeUnresolvedProperty" in source
        assert source.count("FBridgeAnimNodeProperties::IsSettableGraphPin(Pin)") >= 2
        # The pin-default fallback must not recover property names by parsing warning text.
        assert "RightChop(20)" not in source
        assert 'StartsWith(TEXT("Property not found: "))' not in source

    assert "Pin->LinkedTo.Num() == 0" in anim_props_source
    assert "!Pin->bDefaultValueIsIgnored" in anim_props_source
    assert "!Pin->bOrphanedPin" in anim_props_source


def test_multi_pie_world_utility_and_tools_share_instance_discovery():
    header = _read("SoftUEBridgeEditor/Public/Tools/PIE/BridgePIEWorlds.h")
    source = _read("SoftUEBridgeEditor/Private/Tools/PIE/BridgePIEWorlds.cpp")
    spec = _read("SoftUEBridgeEditor/Private/Tools/PIE/BridgePIEWorldsSpec.cpp")
    console = _read("SoftUEBridgeEditor/Private/Tools/PIE/ExecConsoleCommandTool.cpp")
    session = _read("SoftUEBridgeEditor/Private/Tools/PIE/PieSessionTool.cpp")

    assert "Enumerate" in header and "Resolve" in header and "AvailableInstanceIds" in header
    assert "EWorldType::PIE" in source
    for mode in ("standalone", "dedicated-server", "listen-server", "client"):
        assert f'TEXT("{mode}")' in source
    assert "filters invalid PIE contexts" in spec
    assert "resolves instances and reports available IDs" in spec
    assert "returns stable net mode names" in spec
    assert "FBridgePIEWorlds::Resolve" in console
    assert "FBridgePIEWorlds::ResolveLocalPlayerController" in console
    assert "? FBridgePIEWorlds::ResolveLocalPlayerController(World, PlayerIndex)" in console
    assert "UGameplayStatics::GetPlayerController(World, PlayerIndex)" not in console
    assert "local player controller %d not found" in console
    assert "GetGameInstance" in source
    assert "GetLocalPlayers" in source
    assert "LocalPlayer->GetPlayerController(World)" in source
    assert "LocalPlayer->GetPlayerController()" not in source
    assert "resolves only valid local player controllers" in spec
    assert "pie_instance is valid only when world is 'pie'" in console
    assert 'HasField(TEXT("player_index"))' in console
    assert 'HasField(TEXT("pie_instance"))' in console
    assert 'SetNumberField(TEXT("pie_instance")' in console
    assert 'SetStringField(TEXT("world_name")' in console
    assert 'SetStringField(TEXT("net_mode")' in console
    assert "FBridgePIEWorlds::Enumerate" in session
    assert 'SetArrayField(TEXT("worlds")' in session
    assert 'SetNumberField(TEXT("world_count")' in session


def test_legacy_cloth_weight_maps_use_public_runtime_targets_and_section_provenance():
    header = _read("SoftUEBridgeEditor/Public/Utils/BridgeLegacyClothWeightMaps.h")
    source = _read("SoftUEBridgeEditor/Private/Utils/BridgeLegacyClothWeightMaps.cpp")
    spec = _read("SoftUEBridgeEditor/Private/Utils/BridgeLegacyClothWeightMapsSpec.cpp")
    cloth = _read("SoftUEBridgeEditor/Private/Tools/Cloth/ClothTools.cpp")
    build_rules = _read("SoftUEBridgeEditor/SoftUEBridgeEditor.Build.cs")

    assert "FBridgeLegacyWeightMapTarget" in header
    assert "ReadBridgeLegacyWeightMapValues" in header
    assert "ApplyBridgeLegacyWeightMapToLodData" in header
    assert "ApplyBridgeLegacySectionSelection" in header
    assert "RecordBridgeSourceSectionMembership" in header
    assert "CountBridgeSelectedMultiSectionVertices" in header
    assert "GetWeightMapTargetEnum" in source
    assert "UChaosClothingSimulationFactory" in source
    assert "ChaosWeightMapTarget.h" not in source
    assert '"ChaosCloth"' in build_rules
    assert "BridgeSourceSectionPrefix" in header
    assert "SoftUESourceSection_" in header
    assert "WriteBridgeSourceSectionMaps" in cloth
    assert "RecordBridgeSourceSectionMembership" in cloth
    assert "ReadBridgeSourceSectionSelection" in cloth
    assert "section_indices" in cloth
    assert "multi_section_vertex_count" in cloth
    assert "bSourceSectionMap ? FMath::Max" in cloth
    assert "resolves common and extended targets" in spec
    assert "preserves welded dual section membership" in spec
    assert "restricts constant, vertex-color, and bone-distance candidates" in spec
    assert "intersects spatial and section selection" in spec
    assert "rejects an empty combined selection" in spec
    assert "records dual membership through the merge helper" in spec
    assert "round-trips extended target values through read and apply helpers" in spec
