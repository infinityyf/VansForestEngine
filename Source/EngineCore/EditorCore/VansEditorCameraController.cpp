#include "VansEditorCameraController.h"

#include "../RenderCore/VansCamera.h"
#include "../Util/VansSceneViewMath.h"
#include <algorithm>

bool VansGraphics::VansEditorCameraController::Frame(
    VansCamera* camera, const Vans::EditorAPI::EditorSceneBounds& bounds, float aspect)
{
    if (!camera) return false;
    float farClip = 0;
    if (!Vans::VansSceneViewMath::CalculateFramePosition(
        bounds.available,
        glm::vec3(bounds.minimum.x, bounds.minimum.y, bounds.minimum.z),
        glm::vec3(bounds.maximum.x, bounds.maximum.y, bounds.maximum.z),
        glm::vec3(camera->GetForward()), camera->GetFov(), aspect, camera->GetNearClip(),
        m_FrameTarget, farClip)) return false;
    m_FrameStart = glm::vec3(camera->GetPosition());
    camera->SetFarClip(std::max(camera->GetFarClip(), farClip));
    m_FrameElapsed = 0;
    m_IsFraming = true;
    return true;
}

void VansGraphics::VansEditorCameraController::Update(
    VansCamera* camera,
    const VansEditorCameraInputState& input)
{
	// SceneWindow composes after the current frame's camera Resolve and before the
	// next frame's CaptureBase.  These edits intentionally become the next base
	// view; VansCameraControlArbiter asserts the Begin/Capture/Resolve lifecycle.
    if (!camera || !input.editMode)
    {
        Reset(camera);
        return;
    }

    if (input.viewportHovered && input.rightMouseClicked)
        m_IsNavigating = true;

    if (!input.rightMouseDown)
        m_IsNavigating = false;

    if (m_IsNavigating || input.cancelFraming) m_IsFraming = false;
    if (m_IsFraming)
    {
        m_FrameElapsed += std::max(input.deltaTime, 0.0f);
        const float t = std::min(m_FrameElapsed / .2f, 1.0f);
        auto view = camera->CaptureView();
        view.pose.position = glm::mix(m_FrameStart, m_FrameTarget, t * t * (3 - 2 * t));
        camera->ApplyView(view);
        if (t >= 1) m_IsFraming = false;
    }

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
    m_IsFraming = false;
    if (camera)
        camera->SetRightMouseDown(false);
}
