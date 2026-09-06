#include "VansAnimationNode.h"
#include "MotionMatching/VansMotionMatching.h"
#include "../RenderCore/VansRenderNode.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../RuntimeCore/VansCharacterMotion.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

using namespace VansGraphics;

namespace
{
	bool DecomposeMatrixToPRS(const glm::mat4& matrix,
	                          glm::vec3& outPosition,
	                          glm::vec3& outRotationDegrees,
	                          glm::vec3& outScale)
	{
		glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 skew;
		glm::vec4 perspective;
		if (!glm::decompose(matrix, outScale, rotation, outPosition, skew, perspective))
			return false;

		rotation = glm::normalize(rotation);
		outRotationDegrees = glm::degrees(glm::eulerAngles(rotation));
		return true;
	}

	int FindPoseAuditBone(const Skeleton& skeleton, const char* name)
	{
		auto it = skeleton.boneNameToIndex.find(name);
		return it != skeleton.boneNameToIndex.end() ? it->second : -1;
	}

	glm::vec3 ExtractPoseAuditTranslation(const glm::mat4& transform)
	{
		return glm::vec3(transform[3]);
	}

	bool IsFinitePoseAuditVec3(const glm::vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	const char* PoseAuditAxisName(int axis)
	{
		switch (axis)
		{
		case 0: return "X";
		case 1: return "Y";
		default: return "Z";
		}
	}

	int InferPoseAuditUpAxis(const glm::vec3& headMinusPelvis)
	{
		const glm::vec3 absDelta(
			std::abs(headMinusPelvis.x),
			std::abs(headMinusPelvis.y),
			std::abs(headMinusPelvis.z));
		if (absDelta.x >= absDelta.y && absDelta.x >= absDelta.z)
			return 0;
		if (absDelta.y >= absDelta.z)
			return 1;
		return 2;
	}

	float AxisComponent(const glm::vec3& value, int axis)
	{
		if (axis == 0)
			return value.x;
		if (axis == 1)
			return value.y;
		return value.z;
	}

	const char* RetargetTranslationScaleModeName(VansRetargetTranslationScaleMode mode)
	{
		switch (mode)
		{
		case VansRetargetTranslationScaleMode::AutoPelvis: return "autoPelvis";
		case VansRetargetTranslationScaleMode::CompatibleSkeleton: return "compatibleSkeleton";
		case VansRetargetTranslationScaleMode::Explicit: return "explicit";
		}
		return "invalid";
	}

	const char* RetargetRootAlignmentName(VansRetargetRootAlignment alignment)
	{
		return alignment == VansRetargetRootAlignment::FeetToOwner ? "feetToOwner" : "none";
	}

	const char* RetargetModelAlignmentName(VansRetargetModelSpaceAlignment alignment)
	{
		return alignment == VansRetargetModelSpaceAlignment::SourceBindPose
			? "sourceBindPose" : "none";
	}

	void LogPoseAuditBone(
		const char* label,
		const Skeleton& skeleton,
		const std::vector<glm::mat4>& modelTransforms,
		const glm::mat4* ownerWorld = nullptr)
	{
		const int index = FindPoseAuditBone(skeleton, label);
		if (index < 0 || index >= static_cast<int>(modelTransforms.size()))
		{
			VANS_LOG("[RetargetAudit] bone '" << label << "' missing");
			return;
		}

		const glm::vec3 position = ExtractPoseAuditTranslation(modelTransforms[index]);
		if (ownerWorld)
		{
			const glm::vec3 worldPosition = glm::vec3((*ownerWorld) * glm::vec4(position, 1.0f));
			VANS_LOG("[RetargetAudit] bone '" << label << "' modelPos=("
				<< position.x << ", " << position.y << ", " << position.z << ")"
				<< " worldPos=(" << worldPosition.x << ", " << worldPosition.y << ", "
				<< worldPosition.z << ")"
				<< " finite=" << (IsFinitePoseAuditVec3(position) && IsFinitePoseAuditVec3(worldPosition)));
			return;
		}

		VANS_LOG("[RetargetAudit] bone '" << label << "' modelPos=("
			<< position.x << ", " << position.y << ", " << position.z << ")"
			<< " finite=" << IsFinitePoseAuditVec3(position));
	}

	void LogRetargetPoseAudit(
		const std::string& nodeName,
		const Skeleton& targetSkeleton,
		const std::vector<glm::mat4>& targetModelTransforms,
		const glm::mat4* ownerWorld = nullptr,
		int renderNodeCount = -1,
		int enabledRenderNodeCount = -1)
	{
		if (targetModelTransforms.size() != targetSkeleton.bones.size())
		{
			VANS_LOG_WARN("[RetargetAudit] " << nodeName
				<< ": target pose size mismatch transforms=" << targetModelTransforms.size()
				<< " bones=" << targetSkeleton.bones.size());
			return;
		}

		const int pelvis = FindPoseAuditBone(targetSkeleton, "pelvis");
		const int head = FindPoseAuditBone(targetSkeleton, "head");
		const int footL = FindPoseAuditBone(targetSkeleton, "foot_l");
		const int footR = FindPoseAuditBone(targetSkeleton, "foot_r");
		const int handL = FindPoseAuditBone(targetSkeleton, "hand_l");
		const int handR = FindPoseAuditBone(targetSkeleton, "hand_r");

		VANS_LOG("[RetargetAudit] " << nodeName
			<< ": target pose audit begin, bones=" << targetSkeleton.bones.size()
			<< " renderNodes=" << renderNodeCount
			<< " enabledRenderNodes=" << enabledRenderNodeCount);
		LogPoseAuditBone("Armature", targetSkeleton, targetModelTransforms, ownerWorld);
		LogPoseAuditBone("root", targetSkeleton, targetModelTransforms, ownerWorld);
		LogPoseAuditBone("pelvis", targetSkeleton, targetModelTransforms, ownerWorld);
		LogPoseAuditBone("head", targetSkeleton, targetModelTransforms, ownerWorld);
		LogPoseAuditBone("hand_l", targetSkeleton, targetModelTransforms, ownerWorld);
		LogPoseAuditBone("hand_r", targetSkeleton, targetModelTransforms, ownerWorld);
		LogPoseAuditBone("foot_l", targetSkeleton, targetModelTransforms, ownerWorld);
		LogPoseAuditBone("foot_r", targetSkeleton, targetModelTransforms, ownerWorld);

		if (pelvis >= 0 && head >= 0 &&
		    footL >= 0 && footR >= 0 &&
		    handL >= 0 && handR >= 0)
		{
			const glm::vec3 pelvisPos = ExtractPoseAuditTranslation(targetModelTransforms[pelvis]);
			const glm::vec3 headPos = ExtractPoseAuditTranslation(targetModelTransforms[head]);
			const glm::vec3 footLPos = ExtractPoseAuditTranslation(targetModelTransforms[footL]);
			const glm::vec3 footRPos = ExtractPoseAuditTranslation(targetModelTransforms[footR]);
			const glm::vec3 handLPos = ExtractPoseAuditTranslation(targetModelTransforms[handL]);
			const glm::vec3 handRPos = ExtractPoseAuditTranslation(targetModelTransforms[handR]);

			const glm::vec3 headMinusPelvis = headPos - pelvisPos;
			const int inferredUpAxis = InferPoseAuditUpAxis(headMinusPelvis);
			const float headPelvisInferredUp = AxisComponent(headMinusPelvis, inferredUpAxis);
			const float feetSeparation = glm::length(footLPos - footRPos);
			const float handsSeparation = glm::length(handLPos - handRPos);
			const float leftLegLength = glm::length(footLPos - pelvisPos);
			const float rightLegLength = glm::length(footRPos - pelvisPos);
			const bool finite =
				IsFinitePoseAuditVec3(pelvisPos) &&
				IsFinitePoseAuditVec3(headPos) &&
				IsFinitePoseAuditVec3(footLPos) &&
				IsFinitePoseAuditVec3(footRPos) &&
				IsFinitePoseAuditVec3(handLPos) &&
				IsFinitePoseAuditVec3(handRPos);

			VANS_LOG("[RetargetAudit] " << nodeName
				<< ": metrics finite=" << finite
				<< " inferredUpAxis=" << PoseAuditAxisName(inferredUpAxis)
				<< " headMinusPelvisInferredUp=" << headPelvisInferredUp
				<< " headMinusPelvis=(" << headMinusPelvis.x << ", "
					<< headMinusPelvis.y << ", " << headMinusPelvis.z << ")"
				<< " feetSeparation=" << feetSeparation
				<< " handsSeparation=" << handsSeparation
				<< " leftLegPelvisDistance=" << leftLegLength
				<< " rightLegPelvisDistance=" << rightLegLength);
		}
	}

}
// Construction / destruction
VansAnimationNode::VansAnimationNode(const std::string& name)
	: m_Name(name)
{
	std::memset(&m_BoneMatricesSSBO, 0, sizeof(BoneMatricesSSBO));
	for (uint32_t i = 0; i < MAX_BONES; i++)
		m_BoneMatricesSSBO.boneMatrices[i] = glm::mat4(1.0f);
}

VansAnimationNode::~VansAnimationNode()
{
	DestroyGPUResources();
}

bool VansAnimationNode::ConfigureRetargetSource(
	const Skeleton& sourceSkeleton,
	std::unique_ptr<VansAnimationController> sourceController,
	const VansRetargetRuntimeDesc& desc,
	std::string& error)
{
	error.clear();
	m_RetargetEnabled = false;
	m_SourceSkeleton = sourceSkeleton;
	m_SourceController = std::move(sourceController);
	m_RetargetDesc = desc;
	m_LastRetargetSourceMMSwitchCount = -1;
	m_LastRetargetSourceMMActiveClip.clear();
	m_LastRetargetSourceMMSelectedClip.clear();

	if (!m_SourceController)
	{
		error = "missing source controller";
		VANS_LOG_WARN("[Retarget] " << m_Name << ": " << error);
		return false;
	}
	if (!m_SourceController->BindAnimationRigSkeleton(m_SourceSkeleton, error))
	{
		error = "source Animation Rig bind failed: " + error;
		VANS_LOG_WARN("[Retarget] " << m_Name << ": " << error);
		m_SourceController.reset();
		return false;
	}

	const VansCompiledAnimationRig* targetRig = m_Controller ? m_Controller->GetAnimationRig() : nullptr;
	if (!targetRig || !m_RetargetProcessor.Build(
		m_SourceSkeleton, m_Skeleton, *targetRig, m_RetargetDesc))
	{
		error = targetRig
			? "failed to build Source -> Target map"
			: "target controller has no compiled Animation Rig";
		VANS_LOG_WARN("[Retarget] " << m_Name << ": " << error);
		m_SourceController.reset();
		return false;
	}

	const VansRetargetBuildStats& stats = m_RetargetProcessor.GetStats();
	m_RetargetEnabled = true;
	VANS_LOG("[Retarget] " << m_Name
		<< ": sourceBones=" << stats.sourceBoneCount
		<< " targetBones=" << stats.targetBoneCount
		<< " mapped=" << stats.mappedBoneCount
		<< " unmappedTarget=" << stats.unmappedTargetBoneCount
		<< " limbChains=" << stats.limbChainCount
		<< " translationScale=" << stats.translationScale
		<< " translationScaleMode='" << RetargetTranslationScaleModeName(desc.translationScaleMode) << "'"
		<< " rootAlignment='" << RetargetRootAlignmentName(desc.rootAlignment) << "'"
		<< " targetModelSpaceAlignment='" << RetargetModelAlignmentName(desc.targetModelSpaceAlignment) << "'"
		<< " sourceModel='" << desc.sourceModelAssetGuid
		<< "' sourceAnimator='" << desc.sourceAnimatorAssetGuid << "'");
	return true;
}

bool VansAnimationNode::ReplaceRetargetSourceController(
	std::unique_ptr<VansAnimationController> controller)
{
	std::unique_ptr<VansAnimationController> previous;
	return ExchangeRetargetSourceController(std::move(controller), previous);
}

bool VansAnimationNode::ExchangeRetargetSourceController(
	std::unique_ptr<VansAnimationController> controller,
	std::unique_ptr<VansAnimationController>& previousController)
{
	if (!m_RetargetEnabled || !controller)
		return false;
	std::string error;
	if (!controller->BindAnimationRigSkeleton(m_SourceSkeleton, error))
	{
		VANS_LOG_WARN("[Retarget] " << m_Name
			<< ": replacement source Rig bind rejected: " << error);
		return false;
	}
	previousController = std::move(m_SourceController);
	m_SourceController = std::move(controller);
	m_LastRetargetSourceMMSwitchCount = -1;
	m_LastRetargetSourceMMActiveClip.clear();
	m_LastRetargetSourceMMSelectedClip.clear();
	return true;
}

// ════════════════════════════════════════════════════════════════
//  关联 RenderNode
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetRenderNode(VansRenderNode* renderNode)
{
	m_RenderNodes = { renderNode };
	RebuildNodeTransformBindings();
}

void VansAnimationNode::SetRenderNodes(const std::vector<VansRenderNode*>& nodes)
{
	m_RenderNodes = nodes;
	RebuildNodeTransformBindings();
}

// ════════════════════════════════════════════════════════════════
//  骨骼
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetSkeleton(const Skeleton& skeleton)
{
	m_Skeleton = skeleton;
	VANS_LOG("[VansAnimationNode] " << m_Name << ": skeleton set with "
	         << m_Skeleton.bones.size() << " bones");
}

// ════════════════════════════════════════════════════════════════
//  Controller 绑定
// ════════════════════════════════════════════════════════════════

bool VansAnimationNode::SetController(VansAnimationController* controller)
{
	if (controller)
	{
		std::string error;
		if (!controller->BindAnimationRigSkeleton(m_Skeleton, error))
		{
			VANS_LOG_WARN("[VansAnimationNode] " << m_Name
				<< ": controller Rig bind rejected: " << error);
			return false;
		}
	}
	m_Controller = controller;

	if (m_Controller)
	{
		// 将 Node 侧的骨骼覆盖映射关联到 Controller，以便在 Update 管线中应用
		m_Controller->SetBoneOverrides(&m_BoneOverrides);

		VANS_LOG("[VansAnimationNode] " << m_Name << ": controller '" 
		         << m_Controller->GetName() << "' bound");
	}
	RebuildNodeTransformBindings();
	return true;
}

// ════════════════════════════════════════════════════════════════
//  播放控制（委托给 Controller）
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::Play(VansAnimationEvaluationPurpose purpose)
{
	if (m_Controller)
	{
		m_Controller->Play();
	}
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Play();
	if (m_Controller)
		Update({ purpose, 0.0f });
}

void VansAnimationNode::Play(
	const std::string& stateName, VansAnimationEvaluationPurpose purpose)
{
	if (m_Controller)
	{
		m_Controller->Play(stateName);
	}
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Play(stateName);
	if (m_Controller)
		Update({ purpose, 0.0f });
}

void VansAnimationNode::Pause()
{
	if (m_Controller)
		m_Controller->Pause();
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Pause();
}

void VansAnimationNode::Resume()
{
	if (m_Controller)
		m_Controller->Resume();
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Resume();
}

void VansAnimationNode::Stop()
{
	if (m_Controller)
		m_Controller->Stop();
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Stop();
}

VansGraphSetSwitchResult VansAnimationNode::SwitchGraphSet(const std::string& graphSetId)
{
	VansAnimationController* controller = GetCharacterMotionController();
	return controller ? controller->SwitchGraphSet(graphSetId)
		: VansGraphSetSwitchResult::UnknownGraphSet;
}

const std::string& VansAnimationNode::GetActiveGraphSetId() const
{
	const VansAnimationController* controller = m_RetargetEnabled && m_SourceController
		? m_SourceController.get() : m_Controller;
	static const std::string empty;
	return controller ? controller->GetActiveGraphSetId() : empty;
}

const std::string& VansAnimationNode::GetIncomingGraphSetId() const
{
	const VansAnimationController* controller = m_RetargetEnabled && m_SourceController
		? m_SourceController.get() : m_Controller;
	static const std::string empty;
	return controller ? controller->GetIncomingGraphSetId() : empty;
}

bool VansAnimationNode::IsGraphSetTransitioning() const
{
	const VansAnimationController* controller = m_RetargetEnabled && m_SourceController
		? m_SourceController.get() : m_Controller;
	return controller && controller->IsGraphSetTransitioning();
}

float VansAnimationNode::GetGraphSetTransitionProgress() const
{
	const VansAnimationController* controller = m_RetargetEnabled && m_SourceController
		? m_SourceController.get() : m_Controller;
	return controller ? controller->GetGraphSetTransitionProgress() : 0.0f;
}

// ════════════════════════════════════════════════════════════════
//  状态查询（委托给 Controller）
// ════════════════════════════════════════════════════════════════

AnimationState VansAnimationNode::GetState() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetPlaybackState();
	if (m_Controller)
		return m_Controller->GetPlaybackState();
	return AnimationState::Stopped;
}

