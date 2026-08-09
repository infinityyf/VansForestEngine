#pragma once

#include "VansEditorObjectReference.h"
#include "../TimelineCore/VansTimelineTypes.h"

#include <string>
#include <vector>

namespace Vans
{
struct LuaScriptFieldDescriptor;

enum class EditorPropertyKind
{
    Value,
    ObjectReference
};

enum class EditorPropertyDescriptorSource
{
    None,
    Declared
};

struct EditorPropertyDescriptor
{
    EditorPropertyKind kind = EditorPropertyKind::Value;
    EditorPropertyDescriptorSource source = EditorPropertyDescriptorSource::None;
    ObjectReferenceSlotDescriptor objectReferenceSlot;

    bool IsObjectReference() const { return kind == EditorPropertyKind::ObjectReference; }
    bool IsDeclared() const { return source == EditorPropertyDescriptorSource::Declared; }
};

struct EditorAnimatablePropertyDescriptor
{
	std::string stableId;
	std::string displayName;
	std::uint16_t componentTypeId = 0;
	VansTimelineChannelType valueType = VansTimelineChannelType::Float;
	std::string unit;
	double minimum = 0.0;
	double maximum = 1.0;
	double step = 0.01;
};

class VansEditorPropertyDescriptorRegistry
{
public:
    static EditorPropertyDescriptor Resolve(
        const std::string& componentType,
        const std::string& parentKey,
        const std::string& fieldKey);

    static ObjectReferenceSlotDescriptor ProjectAssetReferenceSlot(
        EditorAPI::AssetType expectedType,
        ObjectReferenceStoragePolicy storagePolicy = ObjectReferenceStoragePolicy::GuidObject);

    static bool TryResolveLuaScriptFieldObjectReferenceSlot(
        const LuaScriptFieldDescriptor& descriptor,
        ObjectReferenceSlotDescriptor& slot);

	static const EditorAnimatablePropertyDescriptor* FindAnimatable(const std::string& stableId);
	static std::vector<EditorAnimatablePropertyDescriptor> AnimatableForComponent(std::uint16_t componentTypeId);
	static const std::vector<EditorAnimatablePropertyDescriptor>& AllAnimatable();
};
}
