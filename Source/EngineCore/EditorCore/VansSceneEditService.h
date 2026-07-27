#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"
#include "VansEditorObjectReference.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vans
{
class VansSceneDocument;
class VansSceneEditCommand;
using SceneStateId = std::uint64_t;

struct SceneEditResult
{
    bool success = false;
    std::string message;

    explicit operator bool() const { return success; }
};

struct SceneEditLifecycleHooks
{
    std::function<void()> afterUndo;
    std::function<void()> afterRedo;
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
    SceneEditResult AppendEntities(std::vector<VansSerializedValue> entities,
        SceneEditLifecycleHooks hooks = {});
    SceneEditResult Remove(const DocumentPropertyPath& path);
    SceneEditResult Undo();
    SceneEditResult Redo();
    void ClearHistory();
    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }

private:
    SceneEditResult Execute(std::unique_ptr<VansSceneEditCommand> command);
    SceneEditResult Set(const std::string& propertyPointer, VansSerializedValue value);
    SceneEditResult Remove(const std::string& propertyPointer);

    VansSceneDocument& m_Document;
    std::vector<std::unique_ptr<VansSceneEditCommand>> m_Undo;
    std::vector<std::unique_ptr<VansSceneEditCommand>> m_Redo;
};
}