float VansAnimationNode::GetCurrentPlayTime() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetCurrentPlayTime();
	if (m_Controller)
		return m_Controller->GetCurrentPlayTime();
	return 0.0f;
}

float VansAnimationNode::GetDuration() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetCurrentDuration();
	if (m_Controller)
		return m_Controller->GetCurrentDuration();
	return 0.0f;
}

float VansAnimationNode::GetNormalizedTime() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetNormalizedTime();
	if (m_Controller)
		return m_Controller->GetNormalizedTime();
	return 0.0f;
}

std::string VansAnimationNode::GetCurrentStateName() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetCurrentStateName();
	if (m_Controller)
		return m_Controller->GetCurrentStateName();
	return "";
}

float VansAnimationNode::GetSpeed() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetSpeed();
	if (m_Controller)
		return m_Controller->GetSpeed();
	return 1.0f;
}

// ════════════════════════════════════════════════════════════════
//  Root Motion
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::EnableRootMotion(bool enable)
{
	if (m_Controller)
		m_Controller->EnableRootMotion(enable);
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->EnableRootMotion(enable);
}

bool VansAnimationNode::IsRootMotionEnabled() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->IsRootMotionEnabled();
	if (m_Controller)
		return m_Controller->IsRootMotionEnabled();
	return false;
}

