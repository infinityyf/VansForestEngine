#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"
#include "../SceneCore/VansSceneDocument.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vans
{
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

class VansSceneEditCommand
{
public:
    virtual ~VansSceneEditCommand() = default;
    virtual SceneEditResult Execute(VansSceneDocument& document) = 0;
    virtual SceneEditResult Undo(VansSceneDocument& document) = 0;
    virtual SceneEditResult Redo(VansSceneDocument& document) = 0;
};

class VansSetScenePropertyCommand final : public VansSceneEditCommand
{
public:
    VansSetScenePropertyCommand(std::string jsonPointer, SceneJson value);
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

private:
    std::string m_JsonPointer;
    SceneJson m_NewValue;
    SceneJson m_OldValue;
    bool m_HadOldValue = false;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

class VansRemoveScenePropertyCommand final : public VansSceneEditCommand
{
public:
    explicit VansRemoveScenePropertyCommand(std::string jsonPointer);
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

private:
    std::string m_JsonPointer;
    SceneJson m_OldValue;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

class VansAssignAssetReferenceCommand final : public VansSceneEditCommand
{
public:
    VansAssignAssetReferenceCommand(std::string jsonPointer,
        std::string assetGuid,
        EditorAPI::AssetType expectedType,
        bool writeObjectReference);
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

private:
    SceneJson BuildReferenceValue() const;

    std::string m_JsonPointer;
    std::string m_AssetGuid;
    EditorAPI::AssetType m_ExpectedType = EditorAPI::AssetType::Unknown;
    bool m_WriteObjectReference = true;
    SceneJson m_OldValue;
    bool m_HadOldValue = false;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

class VansAppendSceneEntitiesCommand final : public VansSceneEditCommand
{
public:
    explicit VansAppendSceneEntitiesCommand(std::vector<SceneJson> entities,
        SceneEditLifecycleHooks hooks = {});
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

private:
    std::vector<SceneJson> m_Entities;
    SceneEditLifecycleHooks m_Hooks;
    std::size_t m_InsertIndex = 0;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

class VansSceneEditService
{
public:
    explicit VansSceneEditService(VansSceneDocument& document) : m_Document(document) {}

    SceneEditResult Execute(std::unique_ptr<VansSceneEditCommand> command);
    SceneEditResult Set(const std::string& jsonPointer, SceneJson value);
    SceneEditResult AssignAssetReference(const std::string& jsonPointer,
        std::string assetGuid,
        EditorAPI::AssetType expectedType,
        bool writeObjectReference);
    SceneEditResult AppendEntities(std::vector<SceneJson> entities,
        SceneEditLifecycleHooks hooks = {});
    SceneEditResult Remove(const std::string& jsonPointer);
    SceneEditResult Undo();
    SceneEditResult Redo();
    void ClearHistory();
    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }

private:
    VansSceneDocument& m_Document;
    std::vector<std::unique_ptr<VansSceneEditCommand>> m_Undo;
    std::vector<std::unique_ptr<VansSceneEditCommand>> m_Redo;
};
}
