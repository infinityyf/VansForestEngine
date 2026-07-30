#pragma once

namespace VansGraphics
{
    class VansCamera;

    struct VansEditorCameraInputState
    {
        bool editMode = false;
        bool viewportHovered = false;
        bool rightMouseClicked = false;
        bool rightMouseDown = false;
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
        bool IsNavigating() const { return m_IsNavigating; }

    private:
        bool m_IsNavigating = false;
    };
}