void VansAnimationNode::SetTransformID(uint32_t transformID)
{
	m_TransformID    = transformID;
	m_HasTransformID = true;
}

void VansAnimationNode::SetRootBone(const std::string& boneName)
{
	auto it = m_Skeleton.boneNameToIndex.find(boneName);
	if (it != m_Skeleton.boneNameToIndex.end())
	{
		if (m_Controller)
			m_Controller->SetRootBoneIndex(it->second);

		VANS_LOG("[VansAnimationNode] " << m_Name << ": root bone set to \"" << boneName
		         << "\" (index " << it->second << ")");
	}
	else
	{
		VANS_LOG_WARN("[VansAnimationNode] " << m_Name << ": root bone \"" << boneName << "\" not found in skeleton");
	}
}

glm::vec3 VansAnimationNode::GetRootMotionDelta() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetRootMotionDelta();
	if (m_Controller)
		return m_Controller->GetRootMotionDelta();
	return glm::vec3(0.0f);
}

glm::quat VansAnimationNode::GetRootRotationDelta() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetRootRotationDelta();
	if (m_Controller)
		return m_Controller->GetRootRotationDelta();
	return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

// ════════════════════════════════════════════════════════════════
//  Bone Overrides
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetBoneLocalTransform(const std::string& boneName, const glm::mat4& transform)
{
	m_BoneOverrides[boneName] = transform;
}

