#include "VansAnimationNode.h"
#include "../RenderCore/VansRenderNode.h"
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

using namespace VansGraphics;

// ════════════════════════════════════════════════════════════════
//  Construction & Destruction
// ════════════════════════════════════════════════════════════════

VansAnimationNode::VansAnimationNode(const std::string& name)
	: m_Name(name)
{
	std::memset(&m_BoneMatricesSSBO, 0, sizeof(BoneMatricesSSBO));
	// Initialize all bone matrices to identity
	for (uint32_t i = 0; i < MAX_BONES; i++)
		m_BoneMatricesSSBO.boneMatrices[i] = glm::mat4(1.0f);
}

VansAnimationNode::~VansAnimationNode()
{
	DestroyGPUResources();
}

// ════════════════════════════════════════════════════════════════
//  Setup
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetRenderNode(VansRenderNode* renderNode)
{
	m_RenderNodes = { renderNode };
}

void VansAnimationNode::SetRenderNodes(const std::vector<VansRenderNode*>& nodes)
{
	m_RenderNodes = nodes;
}

void VansAnimationNode::SetSkeleton(const Skeleton& skeleton)
{
	m_Skeleton = skeleton;
	VANS_LOG("[VansAnimationNode] " << m_Name << ": skeleton set with "
	         << m_Skeleton.bones.size() << " bones");
}

// ════════════════════════════════════════════════════════════════
//  Clip Management
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::AddClip(const VansAnimationClip& clip)
{
	m_Clips[clip.clipName] = clip;
	VANS_LOG("[VansAnimationNode] " << m_Name << ": added clip \"" << clip.clipName
	         << "\" (duration=" << clip.duration << "s)");
}

void VansAnimationNode::AddClip(VansAnimationClip&& clip)
{
	std::string name = clip.clipName;
	float dur = clip.duration;
	m_Clips[name] = std::move(clip);
	VANS_LOG("[VansAnimationNode] " << m_Name << ": added clip \""
	         << name << "\" (duration=" << dur << "s)");
}

bool VansAnimationNode::RemoveClip(const std::string& clipName)
{
	auto it = m_Clips.find(clipName);
	if (it == m_Clips.end())
		return false;

	// If this clip is currently playing, stop
	if (m_CurrentClipName == clipName)
		Stop();

	m_Clips.erase(it);
	return true;
}

const VansAnimationClip* VansAnimationNode::GetClip(const std::string& clipName) const
{
	auto it = m_Clips.find(clipName);
	return (it != m_Clips.end()) ? &it->second : nullptr;
}

std::vector<std::string> VansAnimationNode::GetClipNames() const
{
	std::vector<std::string> names;
	names.reserve(m_Clips.size());
	for (const auto& [name, clip] : m_Clips)
		names.push_back(name);
	return names;
}

// ════════════════════════════════════════════════════════════════
//  Playback Control
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::Play(const std::string& clipName, const AnimationPlaySettings& settings)
{
	auto it = m_Clips.find(clipName);
	if (it == m_Clips.end())
	{
		VANS_LOG_WARN("[VansAnimationNode] " << m_Name << ": clip \"" << clipName << "\" not found");
		return;
	}

	m_CurrentClipName = clipName;
	m_PlaySettings    = settings;
	m_CurrentTime     = settings.startTime;
	m_State           = AnimationState::Playing;
	m_LastEventTime   = settings.startTime;
	m_PingPongReversing = false;

	VANS_LOG("[VansAnimationNode] " << m_Name << ": playing \"" << clipName << "\"");
}

void VansAnimationNode::CrossFade(const std::string& clipName, float blendDuration,
                                   const AnimationPlaySettings& settings)
{
	if (m_State == AnimationState::Stopped)
	{
		// Nothing to blend from — just play directly
		Play(clipName, settings);
		return;
	}

	auto it = m_Clips.find(clipName);
	if (it == m_Clips.end())
	{
		VANS_LOG_WARN("[VansAnimationNode] " << m_Name << ": crossfade target \"" << clipName << "\" not found");
		return;
	}

	// Save current clip as blend source
	m_PreviousClipName = m_CurrentClipName;
	m_PreviousTime     = m_CurrentTime;

	// Switch to new clip
	m_CurrentClipName = clipName;
	m_PlaySettings    = settings;
	m_CurrentTime     = settings.startTime;
	m_LastEventTime   = settings.startTime;
	m_PingPongReversing = false;

	// Start blending
	m_BlendAlpha    = 0.0f;
	m_BlendDuration = blendDuration;
	m_State         = AnimationState::Blending;

	VANS_LOG("[VansAnimationNode] " << m_Name << ": crossfade to \""
	         << clipName << "\" over " << blendDuration << "s");
}

