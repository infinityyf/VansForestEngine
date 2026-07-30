#include "VansEditorCameraController.h"

#include "../RenderCore/VansCamera.h"

void VansGraphics::VansEditorCameraController::Update(
    VansCamera* camera,
    const VansEditorCameraInputState& input)
{
    if (!camera || !input.editMode)
    {
        Reset(camera);
        return;
    }

    if (input.viewportHovered && input.rightMouseClicked)
        m_IsNavigating = true;

    if (!input.rightMouseDown)
        m_IsNavigating = false;

    camera->SetRightMouseDown(m_IsNavigating);
    if (!m_IsNavigating)
        return;

    if (input.mouseDeltaX != 0.0f || input.mouseDeltaY != 0.0f)
        camera->HandleMouseMovement(input.mouseDeltaX, input.mouseDeltaY);

    if (input.forwardAxis != 0.0f || input.rightAxis != 0.0f || input.upAxis != 0.0f)
        camera->HandleKeyboardMovement(
            input.forwardAxis,
            input.rightAxis,
            input.upAxis,
            input.deltaTime);
}

void VansGraphics::VansEditorCameraController::Reset(VansCamera* camera)
{
    m_IsNavigating = false;
    if (camera)
        camera->SetRightMouseDown(false);
}