bool VansAnimationNode::HasRootMotionDelta() const
{
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->HasRootMotionDelta();
	return m_Controller && m_Controller->HasRootMotionDelta();
}

bool VansAnimationNode::TryGetBoneLocalTransform(const std::string& boneName, glm::mat4& transform) const
{
	const auto found = m_BoneOverrides.find(boneName);
	if (found == m_BoneOverrides.end())
		return false;
	transform = found->second;
	return true;
}

bool VansAnimationNode::TryGetCurrentBoneLocalTransform(const std::string& boneName, glm::mat4& transform) const
{
	if (TryGetBoneLocalTransform(boneName, transform)) return true;
	int boneIndex = -1;
	if (const auto found = m_Skeleton.boneNameToIndex.find(boneName); found != m_Skeleton.boneNameToIndex.end())
		boneIndex = found->second;
	else
	{
		try
		{
			const int stableId = std::stoi(boneName);
			const auto found = std::find_if(m_Skeleton.bones.begin(), m_Skeleton.bones.end(),
				[&](const BoneInfo& bone) { return bone.id == stableId; });
			if (found != m_Skeleton.bones.end()) boneIndex = static_cast<int>(found - m_Skeleton.bones.begin());
		}
		catch (...) {}
	}
	if (boneIndex < 0 || boneIndex >= static_cast<int>(m_Skeleton.bones.size())) return false;
	if (m_Controller)
	{
		const auto& globals = m_Controller->GetCachedGlobalTransforms();
		if (boneIndex < static_cast<int>(globals.size()))
		{
			const int parent = m_Skeleton.bones[boneIndex].parentIndex;
			transform = parent >= 0 && parent < static_cast<int>(globals.size())
				? glm::inverse(globals[parent]) * globals[boneIndex] : globals[boneIndex];
			return true;
		}
	}
	transform = m_Skeleton.bones[boneIndex].localTransform;
	return true;
}

void VansAnimationNode::ClearBoneOverride(const std::string& boneName)
{
	m_BoneOverrides.erase(boneName);
}

