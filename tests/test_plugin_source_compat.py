from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLUGIN_SOURCE = ROOT / "plugin" / "SoftUEBridge" / "Source"


def _read(relative_path: str) -> str:
    return (PLUGIN_SOURCE / relative_path).read_text(encoding="utf-8")


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


def test_rewind_helper_avoids_removed_trace_file_loaded_check():
    source = _read("SoftUEBridgeEditor/Private/Tools/Rewind/RewindHelper.cpp")

    assert "IsTraceFileLoaded()" not in source
    assert "Debugger->IsRecording()" in source


def test_set_node_property_supports_nested_inner_anim_node_paths_and_struct_arrays():
    set_node_source = _read("SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp")
    modifier_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAssetModifier.cpp")
    serializer_source = _read("SoftUEBridgeEditor/Private/Utils/BridgePropertySerializer.cpp")

    assert "ResolvePropertyPathAgainstStruct" in set_node_source
    assert "TryResolveInnerAnimNodePropertyPath" in set_node_source
    assert "Node." in set_node_source
    assert "FArrayProperty* ArrayProp = CastField<FArrayProperty>" in modifier_source
    assert "DeserializeArrayProperty" in serializer_source
    assert "FBridgePropertySerializer::DeserializePropertyValue(ArrayProp->Inner, ElementPtr, ElementValue" in serializer_source
