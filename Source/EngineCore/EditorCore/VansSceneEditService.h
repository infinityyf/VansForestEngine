#pragma once

#include "../SceneCore/VansSceneDocument.h"

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

class VansSceneEditService
{
public:
    explicit VansSceneEditService(VansSceneDocument& document) : m_Document(document) {}

    SceneEditResult Execute(std::unique_ptr<VansSceneEditCommand> command);
    SceneEditResult Set(const std::string& jsonPointer, SceneJson value);
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