void VansAnimationNode::Pause()
{
	if (m_State == AnimationState::Playing || m_State == AnimationState::Blending)
		m_State = AnimationState::Paused;
}

void VansAnimationNode::Resume()
{
	if (m_State == AnimationState::Paused)
		m_State = AnimationState::Playing;
}

void VansAnimationNode::Stop()
{
	m_State       = AnimationState::Stopped;
	m_BlendAlpha  = 0.0f;
	// Keep m_CurrentTime so the pose freezes at the current progress
}

void VansAnimationNode::SetTime(float time)
{
	m_CurrentTime = time;
}

void VansAnimationNode::SetSpeed(float speed)
{
	m_PlaySettings.speed = speed;
}

void VansAnimationNode::SetLoop(bool loop)
{
	m_PlaySettings.loop = loop;
}

// ════════════════════════════════════════════════════════════════
//  State Queries
// ════════════════════════════════════════════════════════════════

float VansAnimationNode::GetDuration() const
{
	auto it = m_Clips.find(m_CurrentClipName);
	if (it == m_Clips.end())
		return 0.0f;

	float end = (m_PlaySettings.endTime < 0.0f) ? it->second.duration : m_PlaySettings.endTime;
	return end - m_PlaySettings.startTime;
}

float VansAnimationNode::GetNormalizedTime() const
{
	float dur = GetDuration();
	if (dur <= 0.0f) return 0.0f;
	return m_CurrentTime / dur;
}

// ════════════════════════════════════════════════════════════════
//  Events
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::AddEvent(const std::string& clipName, AnimationEvent event)
{
	m_Events[clipName].push_back(std::move(event));

	// Sort events by trigger time for efficient fire checking
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
	m_RootMotionEnabled = enable;
	if (enable && m_RootBoneIndex < 0)
	{
		m_RootBoneIndex = DetectRootBoneIndex();
		if (m_RootBoneIndex >= 0)
		{
			VANS_LOG("[VansAnimationNode] " << m_Name << ": root motion enabled, root bone = \""
			         << m_Skeleton.bones[m_RootBoneIndex].name << "\" (index " << m_RootBoneIndex << ")");
		}
		else
		{
			VANS_LOG_WARN("[VansAnimationNode] " << m_Name << ": root motion enabled but no root bone found!");
		}
	}
	m_RootMotionInitialized = false; // reset so first frame re-samples
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
		m_RootBoneIndex = it->second;
		VANS_LOG("[VansAnimationNode] " << m_Name << ": root bone set to \"" << boneName
		         << "\" (index " << m_RootBoneIndex << ")");
	}
	else
	{
		VANS_LOG_WARN("[VansAnimationNode] " << m_Name << ": root bone \"" << boneName << "\" not found in skeleton");
	}
}