// ════════════════════════════════════════════════════════════════
//  每帧更新
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::RebuildNodeTransformBindings()
{
	m_NodeTransformBindings.clear();
	if (!m_Controller || m_RenderNodes.empty())
		return;

	std::unordered_set<std::string> animatedPaths;
	std::unordered_set<std::string> animatedNames;
	for (const auto& [clipName, clip] : m_Controller->GetClipsMap())
	{
		for (const NodeTransformChannel& channel : clip.nodeTransformChannels)
		{
			if (!channel.nodePath.empty())
				animatedPaths.insert(channel.nodePath);
			if (!channel.nodeName.empty())
				animatedNames.insert(channel.nodeName);
		}
	}
	if (animatedPaths.empty() && animatedNames.empty())
		return;

	for (VansRenderNode* renderNode : m_RenderNodes)
	{
		if (!renderNode || renderNode->m_TransformID == UINT32_MAX)
			continue;

		const VansMesh* mesh = renderNode->m_Mesh;
		const std::string nodePath = mesh ? mesh->m_SourceNodePath : std::string();
		const std::string nodeName = mesh ? mesh->m_SourceNodeName : std::string();
		const bool matchesPath = !nodePath.empty() && animatedPaths.count(nodePath) > 0;
		const bool matchesName = !nodeName.empty() && animatedNames.count(nodeName) > 0;
		if (!matchesPath && !matchesName)
			continue;

		NodeTransformBinding binding;
		binding.nodeName = nodeName;
		binding.nodePath = nodePath;
		binding.transformID = renderNode->m_TransformID;
		binding.renderNode = renderNode;
		m_NodeTransformBindings.push_back(binding);

		renderNode->m_HasSkeletonBone = false;
		renderNode->m_AnimationEnabled = false;
		renderNode->m_AnimOwner = nullptr;
		renderNode->m_AnimBoneIDBuffer = nullptr;
		renderNode->m_AnimBoneWeightBuffer = nullptr;
		renderNode->m_VertexDeformationState = VansVertexDeformationState{};
		renderNode->MarkAnimationDescriptorDirty();
	}

	if (!m_NodeTransformBindings.empty())
	{
		VANS_LOG("[VansAnimationNode] " << m_Name << ": bound "
			<< m_NodeTransformBindings.size() << " node transform channel(s)");
	}
}

void VansAnimationNode::ApplySampledNodeTransforms()
{
	if (!m_Controller || m_NodeTransformBindings.empty())
		return;

	const VansAnimationController* animationSource =
		m_RetargetEnabled && m_SourceController ? m_SourceController.get() : m_Controller;
	const auto& sampledTransforms = animationSource->GetSampledNodeTransforms();
	if (sampledTransforms.empty())
		return;

	const glm::mat4 ownerWorld = m_HasTransformID
		? VansTransformStore::GetTransform(m_TransformID).GetModelMatrix()
		: glm::mat4(1.0f);

	for (const NodeTransformBinding& binding : m_NodeTransformBindings)
	{
		const SampledNodeTransform* sampled = nullptr;
		for (const SampledNodeTransform& candidate : sampledTransforms)
		{
			if ((!binding.nodePath.empty() && candidate.nodePath == binding.nodePath)
				|| (binding.nodePath.empty() && !binding.nodeName.empty()
					&& candidate.nodeName == binding.nodeName))
			{
				sampled = &candidate;
				break;
			}
		}
		if (!sampled || binding.transformID == UINT32_MAX)
			continue;

		glm::vec3 position(0.0f);
		glm::vec3 rotationDegrees(0.0f);
		glm::vec3 scale(1.0f);
		if (!DecomposeMatrixToPRS(ownerWorld * sampled->modelTransform,
		                          position,
		                          rotationDegrees,
		                          scale))
			continue;

		VansTransform& transform = VansTransformStore::GetTransform(binding.transformID);
		transform.m_Position = position;
		transform.m_Rotation = rotationDegrees;
		transform.m_Scale = scale;
		VansTransformStore::TransformIDToTransformDirty[binding.transformID] = true;
	}
}

