#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AuthoringCore/VansAuthoringHistory.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"
#include "../SceneCore/VansSceneParentReference.h"
#include "../AuthoringCore/VansEditorObjectReference.h"

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
struct VansOpenAssetDocument;
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

    SceneEditResult ApplyPrefab(std::shared_ptr<VansOpenAssetDocument> asset, VansSerializedValue source, VansSerializedValue authoring);
    SceneEditResult ReplaceRoot(VansSerializedValue root, SceneEditLifecycleHooks hooks = {});
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
    void SetPrefabPreviewRefresh(std::function<bool()> callback) { m_PrefabPreviewRefresh = std::move(callback); }
    bool CanUndo() const;
    bool CanRedo() const;
	VansAuthoringHistorySnapshot HistorySnapshot() const;

private:
	struct HistoryEntry
	{
		std::unique_ptr<VansSceneEditCommand> command;
		VansHistorySequence sequence = 0;
	};
	void DiscardStaleRedo() const;
    SceneEditResult Execute(std::unique_ptr<VansSceneEditCommand> command);
    SceneEditResult Set(const std::string& propertyPointer, VansSerializedValue value);
    SceneEditResult Remove(const std::string& propertyPointer, SceneEditLifecycleHooks hooks = {});

    VansSceneDocument& m_Document;
    std::function<bool()> m_PrefabPreviewRefresh;
    std::vector<HistoryEntry> m_Undo;
    mutable std::vector<HistoryEntry> m_Redo;
	mutable VansHistorySequence m_RedoRevision = 0;
};
}
