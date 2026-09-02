#pragma once

#include "../../ScriptCore/VansTransform.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansTransformReparentMode : std::uint8_t
{
	KeepWorld,
	KeepLocal,
	Snap
};

enum class VansTransformAnchorKind : std::uint8_t
{
	Bone,
	Socket
};

struct VansTransformAnchorHandle
{
	std::uint64_t instanceId = 0;
	std::uint32_t instanceGeneration = 0;
	VansTransformAnchorKind kind = VansTransformAnchorKind::Bone;
	std::string anchorGuid;

	bool IsValid() const
	{
		return instanceId != 0 && instanceGeneration != 0 && !anchorGuid.empty();
	}
};

class IVansTransformAnchorProvider
{
public:
	virtual ~IVansTransformAnchorProvider() = default;
	virtual bool ResolveModelSpaceTransform(
		const VansTransformAnchorHandle& handle,
		glm::mat4& outModelTransform,
		std::uint64_t& outPoseRevision) const = 0;
};

struct VansLocalTransform
{
	glm::vec3 position{ 0.0f };
	glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	glm::vec3 scale{ 1.0f };

	glm::mat4 ToMatrix() const;
	static bool TryFromMatrix(const glm::mat4& matrix, VansLocalTransform& outTransform);
};

struct VansTransformGraphLink
{
	std::uint32_t childTransformId = UINT32_MAX;
	std::uint32_t parentTransformId = UINT32_MAX;
	bool usesAnchor = false;
	VansTransformAnchorHandle anchor;
};

class VansTransformGraph
{
public:
	explicit VansTransformGraph(const IVansTransformAnchorProvider* anchorProvider = nullptr)
		: m_AnchorProvider(anchorProvider)
	{}

	void SetAnchorProvider(const IVansTransformAnchorProvider* provider)
	{
		m_AnchorProvider = provider;
	}

	bool SetParent(
		std::uint32_t childTransformId,
		std::uint32_t parentTransformId,
		VansTransformReparentMode mode = VansTransformReparentMode::KeepWorld);
	bool SetAnchor(
		std::uint32_t childTransformId,
		std::uint32_t ownerTransformId,
		VansTransformAnchorHandle anchor,
		VansTransformReparentMode mode = VansTransformReparentMode::KeepWorld);
	bool SetAnchorWithLocalTransform(
		std::uint32_t childTransformId,
		std::uint32_t ownerTransformId,
		VansTransformAnchorHandle anchor,
		const VansLocalTransform& localTransform);
	bool ClearParent(
		std::uint32_t childTransformId,
		VansTransformReparentMode mode = VansTransformReparentMode::KeepWorld);

	bool HasParent(std::uint32_t childTransformId) const;
	std::uint32_t GetParent(std::uint32_t childTransformId) const;
	const VansTransformGraphLink* GetLink(std::uint32_t childTransformId) const;
	std::vector<VansTransformGraphLink> GetAllLinks() const;

	bool SetLocalTransform(std::uint32_t transformId, const VansLocalTransform& localTransform);
	bool SetWorldTransform(std::uint32_t transformId, const glm::mat4& worldTransform);
	bool TryGetLocalTransform(std::uint32_t transformId, VansLocalTransform& outTransform) const;
	void MarkWorldDirty(std::uint32_t transformId);

	bool Resolve();
	void Clear();
	const std::string& GetLastError() const { return m_LastError; }

private:
	struct Node
	{
		VansTransformGraphLink link;
		VansLocalTransform local;
		bool importWorldBeforeResolve = false;
		bool anchorWasUnresolved = false;
	};

	bool ValidateTransformId(std::uint32_t transformId) const;
	bool WouldCreateCycle(std::uint32_t childTransformId, std::uint32_t parentTransformId) const;
	bool ResolveParentWorld(const Node& node, glm::mat4& outParentWorld) const;
	bool ImportLocalFromCurrentWorld(Node& node);
	void RebuildTopologicalOrder();
	static bool WriteResolvedWorld(std::uint32_t transformId, const glm::mat4& worldTransform);

	const IVansTransformAnchorProvider* m_AnchorProvider = nullptr;
	std::unordered_map<std::uint32_t, Node> m_Nodes;
	std::vector<std::uint32_t> m_TopologicalOrder;
	std::string m_LastError;
};
}
