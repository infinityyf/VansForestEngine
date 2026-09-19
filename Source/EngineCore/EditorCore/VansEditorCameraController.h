#pragma once
#include "../EngineAPILayer/Public/EngineDTOs.h"
#include <glm/glm.hpp>

namespace VansGraphics
{
    class VansCamera;

    struct VansEditorCameraInputState
    {
        bool editMode = false;
        bool viewportHovered = false;
        bool rightMouseClicked = false;
        bool rightMouseDown = false;
        bool cancelFraming = false;
        float mouseDeltaX = 0.0f;
        float mouseDeltaY = 0.0f;
        float forwardAxis = 0.0f;
        float rightAxis = 0.0f;
        float upAxis = 0.0f;
        float deltaTime = 0.0f;
    };

    class VansEditorCameraController
    {
    public:
        void Update(VansCamera* camera, const VansEditorCameraInputState& input);
        void Reset(VansCamera* camera);
        bool Frame(VansCamera* camera, const Vans::EditorAPI::EditorSceneBounds& bounds, float aspect);
        bool IsNavigating() const { return m_IsNavigating; }

    private:
        bool m_IsNavigating = false;
        bool m_IsFraming = false;
        float m_FrameElapsed = 0.0f;
        glm::vec3 m_FrameStart{0}, m_FrameTarget{0};
    };
}
