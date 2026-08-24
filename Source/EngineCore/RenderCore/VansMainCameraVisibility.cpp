#include "VansMainCameraVisibility.h"

#include "VansRenderNode.h"
#include "VansScene.h"
#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"

bool VansGraphics::TryGetStaticNodeWorldBounds(VansRenderNode* node, VansRenderBounds& bounds)
{
	if (node == nullptr || node->m_Mesh == nullptr || node->m_HasSkeletonBone ||
		!node->m_Mesh->HasLocalOBB())
	{
		return false;
	}

	if (!node->HasWorldBounds())
		node->UpdateWorldBoundsFromTransform();
	if (!node->HasWorldBounds())
		return false;
	bounds = node->GetWorldBounds();
	return bounds.IsValid();
}

bool VansGraphics::IsNodeVisibleInFrustum(VansRenderNode* node, const glm::mat4& worldToClip)
{
	VansRenderBounds bounds;
	return !TryGetStaticNodeWorldBounds(node, bounds) ||
		RenderBoundsIntersectsClipFrustum(bounds, worldToClip);
}

void VansGraphics::VansScene::SetMainCameraHiZCullSettings(
	const VansMainCameraHiZCullSettings& settings)
{
	m_MainCameraHiZCullSettings = settings;
}

bool VansGraphics::VansScene::ShouldDrawMainCameraNode(VansRenderNode* node)
{
	if (node == nullptr)
		return true;
	const auto binding = m_MainRenderProxyBindings.find(node);
	if (binding == m_MainRenderProxyBindings.end())
		return true;
	auto* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	return vkDevice == nullptr || vkDevice->ShouldDrawMainCameraProxy(binding->second.handle);
}

bool VansGraphics::VansScene::IsRenderNodeEnabledForCurrentFrame(
	const VansRenderNode* node) const
{
	if (node == nullptr)
		return false;
	const auto binding = m_MainRenderProxyBindings.find(node);
	if (binding == m_MainRenderProxyBindings.end())
		return node->IsEnabled();
	const auto* vkDevice = dynamic_cast<const VansVKDevice*>(m_GraphicsDevice);
	return vkDevice == nullptr ||
		vkDevice->IsCurrentRenderProxyEnabled(binding->second.handle);
}

const VansGraphics::VansRenderTransformFrameData*
VansGraphics::VansScene::FindRenderNodeTransformForCurrentFrame(
	const VansRenderNode* node) const
{
	if (node == nullptr)
		return nullptr;
	const auto binding = m_MainRenderProxyBindings.find(node);
	if (binding == m_MainRenderProxyBindings.end())
		return nullptr;
	const auto* vkDevice = dynamic_cast<const VansVKDevice*>(m_GraphicsDevice);
	return vkDevice != nullptr
		? vkDevice->FindCurrentRenderTransform(binding->second.handle)
		: nullptr;
}