void VansAnimationNode::PrepareAnimationFrame(
	const VansAnimationFrameContext& context)
{
	const float deltaTime = context.deltaTime;
	bool retargetSourceFramePrepared = false;
	if (m_CharacterMotionFramePrepared)
	{
		if (m_RetargetEnabled && m_SourceController && m_RetargetProcessor.IsValid())
		{
			retargetSourceFramePrepared = true;
		}
		else if (m_Controller && m_HasTransformID)
		{
			m_Controller->SetOwnerWorldTransform(
				VansTransformStore::GetTransform(m_TransformID).GetModelMatrix());
		}
		m_CharacterMotionFramePrepared = false;
		if (!retargetSourceFramePrepared)
			return;
	}
	if (!m_Controller)
		return;

	if (m_RetargetEnabled && m_SourceController && m_RetargetProcessor.IsValid())
	{
		if (m_HasTransformID)
		{
			const glm::mat4 ownerWorld = VansTransformStore::GetTransform(m_TransformID).GetModelMatrix();
			m_Controller->SetOwnerWorldTransform(ownerWorld);
			m_SourceController->SetOwnerWorldTransform(ownerWorld);
		}

		if (!retargetSourceFramePrepared)
		{
			SyncRetargetParameters();
			m_SourceController->Update(deltaTime, m_SourceSkeleton);
		}
		if (const MotionMatchingDebugData* sourceMM = m_SourceController->GetMotionMatchingDebugData())
		{
			const bool shouldLog =
				sourceMM->usedThisFrame &&
				(sourceMM->switches != m_LastRetargetSourceMMSwitchCount ||
				 sourceMM->activeClip != m_LastRetargetSourceMMActiveClip ||
				 sourceMM->selectedClip != m_LastRetargetSourceMMSelectedClip);
			if (shouldLog)
			{
				VANS_LOG("[RetargetMM] " << m_Name
					<< ": source used=" << sourceMM->usedThisFrame
					<< " querySpeed=" << sourceMM->querySpeed
					<< " queryDirection=" << sourceMM->queryDirection
					<< " facingYaw=" << sourceMM->currentFacingYawDegrees
					<< " desiredYaw=" << sourceMM->desiredFacingYawDegrees
					<< " appliedRootYaw=" << sourceMM->appliedRootYawDeltaDegrees
					<< " moveState=" << m_SourceController->GetInt("MoveState")
					<< " activeClip='" << sourceMM->activeClip << "'"
					<< " selectedClip='" << sourceMM->selectedClip << "'"
					<< " switches=" << sourceMM->switches
					<< " candidates=" << sourceMM->topCandidates.size()
					<< " samples=" << sourceMM->sampleCount);
				m_LastRetargetSourceMMSwitchCount = sourceMM->switches;
				m_LastRetargetSourceMMActiveClip = sourceMM->activeClip;
				m_LastRetargetSourceMMSelectedClip = sourceMM->selectedClip;
			}
		}
		std::vector<glm::mat4> targetModelTransforms;
		if (m_RetargetProcessor.Process(
			m_SourceController->GetCachedGlobalTransforms(),
			m_SourceSkeleton,
			m_Skeleton,
			targetModelTransforms))
		{
			VansAnimationExternalInputSnapshot targetInput =
				m_Controller->GetAnimationExternalInput();
			const VansAnimationExternalInputSnapshot& sourceInput =
				m_SourceController->GetAnimationExternalInput();
			targetInput.grounded = sourceInput.grounded;
			targetInput.airborne = sourceInput.airborne;
			targetInput.approachDirectionWorld = sourceInput.approachDirectionWorld;
			targetInput.contacts = sourceInput.contacts;
			m_Controller->SetAnimationExternalInput(std::move(targetInput));
			if (m_RetargetDesc.debugDraw && !m_RetargetPoseAuditLogged)
			{
				const glm::mat4* auditOwnerWorld = nullptr;
				glm::mat4 ownerWorldForAudit(1.0f);
				if (m_HasTransformID)
				{
					ownerWorldForAudit = VansTransformStore::GetTransform(m_TransformID).GetModelMatrix();
					auditOwnerWorld = &ownerWorldForAudit;
				}

				int enabledRenderNodes = 0;
				for (const auto* renderNode : m_RenderNodes)
				{
					if (renderNode && renderNode->IsEnabled())
						++enabledRenderNodes;
				}

				LogRetargetPoseAudit(
					m_Name,
					m_Skeleton,
					targetModelTransforms,
					auditOwnerWorld,
					static_cast<int>(m_RenderNodes.size()),
					enabledRenderNodes);
				m_RetargetPoseAuditLogged = true;
			}
			if (!m_Controller->SubmitExternalModelPose(
				targetModelTransforms,
				m_Skeleton,
				deltaTime,
				VansExternalPoseEvaluationMode::TargetPostProcess,
				true))
			{
				VANS_LOG_WARN("[Retarget] " << m_Name
					<< ": target pose submission failed; using target fallback update");
				m_Controller->PrepareFrame(deltaTime, m_Skeleton);
			}
		}
		else
		{
			VANS_LOG_WARN("[Retarget] " << m_Name << ": runtime process failed; using target fallback update");
			m_Controller->PrepareFrame(deltaTime, m_Skeleton);
		}

		if (context.AllowsOwnerMotion() && !retargetSourceFramePrepared &&
			m_SourceController->IsRootMotionEnabled() &&
			m_SourceController->ShouldApplyRootMotionToOwner() && m_HasTransformID)
		{
			ApplyRootMotionToTransform(
				m_SourceController->GetRootMotionDelta(),
				m_SourceController->GetRootRotationDelta());
		}

		ApplySampledNodeTransforms();
		return;
	}

	// 1. 让 Controller 完成核心更新（状态机 + 关键帧插值 + 混合 + root motion + 矩阵输出）
	if (m_HasTransformID)
		m_Controller->SetOwnerWorldTransform(VansTransformStore::GetTransform(m_TransformID).GetModelMatrix());

	m_Controller->PrepareFrame(deltaTime, m_Skeleton);

	// 2. 如果有 root motion，将 delta 应用到 Transform
	if (context.AllowsOwnerMotion() && m_Controller->IsRootMotionEnabled() &&
		m_Controller->ShouldApplyRootMotionToOwner() && m_HasTransformID)
	{
		glm::vec3 deltaPos = m_Controller->GetRootMotionDelta();
		glm::quat deltaRot = m_Controller->GetRootRotationDelta();
		ApplyRootMotionToTransform(deltaPos, deltaRot);
	}
	else if (context.AllowsOwnerMotion())
	{
		// Gameplay-only diagnostic. Editor Preview intentionally evaluates the
		// same pose while withholding owner/CCT motion submission.
		static bool s_LoggedOnce = false;
		if (!s_LoggedOnce)
		{
			VANS_LOG("[RootMotion] Node '" << m_Name << "' skipped ApplyRootMotion: enabled="
			         << m_Controller->IsRootMotionEnabled() << " hasTransformID=" << m_HasTransformID);
			s_LoggedOnce = true;
		}
	}

	ApplySampledNodeTransforms();
}

void VansAnimationNode::GatherAnimationWorldQueries()
{
	if (!m_Controller)
		return;
	if (m_HasTransformID)
	{
		m_Controller->SetOwnerWorldTransform(
			VansTransformStore::GetTransform(m_TransformID).GetModelMatrix());
		m_Controller->SetOwnerStableId(static_cast<std::uint64_t>(m_TransformID) + 1u);
	}
	m_Controller->GatherPreparedWorldQueries(m_Skeleton);
}