int VansAnimationNode::DetectRootBoneIndex() const
{
	for (uint32_t i = 0; i < (uint32_t)m_Skeleton.bones.size(); i++)
	{
		if (m_Skeleton.bones[i].parentIndex < 0)
			return (int)i;
	}
	return -1;
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
//  Per-Frame Update
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::Update(float deltaTime)
{
	if (m_State == AnimationState::Stopped || m_State == AnimationState::Paused)
		return;

	const VansAnimationClip* currentClip = GetClip(m_CurrentClipName);
	if (!currentClip)
		return;

	uint32_t boneCount = (uint32_t)m_Skeleton.bones.size();
	if (boneCount == 0)
		return;

	// 1. Advance time
	AdvanceTime(deltaTime);

	// 2. Fire events
	FireEvents();

	// 3. Compute bone local transforms for current clip
	std::vector<glm::mat4> localTransforms(boneCount, glm::mat4(1.0f));
	ComputeBoneTransforms(m_CurrentClipName, m_CurrentTime, localTransforms);

	// 4. If blending, compute previous clip and blend
	if (m_State == AnimationState::Blending)
	{
		// Advance blend alpha (real-time, not affected by playback speed)
		m_BlendAlpha += deltaTime / m_BlendDuration;

		if (m_BlendAlpha >= 1.0f)
		{
			// Blend complete — transition to Playing
			m_BlendAlpha = 1.0f;
			m_State = AnimationState::Playing;
			m_PreviousClipName.clear();
		}
		else
		{
			// Compute previous clip transforms
			std::vector<glm::mat4> prevLocalTransforms(boneCount, glm::mat4(1.0f));
			ComputeBoneTransforms(m_PreviousClipName, m_PreviousTime, prevLocalTransforms);

			// Also advance previous clip time
			const VansAnimationClip* prevClip = GetClip(m_PreviousClipName);
			if (prevClip)
			{
				m_PreviousTime += deltaTime * m_PlaySettings.speed;
				if (m_PreviousTime > prevClip->duration)
					m_PreviousTime = prevClip->duration;
			}

			// Blend: lerp from previous → current by blendAlpha
			BlendTransforms(prevLocalTransforms, localTransforms, m_BlendAlpha, localTransforms);
		}
	}

	// 5. Apply bone overrides (IK / procedural)
	ApplyBoneOverrides(localTransforms);

	// 5.5 Ensure root bone index is always known (needed by UpdateHierarchy even without root motion)
	if (m_RootBoneIndex < 0)
		m_RootBoneIndex = DetectRootBoneIndex();

	// 6. Extract root motion (before hierarchy propagation)
	if (m_RootMotionEnabled)
		ExtractRootMotion(localTransforms);

	// 7. Update hierarchy: propagate local → global
	UpdateHierarchy(localTransforms);

	// 8. Build final matrices: global * offset
	BuildFinalMatrices();
}

// ════════════════════════════════════════════════════════════════
//  GPU Resource Management
// ════════════════════════════════════════════════════════════════

bool VansAnimationNode::InitGPUResources(VkDevice device, uint32_t framesInFlight)
{
	m_Device         = device;
	m_FramesInFlight = framesInFlight;

	VkDeviceSize bufferSize = sizeof(BoneMatricesSSBO);  // MAX_BONES * sizeof(mat4) = 8192 bytes
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
			// Create minimal dummy buffers for empty submeshes
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

		// Split VertexBoneData into separate ID and weight arrays
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

		// Upload bone IDs buffer
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

		// Upload bone weights buffer
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

	m_BoneBuffers[frameIndex].SetBufferData(
		&m_BoneMatricesSSBO,
		0,
		sizeof(BoneMatricesSSBO));
}

// ════════════════════════════════════════════════════════════════
//  Internal: AdvanceTime
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::AdvanceTime(float dt)
{
	const VansAnimationClip* clip = GetClip(m_CurrentClipName);
	if (!clip) return;

	float effectiveDuration = (m_PlaySettings.endTime < 0.0f)
		? clip->duration
		: m_PlaySettings.endTime;
	float start = m_PlaySettings.startTime;

	float delta = dt * m_PlaySettings.speed;

	if (m_PlaySettings.pingPong)
	{
		if (m_PingPongReversing)
			m_CurrentTime -= delta;
		else
			m_CurrentTime += delta;

		if (m_CurrentTime >= effectiveDuration)
		{
			m_CurrentTime = effectiveDuration;
			m_PingPongReversing = true;
		}
		else if (m_CurrentTime <= start)
		{
			m_CurrentTime = start;
			m_PingPongReversing = false;

			if (!m_PlaySettings.loop)
			{
				m_State = AnimationState::Stopped;
				return;
			}
		}
	}
	else
	{
		m_CurrentTime += delta;

		if (m_CurrentTime >= effectiveDuration)
		{
			if (m_PlaySettings.loop)
			{
				// Wrap around
				float range = effectiveDuration - start;
				if (range > 0.0f)
					m_CurrentTime = start + fmod(m_CurrentTime - start, range);
				else
					m_CurrentTime = start;

				m_LastEventTime = start;  // reset event tracking on loop
				m_LoopJustWrapped = true; // signal root motion to skip delta this frame
			}
			else
			{
				m_CurrentTime = effectiveDuration;
				m_State = AnimationState::Stopped;
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  Internal: FireEvents
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::FireEvents()
{
	auto it = m_Events.find(m_CurrentClipName);
	if (it == m_Events.end())
		return;

	for (const auto& event : it->second)
	{
		// Fire if the event time is between last checked time and current time
		if (event.triggerTime > m_LastEventTime && event.triggerTime <= m_CurrentTime)
		{
			if (event.callback)
				event.callback();
		}
	}

	m_LastEventTime = m_CurrentTime;
}

// ════════════════════════════════════════════════════════════════
//  Internal: ComputeBoneTransforms
//  Interpolate keyframes for each bone → produce local transform matrices
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::ComputeBoneTransforms(const std::string& clipName, float time,
                                               std::vector<glm::mat4>& outLocalTransforms)
{
	const VansAnimationClip* clip = GetClip(clipName);
	if (!clip) return;

	uint32_t boneCount = (uint32_t)m_Skeleton.bones.size();

	for (uint32_t b = 0; b < boneCount; b++)
	{
		if (b >= clip->boneKeyframes.size() || clip->boneKeyframes[b].empty())
		{
			outLocalTransforms[b] = glm::mat4(1.0f);
			continue;
		}

		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scl;
		InterpolateKeyframes(clip->boneKeyframes[b], time, pos, rot, scl);

		// Compose TRS → mat4
		glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
		glm::mat4 R = glm::toMat4(rot);
		glm::mat4 S = glm::scale(glm::mat4(1.0f), scl);
		outLocalTransforms[b] = T * R * S;
	}
}

// ════════════════════════════════════════════════════════════════
//  Internal: BlendTransforms
//  Decompose both matrices to TRS, interpolate, recompose
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::BlendTransforms(const std::vector<glm::mat4>& a,
                                         const std::vector<glm::mat4>& b,
                                         float alpha,
                                         std::vector<glm::mat4>& outBlended)
{
	uint32_t count = (uint32_t)(std::min)(a.size(), b.size());
	outBlended.resize(count);

	for (uint32_t i = 0; i < count; i++)
	{
		// Decompose A
		glm::vec3 scaleA, posA, skewA;
		glm::quat rotA;
		glm::vec4 perspA;
		glm::decompose(a[i], scaleA, rotA, posA, skewA, perspA);

		// Decompose B
		glm::vec3 scaleB, posB, skewB;
		glm::quat rotB;
		glm::vec4 perspB;
		glm::decompose(b[i], scaleB, rotB, posB, skewB, perspB);

		// Blend
		glm::vec3 blendedPos   = glm::mix(posA, posB, alpha);
		glm::quat blendedRot   = glm::slerp(rotA, rotB, alpha);
		glm::vec3 blendedScale = glm::mix(scaleA, scaleB, alpha);

		// Recompose
		glm::mat4 T = glm::translate(glm::mat4(1.0f), blendedPos);
		glm::mat4 R = glm::toMat4(blendedRot);
		glm::mat4 S = glm::scale(glm::mat4(1.0f), blendedScale);
		outBlended[i] = T * R * S;
	}
}

// ════════════════════════════════════════════════════════════════
//  Internal: ApplyBoneOverrides
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms)
{
	for (const auto& [boneName, overrideTransform] : m_BoneOverrides)
	{
		auto it = m_Skeleton.boneNameToIndex.find(boneName);
		if (it != m_Skeleton.boneNameToIndex.end())
		{
			int idx = it->second;
			if (idx >= 0 && idx < (int)localTransforms.size())
				localTransforms[idx] = overrideTransform;
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  Internal: ExtractRootMotion
//  Extracts the root bone's translation/rotation delta per frame,
//  applies it to the entity's VansTransformStore, and zeros the
//  root bone's horizontal translation in the skeleton so the mesh
//  doesn't double-move.
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::ExtractRootMotion(std::vector<glm::mat4>& localTransforms)
{
	if (m_RootBoneIndex < 0 || m_RootBoneIndex >= (int)localTransforms.size())
		return;

	// Decompose root bone local transform
	glm::vec3 rootPos, rootScale, skew;
	glm::quat rootRot;
	glm::vec4 perspective;
	glm::decompose(localTransforms[m_RootBoneIndex], rootScale, rootRot, rootPos, skew, perspective);

	// Reset deltas
	m_LastRootMotionDelta    = glm::vec3(0.0f);
	m_LastRootRotationDelta  = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	if (!m_RootMotionInitialized)
	{
		// First frame: just save the root pose, no delta to apply
		m_PrevRootPosition      = rootPos;
		m_PrevRootRotation      = rootRot;
		m_RootMotionInitialized = true;
	}
	else if (m_LoopJustWrapped)
	{
		// Loop just wrapped — the position jumped back to clip start.
		// Don't apply a delta this frame; just resample the new start pose.
		m_PrevRootPosition = rootPos;
		m_PrevRootRotation = rootRot;
		m_LoopJustWrapped  = false;
	}
	else
	{
		// Normal frame: compute delta
		glm::vec3 deltaPos = rootPos - m_PrevRootPosition;
		glm::quat deltaRot = rootRot * glm::inverse(m_PrevRootRotation);

		m_PrevRootPosition = rootPos;
		m_PrevRootRotation = rootRot;

		m_LastRootMotionDelta   = deltaPos;
		m_LastRootRotationDelta = deltaRot;

		// Apply delta to world transform
		if (m_HasTransformID)
		{
			VansTransform& transform = VansTransformStore::GetTransform(m_TransformID);

			// Rotate the local-space delta into world space using the entity's current Y rotation,
			// and scale by the entity's transform scale so root motion matches the visual size.
			float yawRad = glm::radians(transform.m_Rotation.y);
			glm::mat3 entityYawMat = glm::mat3(glm::rotate(glm::mat4(1.0f), yawRad, glm::vec3(0.0f, 1.0f, 0.0f)));
			glm::vec3 worldDelta = entityYawMat * (deltaPos * transform.m_Scale);

			transform.m_Position += worldDelta;

			// Extract yaw from delta rotation and apply to entity
			// deltaRot encodes the rotation change; extract the Y-axis (yaw) component
			glm::vec3 deltaEuler = glm::degrees(glm::eulerAngles(deltaRot));
			transform.m_Rotation.y += deltaEuler.y;

			// Mark transform dirty so UpdateTransformRenderData picks it up
			VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
		}
	}

	// Zero out the root bone's position in the local transform so the skeleton
	// doesn't double-move. Keep the Y component if you want vertical motion
	// (jumps/crouches) to stay in the skeleton, or zero it for full root motion.
	// Here we zero XZ (horizontal) and keep Y in skeleton space.
	glm::vec3 skeletonPos = glm::vec3(0.0f, rootPos.y, 0.0f);
	glm::mat4 T = glm::translate(glm::mat4(1.0f), skeletonPos);
	glm::mat4 R = glm::toMat4(rootRot);  // keep full rotation in skeleton (only yaw delta was applied to entity)
	glm::mat4 S = glm::scale(glm::mat4(1.0f), rootScale);
	localTransforms[m_RootBoneIndex] = T * R * S;

	// Clear the loop-wrap flag (in case it wasn't cleared above — e.g. root motion disabled mid-frame)
	m_LoopJustWrapped = false;
}

// ════════════════════════════════════════════════════════════════
//  Internal: UpdateHierarchy
//  Propagate local → global transforms via parent chain (DFS)
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::UpdateHierarchy(std::vector<glm::mat4>& localTransforms)
{
	uint32_t boneCount = (uint32_t)m_Skeleton.bones.size();

	// When root motion is OFF, strip the root bone's animated translation so the
	// skeleton stays in place. Keep rotation and scale so the character still
	// turns/scales in-place via the animation.
	if (!m_RootMotionEnabled && m_RootBoneIndex >= 0 && m_RootBoneIndex < (int)boneCount)
	{
		glm::vec3 pos, scale, skew;
		glm::quat rot;
		glm::vec4 perspective;
		glm::decompose(localTransforms[m_RootBoneIndex], scale, rot, pos, skew, perspective);

		// Keep only Y (vertical) so the mesh doesn't float; zero XZ so it stays planted
		glm::vec3 clampedPos = glm::vec3(0.0f, pos.y, 0.0f);
		glm::mat4 T = glm::translate(glm::mat4(1.0f), clampedPos);
		glm::mat4 R = glm::toMat4(rot);
		glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
		localTransforms[m_RootBoneIndex] = T * R * S;
	}

	for (uint32_t b = 0; b < boneCount; b++)
	{
		m_Skeleton.bones[b].localTransform = localTransforms[b];
	}

	// Process bones in order — works because parents always have lower IDs than children
	// (standard Assimp bone ordering)
	for (uint32_t b = 0; b < boneCount; b++)
	{
		BoneInfo& bone = m_Skeleton.bones[b];
		if (bone.parentIndex >= 0 && bone.parentIndex < (int)boneCount)
		{
			bone.globalTransform = m_Skeleton.bones[bone.parentIndex].globalTransform * bone.localTransform;
		}
		else
		{
			// Root bone: apply global inverse transform
			bone.globalTransform = bone.localTransform;
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  Internal: BuildFinalMatrices
//  finalMatrix[i] = globalInverse * globalTransform[i] * offsetMatrix[i]
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::BuildFinalMatrices()
{
	uint32_t boneCount = (uint32_t)m_Skeleton.bones.size();
	uint32_t limit = (std::min)(boneCount, MAX_BONES);

	for (uint32_t i = 0; i < limit; i++)
	{
		const BoneInfo& bone = m_Skeleton.bones[i];
		m_BoneMatricesSSBO.boneMatrices[i] =
			m_Skeleton.globalInverseTransform * bone.globalTransform * bone.offsetMatrix;
	}

	// Fill remaining slots with identity
	for (uint32_t i = limit; i < MAX_BONES; i++)
		m_BoneMatricesSSBO.boneMatrices[i] = glm::mat4(1.0f);
}

// ════════════════════════════════════════════════════════════════
//  Internal: InterpolateKeyframes
//  Binary search for surrounding keyframes, then lerp/slerp
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
                                              float time,
                                              glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
{
	if (keyframes.empty())
	{
		outPos   = glm::vec3(0.0f);
		outRot   = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		outScale = glm::vec3(1.0f);
		return;
	}

	// Clamp: before first keyframe
	if (time <= keyframes.front().time || keyframes.size() == 1)
	{
		outPos   = keyframes.front().position;
		outRot   = keyframes.front().rotation;
		outScale = keyframes.front().scale;
		return;
	}

	// Clamp: after last keyframe
	if (time >= keyframes.back().time)
	{
		outPos   = keyframes.back().position;
		outRot   = keyframes.back().rotation;
		outScale = keyframes.back().scale;
		return;
	}

	// Binary search for the keyframe pair surrounding `time`
	// Find the first keyframe with time > our target time
	int lo = 0;
	int hi = (int)keyframes.size() - 1;
	int nextIdx = hi;

	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (keyframes[mid].time <= time)
			lo = mid + 1;
		else
		{
			nextIdx = mid;
			hi = mid - 1;
		}
	}

	int prevIdx = nextIdx - 1;
	if (prevIdx < 0) prevIdx = 0;

	const BoneKeyframe& kfA = keyframes[prevIdx];
	const BoneKeyframe& kfB = keyframes[nextIdx];

	float segmentDuration = kfB.time - kfA.time;
	float alpha = (segmentDuration > 0.0001f) ? (time - kfA.time) / segmentDuration : 0.0f;
	alpha = glm::clamp(alpha, 0.0f, 1.0f);

	// Interpolate
	outPos   = glm::mix(kfA.position, kfB.position, alpha);
	outRot   = glm::slerp(kfA.rotation, kfB.rotation, alpha);
	outScale = glm::mix(kfA.scale, kfB.scale, alpha);
}
