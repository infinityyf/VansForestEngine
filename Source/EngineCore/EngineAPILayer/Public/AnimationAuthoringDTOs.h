#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Vans::EditorAPI
{
	struct AnimationVector3DTO { float x = 0.0f, y = 0.0f, z = 0.0f; };
	struct AnimationQuaternionDTO { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };

	enum class AnimatorParamType { Float, Bool, Int, Trigger, Vector3, Quaternion };
	enum class CompareOp { Greater, Less, Equal, NotEqual, GreaterEqual, LessEqual };
	enum class AnimGraphNodeType
	{
		Entry, Output, Clip, Blend, Blend1D, IfCondition, Switch, AdditiveBlend,
		SpeedScale, StateMachine, MotionMatching, Slot, TargetPoseInput, IK,
		TwoBoneIK, LookAt, FootPlacement
	};
	enum class AnimGraphPinType { Pose, Float, Bool, Int };
	enum class AnimGraphPinKind { Input, Output };
	enum class AnimatorGraphRole { Pose, TargetPostProcess };
	enum class IKSolverType { TwoBone, CCD, FABRIK, LookAt };
	enum class IKProfileType
	{
		Custom, HumanoidArm, HumanoidLeg, HumanoidSpine, HumanoidHead, Tail, Tentacle, Rope
	};
	enum class IKCoordinateSpace { Model, World, Bone, ParentBone };
	enum class JointConstraintType { None, BallSocket, Hinge, AngleLimit, TwistLimit, Locked };
	enum class VansAnimationLayerKind { Base, Overlay };
	enum class VansLayerBlendMode { Override, Additive };
	enum class VansRotationBlendSpace { Local, Mesh };
	enum class VansAdditiveReferenceMode { BindPose, FirstFrame, ClipTime, ReferenceClip };
	enum class VansLayerRootMotionMode { Ignore, Base, BlendByRootWeight, Override };
	enum class VansLayerCurveMode { BaseOnly, Override, Blend, Normalize, Min, Max };
	enum class VansLayerEventMode { Ignore, ActiveOnly, Always };
	enum class VansLayerNodeTrackMode { Ignore, Override };
	enum class VansLayerSyncMode { Independent, NormalizedTime, MarkerSync, SyncedGraph };
	enum class VansSlotConcurrency { Replace, Queue, Reject };

	struct AnimatorParameterDTO
	{
		std::string name;
		AnimatorParamType type = AnimatorParamType::Float;
		float floatVal = 0.0f;
		bool boolVal = false;
		int intVal = 0;
		AnimationVector3DTO vec3Val;
		AnimationQuaternionDTO quatVal;
	};

	struct AnimatorClipRefDTO
	{
		std::string name;
		std::string assetGuid;
		std::string pathHint;
	};

	struct TransitionConditionDTO
	{
		std::string paramName;
		CompareOp op = CompareOp::Equal;
		float floatVal = 0.0f;
		bool boolVal = false;
		int intVal = 0;
	};

	struct AnimatorStateDTO
	{
		std::string name;
		std::string clipName;
		float speed = 1.0f;
		bool loop = true;
		bool rootMotion = false;
		float startTime = 0.0f;
		float endTime = -1.0f;
	};

	struct AnimatorTransitionDTO
	{
		std::string fromState;
		std::string toState;
		float blendDuration = 0.2f;
		bool hasExitTime = false;
		float exitTime = 1.0f;
		std::vector<TransitionConditionDTO> conditions;
	};

	struct JointConstraintDTO
	{
		JointConstraintType type = JointConstraintType::None;
		AnimationVector3DTO localXAxis{ 1.0f, 0.0f, 0.0f };
		AnimationVector3DTO localYAxis{ 0.0f, 1.0f, 0.0f };
		AnimationVector3DTO localZAxis{ 0.0f, 0.0f, 1.0f };
		float minAngleX = -180.0f, maxAngleX = 180.0f;
		float minAngleY = -180.0f, maxAngleY = 180.0f;
		float minAngleZ = -180.0f, maxAngleZ = 180.0f;
		float coneAngleDeg = 60.0f;
		float stiffness = 1.0f;
		AnimationQuaternionDTO restRotation;
	};

	struct IKBoneLinkDTO
	{
		int boneIndex = -1;
		std::string boneName;
		JointConstraintDTO constraint;
		float stiffnessWeight = 1.0f;
		bool isEffector = false;
	};

	struct IKChainDefinitionDTO
	{
		std::string chainName;
		IKSolverType solverType = IKSolverType::CCD;
		IKProfileType profileType = IKProfileType::Custom;
		std::vector<IKBoneLinkDTO> bones;
		int maxIterations = 15;
		float positionTolerance = 0.0005f;
		float rotationTolerance = 0.01f;
		AnimationVector3DTO poleVector{ 0.0f, 0.0f, -1.0f };
		float poleWeight = 0.0f;
		IKCoordinateSpace poleSpace = IKCoordinateSpace::Model;
		int poleReferenceBoneIndex = -1;
		std::string poleReferenceBoneName;
		bool enableRotationTarget = false;
		float rotationWeight = 0.0f;
		bool maintainEffectorGlobalRotation = false;
		bool allowStretch = false;
		float startStretchRatio = 1.0f;
		float maxStretchScale = 1.2f;
		int solvePriority = 0;
	};

	struct FootPlacementBoneNamesDTO
	{
		std::string pelvis = "pelvis";
		std::string leftHip = "thigh_l";
		std::string leftKnee = "calf_l";
		std::string leftFoot = "foot_l";
		std::string rightHip = "thigh_r";
		std::string rightKnee = "calf_r";
		std::string rightFoot = "foot_r";
	};

	struct FootPlacementSettingsDTO
	{
		bool enabled = false;
		float probeOriginHeight = 0.45f;
		float probeLength = 1.10f;
		float footHalfLength = 0.18f;
		float footHalfWidth = 0.08f;
		float ankleHeight = 0.08f;
		float fullContactHeight = 0.08f;
		float contactFadeHeight = 0.28f;
		float maxStepUp = 0.35f;
		float maxStepDown = 0.55f;
		float maxSlopeDeg = 55.0f;
		float pelvisMaxDrop = 0.30f;
		float pelvisSmoothTime = 0.08f;
		float offsetSmoothTime = 0.04f;
		float normalSmoothTime = 0.06f;
		float weightSmoothTime = 0.04f;
		float globalWeightSmoothTime = 0.08f;
		float ikWeight = 1.0f;
		float rotationWeight = 0.70f;
		float maxLegExtensionRatio = 0.98f;
		float poleSmoothTime = 0.05f;
		AnimationVector3DTO kneePoleModelDir{ 0.0f, 0.0f, 1.0f };
		float kneePoleModelWeight = 0.0f;
		bool debugVisualization = false;
		std::uint32_t collisionMask = 0xffffffffu;
		std::string airborneParameter = "IsAirborne";
		FootPlacementBoneNamesDTO bones;
	};

	struct AnimGraphPinDTO
	{
		int pinIndex = 0;
		std::string name;
		AnimGraphPinType type = AnimGraphPinType::Pose;
		AnimGraphPinKind kind = AnimGraphPinKind::Input;
	};

	struct AnimationNodeDTO
	{
		int m_NodeId = -1;
		std::string m_Name;
		AnimGraphNodeType m_Type = AnimGraphNodeType::Entry;
		float m_EditorPosX = 0.0f;
		float m_EditorPosY = 0.0f;

		std::string m_ClipName;
		float m_Speed = 1.0f;
		bool m_Loop = true;
		std::string m_ParamName;
		float m_FixedAlpha = 0.5f;
		bool m_UseParam = true;
		std::vector<float> m_Thresholds;
		CompareOp m_CompareOp = CompareOp::Greater;
		float m_FloatVal = 0.0f;
		bool m_BoolVal = false;
		int m_IntVal = 0;
		int m_CaseCount = 2;
		float m_FixedWeight = 1.0f;
		float m_FixedSpeed = 1.0f;
		std::vector<AnimatorStateDTO> m_States;
		std::vector<AnimatorTransitionDTO> m_Transitions;
		std::string m_DefaultStateName;
		bool m_EnableFallbackInput = true;
		std::string m_SlotId;

		IKChainDefinitionDTO m_Chain;
		std::string m_TargetPosParamName;
		std::string m_TargetRotParamName;
		std::string m_WeightParamName;
		bool m_UseFixedTarget = false;
		AnimationVector3DTO m_FixedTargetPos;
		AnimationQuaternionDTO m_FixedTargetRot;
		IKCoordinateSpace m_TargetPositionSpace = IKCoordinateSpace::Model;
		IKCoordinateSpace m_TargetRotationSpace = IKCoordinateSpace::Model;
		std::string m_TargetReferenceBoneName;

		std::string m_RootBoneName;
		std::string m_MidBoneName;
		std::string m_TipBoneName;
		bool m_UseLegProfile = false;
		bool m_IsRightSide = true;
		float m_HingeMinAngle = 0.0f;
		float m_HingeMaxAngle = 150.0f;
		float m_ConeAngle = 60.0f;
		bool m_UsePoleVector = false;
		AnimationVector3DTO m_PoleVector{ 0.0f, 0.0f, -1.0f };
		float m_PoleWeight = 1.0f;
		bool m_EnableRotationTarget = false;
		float m_RotationWeight = 1.0f;
		IKCoordinateSpace m_PoleSpace = IKCoordinateSpace::Model;
		std::string m_PoleReferenceBoneName;
		bool m_MaintainEffectorGlobalRotation = false;
		bool m_AllowStretch = false;
		float m_StartStretchRatio = 1.0f;
		float m_MaxStretchScale = 1.2f;

		std::vector<std::string> m_BoneNames;
		std::vector<float> m_BoneWeights;
		float m_MaxAnglePerBoneDeg = 80.0f;
		AnimationVector3DTO m_ForwardAxis{ 0.0f, 0.0f, -1.0f };
		AnimationVector3DTO m_WorldForward;
		AnimationVector3DTO m_ModelUp{ 0.0f, 1.0f, 0.0f };
		float m_UpWeight = 1.0f;
		FootPlacementSettingsDTO m_Settings;

		int GetNodeId() const { return m_NodeId; }
		AnimGraphNodeType GetType() const { return m_Type; }
		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }
		std::vector<AnimGraphPinDTO> GetPins() const;
		static const char* TypeToString(AnimGraphNodeType type);
	};

	struct AnimGraphLinkDTO
	{
		int linkId = -1;
		int fromNodeId = -1;
		int fromPinIndex = 0;
		int toNodeId = -1;
		int toPinIndex = 0;
	};

	struct AnimationGraphDTO
	{
		std::unordered_map<int, std::unique_ptr<AnimationNodeDTO>> nodes;
		std::vector<AnimGraphLinkDTO> links;
		int entryNodeId = -1;
		int outputNodeId = -1;
		int nextNodeId = 1;
		int nextLinkId = 1;

		AnimationNodeDTO* GetNode(int id);
		const AnimationNodeDTO* GetNode(int id) const;
		const std::unordered_map<int, std::unique_ptr<AnimationNodeDTO>>& GetNodes() const { return nodes; }
		std::unordered_map<int, std::unique_ptr<AnimationNodeDTO>>& GetNodes() { return nodes; }
		const std::vector<AnimGraphLinkDTO>& GetLinks() const { return links; }
		int GetOutputNodeId() const { return outputNodeId; }
		int AddNode(std::unique_ptr<AnimationNodeDTO> node);
		void RemoveNode(int id);
		int AddLink(int fromNode, int fromPin, int toNode, int toPin);
		void RemoveLink(int id);
		static std::unique_ptr<AnimationNodeDTO> CreateNodeByType(AnimGraphNodeType type);
		static std::unique_ptr<AnimationGraphDTO> Clone(const AnimationGraphDTO& source);
	};

	struct AnimatorGraphDTO
	{
		using Role = AnimatorGraphRole;

		std::string id;
		std::string name;
		AnimatorGraphRole role = AnimatorGraphRole::Pose;
		std::unique_ptr<AnimationGraphDTO> graph;
	};

	struct AnimationLayerDTO
	{
		std::string id, name, graphId, maskGuid, maskPathHint;
		VansAnimationLayerKind kind = VansAnimationLayerKind::Overlay;
		VansLayerBlendMode blendMode = VansLayerBlendMode::Override;
		VansRotationBlendSpace rotationSpace = VansRotationBlendSpace::Local;
		VansAdditiveReferenceMode additiveReference = VansAdditiveReferenceMode::BindPose;
		std::string referenceClipName;
		float referenceTime = 0.0f;
		std::string weightParameter;
		float fixedWeight = 1.0f;
		bool useWeightParameter = false;
		float weightSmoothingTime = 0.0f;
		VansLayerRootMotionMode rootMotion = VansLayerRootMotionMode::Ignore;
		VansLayerCurveMode curves = VansLayerCurveMode::Blend;
		VansLayerEventMode events = VansLayerEventMode::ActiveOnly;
		VansLayerNodeTrackMode nodeTracks = VansLayerNodeTrackMode::Ignore;
		VansLayerSyncMode sync = VansLayerSyncMode::Independent;
		std::string syncLeaderLayerId;
		float eventWeightThreshold = 0.01f;
		bool enabled = true;
		bool updateWhenWeightIsZero = true;
	};

	struct AnimationSlotDTO
	{
		std::string id, name, layerId;
		int slotNodeId = -1;
		VansSlotConcurrency concurrency = VansSlotConcurrency::Replace;
		std::uint32_t maxQueueDepth = 4;
		float defaultBlendIn = 0.08f;
		float defaultBlendOut = 0.12f;
		bool interruptible = true;
	};

	struct AnimatorEditorSettingsDTO
	{
		std::string previewModelGuid;
		std::string previewModelPathHint;
	};

	struct AnimatorDocumentDTO
	{
		std::string name;
		std::vector<AnimatorParameterDTO> parameters;
		std::vector<AnimatorClipRefDTO> clipRefs;
		std::vector<AnimatorGraphDTO> graphs;
		std::vector<AnimationLayerDTO> layers;
		std::vector<AnimationSlotDTO> slots;
		AnimatorEditorSettingsDTO editor;
		AnimationGraphDTO* FindGraph(const std::string& id);
		const AnimationGraphDTO* FindGraph(const std::string& id) const;
	};

	struct AnimatorDocumentDecodeResult
	{
		bool success = false;
		std::string message;
		std::unique_ptr<AnimatorDocumentDTO> document;
	};
	struct AnimatorDocumentEncodeResult
	{
		bool success = false;
		std::string message;
		std::string canonicalJson;
	};

	enum class BoneMaskRuleMode { Include, Exclude };
	enum class BoneMaskFalloff { Constant, Linear, SmoothStep };
	enum class BoneMaskDiagnosticSeverity { Warning, Error };
	struct BoneMaskRuleDTO
	{
		std::string id;
		BoneMaskRuleMode mode = BoneMaskRuleMode::Include;
		std::string rootBone;
		bool includeDescendants = true;
		int maxDepth = -1;
		float rootWeight = 1.0f;
		float endWeight = 1.0f;
		BoneMaskFalloff falloff = BoneMaskFalloff::Linear;
	};
	struct BoneMaskDocumentDTO
	{
		std::string id, name, previewSkeletonGuid, previewSkeletonPathHint;
		float defaultWeight = 0.0f;
		std::vector<BoneMaskRuleDTO> branchRules;
		std::unordered_map<std::string, float> explicitWeights;
		std::vector<std::string> editorExpandedBones;
	};
	struct BoneMaskDiagnosticDTO
	{
		BoneMaskDiagnosticSeverity severity = BoneMaskDiagnosticSeverity::Warning;
		std::string ruleId, message;
	};
	struct BoneMaskCompileResult
	{
		bool valid = false, allZero = true, allOne = false;
		float rootWeight = 0.0f;
		std::vector<float> weights;
		std::vector<std::uint32_t> activeBones;
		std::vector<BoneMaskDiagnosticDTO> diagnostics;
		std::string message;
	};
	struct BoneMaskDocumentDecodeResult
	{
		bool success = false;
		std::string message;
		BoneMaskDocumentDTO document;
	};
	struct BoneMaskDocumentEncodeResult
	{
		bool success = false;
		std::string message;
		std::string canonicalJson;
	};

	inline const char* AnimationNodeDTO::TypeToString(AnimGraphNodeType type)
	{
		switch (type)
		{
		case AnimGraphNodeType::Entry: return "Entry";
		case AnimGraphNodeType::Output: return "Output";
		case AnimGraphNodeType::Clip: return "Clip";
		case AnimGraphNodeType::Blend: return "Blend";
		case AnimGraphNodeType::Blend1D: return "Blend1D";
		case AnimGraphNodeType::IfCondition: return "IfCondition";
		case AnimGraphNodeType::Switch: return "Switch";
		case AnimGraphNodeType::AdditiveBlend: return "AdditiveBlend";
		case AnimGraphNodeType::SpeedScale: return "SpeedScale";
		case AnimGraphNodeType::StateMachine: return "StateMachine";
		case AnimGraphNodeType::MotionMatching: return "MotionMatching";
		case AnimGraphNodeType::Slot: return "Slot";
		case AnimGraphNodeType::TargetPoseInput: return "TargetPoseInput";
		case AnimGraphNodeType::IK: return "IK";
		case AnimGraphNodeType::TwoBoneIK: return "TwoBoneIK";
		case AnimGraphNodeType::LookAt: return "LookAt";
		case AnimGraphNodeType::FootPlacement: return "FootPlacement";
		}
		return "Unknown";
	}

	inline std::vector<AnimGraphPinDTO> AnimationNodeDTO::GetPins() const
	{
		auto input = [](int index, std::string name)
			{ return AnimGraphPinDTO{ index, std::move(name), AnimGraphPinType::Pose, AnimGraphPinKind::Input }; };
		auto output = [](int index, std::string name)
			{ return AnimGraphPinDTO{ index, std::move(name), AnimGraphPinType::Pose, AnimGraphPinKind::Output }; };
		switch (m_Type)
		{
		case AnimGraphNodeType::Entry: return { output(0, "Pose") };
		case AnimGraphNodeType::Output: return { input(0, "Pose") };
		case AnimGraphNodeType::Clip: return { output(0, "Pose") };
		case AnimGraphNodeType::Blend: return { input(0, "A"), input(1, "B"), output(0, "Pose") };
		case AnimGraphNodeType::Blend1D:
		{
			std::vector<AnimGraphPinDTO> pins;
			for (int index = 0; index < static_cast<int>(m_Thresholds.size()); ++index)
				pins.push_back(input(index, "Pose " + std::to_string(index)));
			pins.push_back(output(0, "Pose"));
			return pins;
		}
		case AnimGraphNodeType::IfCondition: return { input(0, "True"), input(1, "False"), output(0, "Pose") };
		case AnimGraphNodeType::Switch:
		{
			std::vector<AnimGraphPinDTO> pins;
			for (int index = 0; index < std::max(1, m_CaseCount); ++index)
				pins.push_back(input(index, "Case " + std::to_string(index)));
			pins.push_back(output(0, "Pose"));
			return pins;
		}
		case AnimGraphNodeType::AdditiveBlend: return { input(0, "Base"), input(1, "Additive"), output(0, "Pose") };
		case AnimGraphNodeType::SpeedScale:
		case AnimGraphNodeType::IK:
		case AnimGraphNodeType::TwoBoneIK:
		case AnimGraphNodeType::LookAt:
		case AnimGraphNodeType::FootPlacement: return { input(0, "Pose"), output(0, "Pose") };
		case AnimGraphNodeType::StateMachine: return { output(0, "Pose") };
		case AnimGraphNodeType::MotionMatching:
		case AnimGraphNodeType::Slot: return { input(0, "Fallback Pose"), output(0, "Pose") };
		case AnimGraphNodeType::TargetPoseInput: return { output(0, "Target Pose") };
		}
		return {};
	}

	inline AnimationNodeDTO* AnimationGraphDTO::GetNode(int id)
	{
		auto found = nodes.find(id); return found == nodes.end() ? nullptr : found->second.get();
	}
	inline const AnimationNodeDTO* AnimationGraphDTO::GetNode(int id) const
	{
		auto found = nodes.find(id); return found == nodes.end() ? nullptr : found->second.get();
	}
	inline std::unique_ptr<AnimationNodeDTO> AnimationGraphDTO::CreateNodeByType(AnimGraphNodeType type)
	{
		auto node = std::make_unique<AnimationNodeDTO>();
		node->m_Type = type;
		node->m_Name = AnimationNodeDTO::TypeToString(type);
		if (type == AnimGraphNodeType::Blend1D) node->m_Thresholds = { 0.0f, 1.0f };
		if (type == AnimGraphNodeType::Slot) node->m_EnableFallbackInput = true;
		return node;
	}
	inline int AnimationGraphDTO::AddNode(std::unique_ptr<AnimationNodeDTO> node)
	{
		if (!node) return -1;
		if (node->m_Type == AnimGraphNodeType::Entry && entryNodeId >= 0) return -1;
		if (node->m_Type == AnimGraphNodeType::Output && outputNodeId >= 0) return -1;
		const int id = nextNodeId++;
		node->m_NodeId = id;
		if (node->m_Type == AnimGraphNodeType::Entry) entryNodeId = id;
		if (node->m_Type == AnimGraphNodeType::Output) outputNodeId = id;
		nodes.emplace(id, std::move(node));
		return id;
	}
	inline void AnimationGraphDTO::RemoveNode(int id)
	{
		nodes.erase(id);
		links.erase(std::remove_if(links.begin(), links.end(),
			[&](const AnimGraphLinkDTO& link) { return link.fromNodeId == id || link.toNodeId == id; }), links.end());
		if (entryNodeId == id) entryNodeId = -1;
		if (outputNodeId == id) outputNodeId = -1;
	}
	inline int AnimationGraphDTO::AddLink(int fromNode, int fromPin, int toNode, int toPin)
	{
		const AnimationNodeDTO* source = GetNode(fromNode);
		const AnimationNodeDTO* target = GetNode(toNode);
		if (!source || !target || fromNode == toNode) return -1;
		const auto sourcePins = source->GetPins();
		const auto targetPins = target->GetPins();
		const AnimGraphPinDTO* outputPin = nullptr;
		const AnimGraphPinDTO* inputPin = nullptr;
		for (const auto& pin : sourcePins)
			if (pin.kind == AnimGraphPinKind::Output && pin.pinIndex == fromPin) { outputPin = &pin; break; }
		for (const auto& pin : targetPins)
			if (pin.kind == AnimGraphPinKind::Input && pin.pinIndex == toPin) { inputPin = &pin; break; }
		if (!outputPin || !inputPin || outputPin->type != inputPin->type) return -1;
		for (const auto& link : links)
			if (link.toNodeId == toNode && link.toPinIndex == toPin) return -1;
		std::vector<int> pending{ toNode };
		std::unordered_set<int> visited;
		while (!pending.empty())
		{
			const int current = pending.back(); pending.pop_back();
			if (current == fromNode) return -1;
			if (!visited.insert(current).second) continue;
			for (const auto& link : links)
				if (link.fromNodeId == current) pending.push_back(link.toNodeId);
		}
		const int id = nextLinkId++;
		links.push_back({ id, fromNode, fromPin, toNode, toPin });
		return id;
	}
	inline void AnimationGraphDTO::RemoveLink(int id)
	{
		links.erase(std::remove_if(links.begin(), links.end(),
			[&](const AnimGraphLinkDTO& link) { return link.linkId == id; }), links.end());
	}
	inline std::unique_ptr<AnimationGraphDTO> AnimationGraphDTO::Clone(const AnimationGraphDTO& source)
	{
		auto clone = std::make_unique<AnimationGraphDTO>();
		clone->links = source.links;
		clone->entryNodeId = source.entryNodeId;
		clone->outputNodeId = source.outputNodeId;
		clone->nextNodeId = source.nextNodeId;
		clone->nextLinkId = source.nextLinkId;
		for (const auto& [id, node] : source.nodes)
			clone->nodes.emplace(id, std::make_unique<AnimationNodeDTO>(*node));
		return clone;
	}
	inline AnimationGraphDTO* AnimatorDocumentDTO::FindGraph(const std::string& id)
	{
		for (AnimatorGraphDTO& graph : graphs) if (graph.id == id) return graph.graph.get();
		return nullptr;
	}
	inline const AnimationGraphDTO* AnimatorDocumentDTO::FindGraph(const std::string& id) const
	{
		for (const AnimatorGraphDTO& graph : graphs) if (graph.id == id) return graph.graph.get();
		return nullptr;
	}
}
