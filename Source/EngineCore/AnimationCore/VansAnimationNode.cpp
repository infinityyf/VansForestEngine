#include "VansAnimationNode.h"
#include "MotionMatching/VansMotionMatching.h"
#include "../RenderCore/VansRenderNode.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cctype>
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

	std::string ToLowerAscii(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	bool IsOwnerFeetAlignmentMode(const std::string& mode)
	{
		const std::string normalized = ToLowerAscii(mode);
		return normalized == "feet_to_owner" ||
		       normalized == "feettoowner" ||
		       normalized == "feet_to_owner_ground" ||
		       normalized == "owner_ground";
	}

	bool ApplyOwnerFeetAlignment(
		const Skeleton& skeleton,
		const glm::mat4& ownerWorld,
		std::vector<glm::mat4>& modelTransforms)
	{
		const int footL = FindPoseAuditBone(skeleton, "foot_l");
		const int footR = FindPoseAuditBone(skeleton, "foot_r");
		if (footL < 0 || footR < 0 ||
		    footL >= static_cast<int>(modelTransforms.size()) ||
		    footR >= static_cast<int>(modelTransforms.size()))
		{
			return false;
		}

		const glm::vec3 footLModel = ExtractPoseAuditTranslation(modelTransforms[footL]);
		const glm::vec3 footRModel = ExtractPoseAuditTranslation(modelTransforms[footR]);
		const glm::vec3 footLWorld = glm::vec3(ownerWorld * glm::vec4(footLModel, 1.0f));
		const glm::vec3 footRWorld = glm::vec3(ownerWorld * glm::vec4(footRModel, 1.0f));
		if (!IsFinitePoseAuditVec3(footLWorld) || !IsFinitePoseAuditVec3(footRWorld))
			return false;

		const glm::vec3 ownerWorldPosition = glm::vec3(ownerWorld[3]);
		const glm::vec3 footCenterWorld = (footLWorld + footRWorld) * 0.5f;
		const float footGroundWorldY = std::min(footLWorld.y, footRWorld.y);
		const glm::vec3 deltaWorld(
			ownerWorldPosition.x - footCenterWorld.x,
			ownerWorldPosition.y - footGroundWorldY,
			ownerWorldPosition.z - footCenterWorld.z);

		if (glm::length(deltaWorld) <= 1.0e-5f)
			return true;

		const glm::mat4 worldCorrection = glm::translate(glm::mat4(1.0f), deltaWorld);
		const glm::mat4 modelCorrection = glm::inverse(ownerWorld) * worldCorrection * ownerWorld;
		for (glm::mat4& transform : modelTransforms)
			transform = modelCorrection * transform;
		return true;
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

void VansAnimationNode::ConfigureRetargetSource(
	const Skeleton& sourceSkeleton,
	std::unique_ptr<VansAnimationController> sourceController,
	const VansRetargetRuntimeDesc& desc)
{
	m_RetargetEnabled = false;
	m_SourceSkeleton = sourceSkeleton;
	m_SourceController = std::move(sourceController);
	m_RetargetDesc = desc;
	m_LastRetargetSourceMMSwitchCount = -1;
	m_LastRetargetSourceMMActiveClip.clear();
	m_LastRetargetSourceMMSelectedClip.clear();

	if (!m_SourceController)
	{
		VANS_LOG_WARN("[Retarget] " << m_Name << ": missing source controller");
		return;
	}

	if (!m_RetargetProcessor.Build(m_SourceSkeleton, m_Skeleton, m_RetargetDesc))
	{
		VANS_LOG_WARN("[Retarget] " << m_Name << ": failed to build Source -> Target map");
		m_SourceController.reset();
		return;
	}

	const VansRetargetBuildStats& stats = m_RetargetProcessor.GetStats();
	m_RetargetEnabled = true;
	VANS_LOG("[Retarget] " << m_Name
		<< ": sourceBones=" << stats.sourceBoneCount
		<< " targetBones=" << stats.targetBoneCount
		<< " mapped=" << stats.mappedBoneCount
		<< " unmappedTarget=" << stats.unmappedTargetBoneCount
		<< " translationScale=" << stats.translationScale
		<< " translationScaleMode='" << desc.translationScaleMode << "'"
		<< " rootAlignment='" << desc.rootAlignmentMode << "'"
		<< " targetModelSpaceAlignment='" << desc.targetModelSpaceAlignmentMode << "'"
		<< " sourceModel='" << desc.sourceModelPath
		<< "' sourceAnimator='" << desc.sourceAnimatorPath << "'");
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

void VansAnimationNode::SetController(VansAnimationController* controller)
{
	m_Controller = controller;

	if (m_Controller)
	{
		// 将 Node 侧的骨骼覆盖映射关联到 Controller，以便在 Update 管线中应用
		m_Controller->SetBoneOverrides(&m_BoneOverrides);

		VANS_LOG("[VansAnimationNode] " << m_Name << ": controller '" 
		         << m_Controller->GetName() << "' bound");
	}
	RebuildNodeTransformBindings();
}

// ════════════════════════════════════════════════════════════════
//  播放控制（委托给 Controller）
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::Play()
{
	if (m_Controller)
	{
		m_LastEventTime = 0.0f;
		m_Controller->Play();
	}
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Play();
	if (m_Controller)
		Update(0.0f);
}

void VansAnimationNode::Play(const std::string& stateName)
{
	if (m_Controller)
	{
		m_LastEventTime = 0.0f;
		m_Controller->Play(stateName);
	}
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Play(stateName);
	if (m_Controller)
		Update(0.0f);
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
	m_LastEventTime = 0.0f;
	if (m_Controller)
		m_Controller->Stop();
	if (m_RetargetEnabled && m_SourceController)
		m_SourceController->Stop();
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
//  Events
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::AddEvent(const std::string& clipName, AnimationEvent event)
{
	m_Events[clipName].push_back(std::move(event));

	auto& events = m_Events[clipName];
	std::sort(events.begin(), events.end(),
		[](const AnimationEvent& a, const AnimationEvent& b) {
			return a.triggerTime < b.triggerTime;
		});
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

	const auto& sampledTransforms = m_Controller->GetSampledNodeTransforms();
	if (sampledTransforms.empty())
		return;

	std::unordered_map<std::string, const SampledNodeTransform*> byPath;
	std::unordered_map<std::string, const SampledNodeTransform*> byName;
	for (const SampledNodeTransform& sampled : sampledTransforms)
	{
		if (!sampled.nodePath.empty())
			byPath[sampled.nodePath] = &sampled;
		if (!sampled.nodeName.empty())
			byName[sampled.nodeName] = &sampled;
	}

	const glm::mat4 ownerWorld = m_HasTransformID
		? VansTransformStore::GetTransform(m_TransformID).GetModelMatrix()
		: glm::mat4(1.0f);

	for (const NodeTransformBinding& binding : m_NodeTransformBindings)
	{
		const SampledNodeTransform* sampled = nullptr;
		if (!binding.nodePath.empty())
		{
			auto it = byPath.find(binding.nodePath);
			if (it != byPath.end())
				sampled = it->second;
		}
		if (!sampled && !binding.nodeName.empty())
		{
			auto it = byName.find(binding.nodeName);
			if (it != byName.end())
				sampled = it->second;
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

void VansAnimationNode::Update(float deltaTime)
{
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

		SyncRetargetParameters();
		m_SourceController->Update(deltaTime, m_SourceSkeleton);
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
			if (m_HasTransformID && IsOwnerFeetAlignmentMode(m_RetargetDesc.rootAlignmentMode))
			{
				const glm::mat4 ownerWorld = VansTransformStore::GetTransform(m_TransformID).GetModelMatrix();
				if (!ApplyOwnerFeetAlignment(m_Skeleton, ownerWorld, targetModelTransforms))
				{
					VANS_LOG_WARN("[Retarget] " << m_Name
						<< ": root_alignment='" << m_RetargetDesc.rootAlignmentMode
						<< "' could not be applied");
				}
			}

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
			m_Controller->FeedExternalBoneWorldTransforms(targetModelTransforms, m_Skeleton);
		}
		else
		{
			VANS_LOG_WARN("[Retarget] " << m_Name << ": runtime process failed; using target fallback update");
			m_Controller->Update(deltaTime, m_Skeleton);
		}

		if (m_SourceController->IsRootMotionEnabled() && m_HasTransformID)
		{
			ApplyRootMotionToTransform(
				m_SourceController->GetRootMotionDelta(),
				m_SourceController->GetRootRotationDelta());
		}

		ApplySampledNodeTransforms();
		FireEvents();
		return;
	}

	// 1. 让 Controller 完成核心更新（状态机 + 关键帧插值 + 混合 + root motion + 矩阵输出）
	if (m_HasTransformID)
		m_Controller->SetOwnerWorldTransform(VansTransformStore::GetTransform(m_TransformID).GetModelMatrix());

	m_Controller->Update(deltaTime, m_Skeleton);

	// 2. 如果有 root motion，将 delta 应用到 Transform
	if (m_Controller->IsRootMotionEnabled() && m_HasTransformID)
	{
		glm::vec3 deltaPos = m_Controller->GetRootMotionDelta();
		glm::quat deltaRot = m_Controller->GetRootRotationDelta();
		ApplyRootMotionToTransform(deltaPos, deltaRot);
	}
	else
	{
		// 诊断: 仅输出一次
		static bool s_LoggedOnce = false;
		if (!s_LoggedOnce)
		{
			VANS_LOG("[RootMotion] Node '" << m_Name << "' skipped ApplyRootMotion: enabled="
			         << m_Controller->IsRootMotionEnabled() << " hasTransformID=" << m_HasTransformID);
			s_LoggedOnce = true;
		}
	}

	// 3. Fire events; the node still owns event dispatch.
	ApplySampledNodeTransforms();
	FireEvents();
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

// ════════════════════════════════════════════════════════════════
// GPU resource management
// ════════════════════════════════════════════════════════════════

bool VansAnimationNode::InitGPUResources(VkDevice device, uint32_t framesInFlight)
{
	m_Device         = device;
	m_FramesInFlight = framesInFlight;

	VkDeviceSize bufferSize = sizeof(BoneMatricesSSBO);
	m_BoneBuffers.resize(framesInFlight);

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
	}

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

	for (auto& buffer : m_PerSubmeshBoneIDBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	for (auto& buffer : m_PerSubmeshBoneWeightBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	m_BoneBuffers.clear();
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
	if (frameIndex >= m_BoneBuffers.size())
		return;

	const BoneMatricesSSBO& ssbo = GetBoneSSBO();
	m_BoneBuffers[frameIndex].SetBufferData(
		&ssbo,
		0,
		sizeof(BoneMatricesSSBO));
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

	// Rotate the local-space delta into world space using the owner's current yaw.
	float yawRad = glm::radians(transform.m_Rotation.y);
	glm::mat3 entityYawMat = glm::mat3(glm::rotate(glm::mat4(1.0f), yawRad, glm::vec3(0.0f, 1.0f, 0.0f)));
	glm::vec3 worldDelta = entityYawMat * (deltaPos * transform.m_Scale);

	transform.m_Position += worldDelta;

	// Apply only the yaw component from root-motion rotation.
	glm::vec3 deltaEuler = glm::degrees(glm::eulerAngles(deltaRot));
	transform.m_Rotation.y += deltaEuler.y;

	VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
}

// ════════════════════════════════════════════════════════════════
// Fire animation notify events for the current frame.
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::FireEvents()
{
	if (!m_Controller)
		return;

	std::string currentClipName = GetCurrentStateName();
	float currentTime = GetCurrentPlayTime();

	auto it = m_Events.find(currentClipName);
	if (it == m_Events.end())
		return;

	for (const auto& event : it->second)
	{
		if (event.triggerTime > m_LastEventTime && event.triggerTime <= currentTime)
		{
			if (event.callback)
				event.callback();
		}
	}

	m_LastEventTime = currentTime;
}

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
				m_SourceController->SetTrigger(name);
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