void VansAnimationNode::ResolveAnimationWorldQueries(
	const std::vector<VansWorldQueryResult>& results)
{
	if (m_Controller && m_Controller->HasPreparedWorldQueries())
		m_Controller->ResolvePreparedWorldQueries(results, m_Skeleton);
	ApplySampledNodeTransforms();
}

bool VansAnimationNode::HasAnimationWorldQueries() const
{
	return m_Controller && m_Controller->HasPreparedWorldQueries();
}

const std::vector<VansWorldQueryRequest>& VansAnimationNode::GetAnimationWorldQueries() const
{
	static const std::vector<VansWorldQueryRequest> empty;
	return m_Controller ? m_Controller->GetPreparedWorldQueries() : empty;
}

void VansAnimationNode::Update(const VansAnimationFrameContext& context)
{
	PrepareAnimationFrame(context);
	GatherAnimationWorldQueries();
	ResolveAnimationWorldQueries({});
}

void VansAnimationNode::PrepareCharacterMotionFrame(
	float deltaTime, const Vans::VansCharacterTrajectory& trajectory)
{
	VansAnimationController* characterMotionController = GetCharacterMotionController();
	if (!characterMotionController)
		return;
	characterMotionController->SetCharacterTrajectory(&trajectory);
	if (m_HasTransformID)
	{
		const glm::mat4 ownerWorld =
			VansTransformStore::GetTransform(m_TransformID).GetModelMatrix();
		m_Controller->SetOwnerWorldTransform(ownerWorld);
		characterMotionController->SetOwnerWorldTransform(ownerWorld);
	}
	if (m_RetargetEnabled && m_SourceController)
	{
		SyncRetargetParameters();
		m_SourceController->PrepareFrame(deltaTime, m_SourceSkeleton);
	}
	else
	{
		m_Controller->PrepareFrame(deltaTime, m_Skeleton);
	}
	m_CharacterMotionFramePrepared = true;
}

// ════════════════════════════════════════════════════════════════
// Result access
// ════════════════════════════════════════════════════════════════

const BoneMatricesSSBO& VansAnimationNode::GetBoneSSBO() const
{
	if (m_Controller)
		return m_Controller->GetBoneMatricesSSBO();
	return m_BoneMatricesSSBO;
}

const VansAnimationFrameVector<VansAnimationEventSample>& VansAnimationNode::GetSampledEvents() const
{
	static const VansAnimationFrameVector<VansAnimationEventSample> empty(
		std::pmr::new_delete_resource());
	if (m_RetargetEnabled && m_SourceController)
		return m_SourceController->GetSampledEvents();
	return m_Controller ? m_Controller->GetSampledEvents() : empty;
}

// ════════════════════════════════════════════════════════════════
// GPU resource management
// ════════════════════════════════════════════════════════════════

