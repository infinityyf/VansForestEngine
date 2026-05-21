#pragma once

#include "VansBoneColliderBindingTypes.h"
#include <nlohmann/json.hpp>

#include <vector>

namespace VansEngine
{
	class VansBoneAttachmentSystem
	{
	public:
		static VansBoneAttachmentSystem& GetInstance();

		// ── 生命周期 ──────────────────────────────────────────────────
		void Initialize();
		void Shutdown();

		// ── 注册 / 查询 ───────────────────────────────────────────────
		void RegisterBindingSet(BoneColliderBindingSet&& set);
		void UnregisterBindingSet(VansGraphics::VansAnimationNode* animNode);
		BoneColliderBindingSet* FindBindingSet(VansGraphics::VansAnimationNode* animNode);

		// ── 每帧同步：动画骨骼 → TransformStore → PhysX dirty ─────────
		void Update();

		// ── 调试 / 序列化接口 ────────────────────────────────────────
		void DrawDebugGizmos(bool enabledOnly = true);
		nlohmann::json SerializeBindingSet(const BoneColliderBindingSet& set) const;

	private:
		VansBoneAttachmentSystem() = default;
		~VansBoneAttachmentSystem() = default;

		void SyncBinding(BoneColliderBinding& binding,
		                 VansGraphics::VansAnimationNode* animNode);
		void FreeBindingTransform(BoneColliderBinding& binding);

		static void DecomposeWorldMatrix(const glm::mat4& m,
		                                 glm::vec3& pos,
		                                 glm::vec3& rotDeg,
		                                 glm::vec3& scale);
		static glm::mat4 MakeTRS(const glm::vec3& pos,
		                         const glm::vec3& rotDeg,
		                         const glm::vec3& scale);

	private:
		std::vector<BoneColliderBindingSet> m_BindingSets;
	};
}
