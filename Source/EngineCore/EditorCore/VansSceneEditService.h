#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"
#include "../SceneCore/VansSceneParentReference.h"
#include "VansEditorObjectReference.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
class VansSceneDocument;
class VansSceneEditCommand;
using SceneStateId = std::uint64_t;

enum class ReparentTransformPolicy : std::uint8_t
{
    KeepWorld,
    KeepLocal,
    Snap
};

struct SceneEditResult
{
    bool success = false;
    std::string message;
    std::string changedEntityGuid;
    EditorAPI::RuntimeParentReference changedParent;
    bool runtimePreviewSupported = false;
    bool runtimeParentPreviewSupported = false;
    bool runtimeChangeApplied = false;

    explicit operator bool() const { return success; }
};

struct SceneEditLifecycleHooks
{
    std::function<bool()> afterExecute;
    std::function<bool()> afterUndo;
    std::function<bool()> afterRedo;
};

class VansSceneEditService
{
public:
    explicit VansSceneEditService(VansSceneDocument& document);
    ~VansSceneEditService();

    SceneEditResult Set(const DocumentPropertyPath& path, VansSerializedValue value);
    SceneEditResult SetAndAssignObjectReference(
        const DocumentPropertyPath& path,
        VansSerializedValue value,
        const ObjectReferenceAssignment& assignment);
    SceneEditResult AssignObjectReference(const ObjectReferenceAssignment& assignment);
    SceneEditResult ReparentEntity(
        const std::string& childEntityGuid,
        std::optional<VansSceneParentReference> newParent,
        ReparentTransformPolicy transformPolicy,
        std::optional<EditorAPI::RuntimeTransformSnapshot> resolvedLocalTransform = std::nullopt);
    SceneEditResult SetEntityTransform(
        const std::string& entityGuid,
        const EditorAPI::RuntimeTransformSnapshot& transform);
    SceneEditResult AppendEntities(std::vector<VansSerializedValue> entities,
        SceneEditLifecycleHooks hooks = {});
    SceneEditResult Remove(const DocumentPropertyPath& path, SceneEditLifecycleHooks hooks = {});
    SceneEditResult Undo();
    SceneEditResult Redo();
    void ClearHistory();
    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }

private:
    SceneEditResult Execute(std::unique_ptr<VansSceneEditCommand> command);
    SceneEditResult Set(const std::string& propertyPointer, VansSerializedValue value);
    SceneEditResult Remove(const std::string& propertyPointer, SceneEditLifecycleHooks hooks = {});

    VansSceneDocument& m_Document;
    std::vector<std::unique_ptr<VansSceneEditCommand>> m_Undo;
    std::vector<std::unique_ptr<VansSceneEditCommand>> m_Redo;
};
}