bool VansAnimationNode::InitGPUResources(VkDevice device, uint32_t framesInFlight)
{
	m_Device         = device;
	m_FramesInFlight = framesInFlight;

	VkDeviceSize bufferSize = sizeof(BoneMatricesSSBO);
	m_BoneBuffers.resize(framesInFlight);
	m_PreviousBoneBuffers.resize(framesInFlight);

	for (uint32_t i = 0; i < framesInFlight; i++)
	{
		bool ok = m_BoneBuffers[i].CreatVulkanBuffer(
			device,
			bufferSize,
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (!ok)
		{
			VANS_LOG_ERROR("[VansAnimationNode] " << m_Name << ": failed to create bone buffer " << i);
			return false;
		}

		ok = m_PreviousBoneBuffers[i].CreatVulkanBuffer(
			device,
			bufferSize,
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (!ok)
		{
			VANS_LOG_ERROR("[VansAnimationNode] " << m_Name << ": failed to create previous bone buffer " << i);
			return false;
		}
	}
	m_HasUploadedBoneMatrices = false;

	VANS_LOG("[VansAnimationNode] " << m_Name << ": GPU resources initialized ("
	         << framesInFlight << " frames, " << bufferSize << " bytes each)");
	return true;
}

void VansAnimationNode::DestroyGPUResources()
{
	if (m_Device == VK_NULL_HANDLE)
		return;

	for (auto& buffer : m_BoneBuffers)
		buffer.DestroyVulkanBuffer(m_Device);
	for (auto& buffer : m_PreviousBoneBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	for (auto& buffer : m_PerSubmeshBoneIDBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	for (auto& buffer : m_PerSubmeshBoneWeightBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	m_BoneBuffers.clear();
	m_PreviousBoneBuffers.clear();
	m_HasUploadedBoneMatrices = false;
	m_PerSubmeshBoneIDBuffers.clear();
	m_PerSubmeshBoneWeightBuffers.clear();
	m_Device = VK_NULL_HANDLE;
}

void VansAnimationNode::UploadPerSubmeshBoneBuffers(const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData)
{
	if (perSubmeshBoneData.empty() || m_Device == VK_NULL_HANDLE)
		return;

	uint32_t submeshCount = static_cast<uint32_t>(perSubmeshBoneData.size());
	m_PerSubmeshBoneIDBuffers.resize(submeshCount);
	m_PerSubmeshBoneWeightBuffers.resize(submeshCount);

	for (uint32_t s = 0; s < submeshCount; s++)
	{
		const auto& boneData = perSubmeshBoneData[s];
		if (boneData.empty())
		{
			m_PerSubmeshBoneIDBuffers[s].CreatVulkanBuffer(
				m_Device, 64, VK_FORMAT_R32_SFLOAT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			m_PerSubmeshBoneWeightBuffers[s].CreatVulkanBuffer(
				m_Device, 64, VK_FORMAT_R32_SFLOAT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			continue;
		}

		uint32_t vertexCount = static_cast<uint32_t>(boneData.size());

		std::vector<VertexBoneID> boneIDs(vertexCount);
		std::vector<VertexBoneWeight> boneWeights(vertexCount);
		for (uint32_t v = 0; v < vertexCount; v++)
		{
			for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
			{
				boneIDs[v].boneIDs[i]     = boneData[v].boneIDs[i];
				boneWeights[v].weights[i]  = boneData[v].weights[i];
			}
		}

		VkDeviceSize idBufferSize = sizeof(VertexBoneID) * vertexCount;
		bool ok = m_PerSubmeshBoneIDBuffers[s].CreatVulkanBuffer(
			m_Device, idBufferSize, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (!ok)
		{
			VANS_LOG_ERROR("[VansAnimationNode] " << m_Name << ": failed to create bone ID buffer for submesh " << s);
			continue;
		}
		m_PerSubmeshBoneIDBuffers[s].SetBufferData(
			boneIDs.data(), 0, static_cast<int>(idBufferSize));

		VkDeviceSize weightBufferSize = sizeof(VertexBoneWeight) * vertexCount;
		ok = m_PerSubmeshBoneWeightBuffers[s].CreatVulkanBuffer(
			m_Device, weightBufferSize, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (!ok)
		{
			VANS_LOG_ERROR("[VansAnimationNode] " << m_Name << ": failed to create bone weight buffer for submesh " << s);
			continue;
		}
		m_PerSubmeshBoneWeightBuffers[s].SetBufferData(
			boneWeights.data(), 0, static_cast<int>(weightBufferSize));

		VANS_LOG("[VansAnimationNode] " << m_Name << ": submesh " << s
			<< " bone buffers uploaded (" << vertexCount << " vertices)");
	}

	VANS_LOG("[VansAnimationNode] " << m_Name << ": uploaded per-submesh bone buffers for "
		<< submeshCount << " submesh(es)");
}

void VansAnimationNode::UploadBoneMatrices(uint32_t frameIndex)
{
	UploadBoneMatrices(frameIndex, GetBoneSSBO());
}

void VansAnimationNode::UploadBoneMatrices(
	uint32_t frameIndex,
	const BoneMatricesSSBO& ssbo)
{
	if (frameIndex >= m_BoneBuffers.size() || frameIndex >= m_PreviousBoneBuffers.size())
		return;

	const BoneMatricesSSBO& previous = m_HasUploadedBoneMatrices
		? m_PreviousBoneMatricesSSBO
		: ssbo;
	m_PreviousBoneBuffers[frameIndex].SetBufferData(
		&previous,
		0,
		sizeof(BoneMatricesSSBO));
	m_BoneBuffers[frameIndex].SetBufferData(
		&ssbo,
		0,
		sizeof(BoneMatricesSSBO));
	m_PreviousBoneMatricesSSBO = ssbo;
	m_HasUploadedBoneMatrices = true;
}

// ════════════════════════════════════════════════════════════════
// Apply bone overrides.
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms)
{
	for (const auto& [boneName, overrideTransform] : m_BoneOverrides)
	{
		auto it = m_Skeleton.boneNameToIndex.find(boneName);
		if (it != m_Skeleton.boneNameToIndex.end())
		{
			int idx = it->second;
			if (idx >= 0 && idx < static_cast<int>(localTransforms.size()))
				localTransforms[idx] = overrideTransform;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// Apply root motion to the owning scene transform.
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::ApplyRootMotionToTransform(const glm::vec3& deltaPos, const glm::quat& deltaRot)
{
	if (!m_HasTransformID)
		return;

	// Skip a near-zero delta.
	if (glm::length(deltaPos) < 0.00001f && glm::abs(glm::dot(deltaRot, glm::quat(1, 0, 0, 0)) - 1.0f) < 0.00001f)
		return;

	VansTransform& transform = VansTransformStore::GetTransform(m_TransformID);

	const Vans::VansRootMotionOwnerDelta ownerDelta =
		Vans::ResolveAnimationRootMotionOwnerDelta(
			deltaPos, deltaRot, transform.m_Rotation.y, transform.m_Scale);
	transform.m_Position += ownerDelta.translationWorld;
	transform.m_Rotation.y += ownerDelta.yawDegrees;

	VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
}

// ════════════════════════════════════════════════════════════════
void VansAnimationNode::SyncRetargetParameters()
{
	if (!m_Controller || !m_SourceController)
		return;

	for (const auto& [name, param] : m_Controller->GetParameters())
	{
		if (!m_SourceController->HasParameter(name))
			continue;

		switch (param.type)
		{
		case AnimatorParamType::Float:
			m_SourceController->SetFloat(name, param.floatVal);
			break;
		case AnimatorParamType::Bool:
			m_SourceController->SetBool(name, param.boolVal);
			break;
		case AnimatorParamType::Int:
			m_SourceController->SetInt(name, param.intVal);
			break;
		case AnimatorParamType::Trigger:
			if (param.boolVal)
			{
				m_SourceController->SetTrigger(name);
				// 外部姿态控制器不执行普通图更新；移交后消费，避免下一帧重复触发源图。
				m_Controller->ResetTrigger(name);
			}
			break;
		case AnimatorParamType::Vector3:
			m_SourceController->SetVector3(name, param.vec3Val);
			break;
		case AnimatorParamType::Quaternion:
			m_SourceController->SetQuaternion(name, param.quatVal);
			break;
		}
	}
}
