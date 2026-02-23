#include "VansTransform.h"

// Static member definitions
std::vector<VansGraphics::VansTransform> VansGraphics::VansTransformStore::GlobalTransforms;
std::queue<uint32_t> VansGraphics::VansTransformStore::FreeTransformIndices;
std::map<uint32_t, bool> VansGraphics::VansTransformStore::TransformIDToTransformDirty;

glm::mat4x4 VansGraphics::VansTransform::GetModelMatrix()
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, m_Position);
    model = glm::rotate(model, glm::radians(m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, m_Scale);
    return model;
}