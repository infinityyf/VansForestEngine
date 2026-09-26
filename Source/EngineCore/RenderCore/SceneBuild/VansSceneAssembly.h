#pragma once

#include "../VansScene.h"
#include "../../SceneCore/VansSceneObjectBuildPlan.h"

namespace VansGraphics
{
// 场景对象装配的唯一协调器。VansScene 只提供被装配的状态与领域能力，
// 装配顺序、批量预检和失败回滚不再作为 VansScene 成员公开。
class VansSceneAssembly
{
public:
	static VansSceneObjectBuildResult BuildObjects(
		VansScene& scene,
		VkDevice& device,
		const Vans::VansSceneObjectBuildPlan& objectBuildPlan,
		const std::string& projectRoot);

	static Vans::VansSceneEntityBatchResult CreateEntityBatch(
		VansScene& scene,
		VkDevice& device,
		const Vans::VansSceneObjectBuildPlan& buildPlan,
		const std::string& projectRoot);

private:
	explicit VansSceneAssembly(VansScene& scene)
		: m_Scene(scene),
		  m_SceneObjects(scene.m_SceneObjects),
		  m_SceneObjectCollectionGeneration(scene.m_SceneObjectCollectionGeneration),
		  m_RuntimeWorld(scene.m_RuntimeWorld),
		  m_GameplayRuntime(scene.m_GameplayRuntime),
		  m_AIWorld(scene.m_AIWorld),
		  m_TimelineRuntime(scene.m_TimelineRuntime),
		  m_CameraControlArbiter(scene.m_CameraControlArbiter),
		  m_TransformGraph(scene.m_TransformGraph),
		  m_AudioManager(scene.m_AudioManager)
	{
	}

	VansSceneObjectBuildResult BuildObjectsInternal(
		VkDevice& device,
		const Vans::VansSceneObjectBuildPlan& objectBuildPlan,
		const std::string& projectRoot);
	Vans::VansSceneEntityBatchResult CreateEntityBatchInternal(
		VkDevice& device,
		const Vans::VansSceneObjectBuildPlan& buildPlan,
		const std::string& projectRoot);

	VansScene& m_Scene;
	decltype(VansScene::m_SceneObjects)& m_SceneObjects;
	decltype(VansScene::m_SceneObjectCollectionGeneration)&
		m_SceneObjectCollectionGeneration;
	decltype(VansScene::m_RuntimeWorld)& m_RuntimeWorld;
	decltype(VansScene::m_GameplayRuntime)& m_GameplayRuntime;
	decltype(VansScene::m_AIWorld)& m_AIWorld;
	decltype(VansScene::m_TimelineRuntime)& m_TimelineRuntime;
	decltype(VansScene::m_CameraControlArbiter)& m_CameraControlArbiter;
	decltype(VansScene::m_TransformGraph)& m_TransformGraph;
	decltype(VansScene::m_AudioManager)& m_AudioManager;
};
}
