#pragma once

#include "AnimationAuthoringDTOs.h"

#include "EngineIds.h"

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Vans::EditorAPI
{
	using EditorTextureHandle = void*;
	using UIDocumentId = std::uint64_t;
	using UIPreviewId = std::uint64_t;

	struct UIDocumentOpenResult
	{
		bool success = false;
		UIDocumentId documentId = 0;
		std::string sourcePath;
		std::string error;
	};

	struct UIScreenEventSummary
	{
		std::string source;
		std::string eventName;
		std::string action;
	};

	struct UIScreenPerformanceBudgetSummary
	{
		std::uint32_t maxDrawCalls = 0;
		std::uint32_t maxTextureMemoryMB = 0;
		double maxLayoutMs = 0.0;
		std::uint32_t maxBindingUpdatesPerFrame = 0;
		std::uint32_t maxAnimations = 0;
	};

	struct UIDocumentSnapshot
	{
		bool valid = false;
		UIDocumentId documentId = 0;
		std::string sourcePath;
		bool visible = false;
		std::string assetKind;
		std::string name;
		std::string xamlPath;
		std::string layer;
		std::int32_t zOrder = 0;
		std::vector<std::string> themes;
		std::vector<std::string> tokens;
		std::vector<std::string> localization;
		std::vector<std::string> dependencies;
		std::vector<UIScreenEventSummary> events;
		UIScreenPerformanceBudgetSummary performanceBudget;
	};

	struct UIDiagnosticsSnapshot
	{
		bool available = false;
		std::vector<std::string> messages;
	};

	struct UIPreviewRequest
	{
		UIDocumentId documentId = 0;
		std::uint32_t width = 1280;
		std::uint32_t height = 720;
	};

	struct UIPreviewResult
	{
		bool success = false;
		UIPreviewId previewId = 0;
		EditorTextureHandle texture = nullptr;
		std::string message;
	};

	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct Vec4
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 0.0f;
	};

	struct Ray
	{
		Vec3 origin;
		Vec3 direction;
	};

	struct AudioBusDebugState
	{
		std::string name;
		float gain = 1.0f;
		float duckingGain = 1.0f;
		float effectiveGain = 1.0f;
		int activeVoiceCount = 0;
		bool muted = false;
		bool soloed = false;
	};

	struct AudioDuckingRuleDebugState
	{
		std::string triggerBusName;
		std::string targetBusName;
		float targetGain = 1.0f;
		float attackSeconds = 0.0f;
		float releaseSeconds = 0.0f;
		bool enabled = false;
		bool active = false;
	};

	struct AudioSourceDebugState
	{
		std::string objectName;
		std::string sourceName;
		std::string busName;
		Vec3 position;
		float listenerDistance = 0.0f;
		float volume = 1.0f;
		float effectiveBusGain = 1.0f;
		float reverbSend = 0.0f;
		float occlusionGain = 1.0f;
		float occlusionHighFrequencyGain = 1.0f;
		std::string occlusionMaterial = "custom";
		float occlusionMaterialThickness = 1.0f;
		float occlusionQueryTimer = 0.0f;
		bool bound = false;
		bool objectActive = false;
		bool componentEnabled = false;
		bool playing = false;
		bool paused = false;
		bool spatial = false;
		bool usesInstance = false;
		bool usesPrivateNode = false;
		bool hardwareVoiceActive = false;
		bool virtualized = false;
		bool occlusionEnabled = false;
		bool occlusionBlocked = false;
		bool dopplerEnabled = false;
	};

	struct AudioReverbZoneDebugState
	{
		std::string objectName;
		std::string componentType = "AudioReverbZone";
		std::string shape;
		std::string preset;
		Vec3 position;
		float blend = 0.0f;
		float wetGain = 0.0f;
		float effectiveWetGain = 0.0f;
		int priority = 0;
		bool affectsListener = false;
		bool selected = false;
	};

	struct AudioBusDebugSnapshot
	{
		bool available = false;
		bool audioSystemInitialized = false;
		bool efxSupported = false;
		std::string defaultReverbPreset = "generic";
		float defaultReverbWetGain = 0.0f;
		bool listenerAvailable = false;
		Vec3 listenerPosition;
		int sourceCount = 0;
		int boundSourceCount = 0;
		int playingSourceCount = 0;
		int spatialSourceCount = 0;
		int virtualizedSourceCount = 0;
		int hardwareVoiceActiveCount = 0;
		int hardwareVoiceSuspendedThisFrame = 0;
		int hardwareVoiceResumedThisFrame = 0;
		int activeSourceLeaseCount = 0;
		int pooledSourceCount = 0;
		int maxActiveVoices = 32;
		int reverbZoneCount = 0;
		int affectingReverbZoneCount = 0;
		std::vector<AudioBusDebugState> buses;
		std::vector<AudioDuckingRuleDebugState> duckingRules;
		std::vector<AudioSourceDebugState> sources;
		std::vector<AudioReverbZoneDebugState> reverbZones;
	};

	enum class RuntimeTransformSpace : std::uint8_t
	{
		World,
		Local,
		Model
	};

	struct RuntimeTransformSnapshot
	{
		bool available = false;
		std::string entityGuid;
		RuntimeTransformSpace space = RuntimeTransformSpace::World;
		Vec3 position;
		Vec3 rotationDegrees;
		Vec3 scale = { 1.0f, 1.0f, 1.0f };
	};

	struct RuntimeTransformEdit
	{
		std::string entityGuid;
		RuntimeTransformSpace space = RuntimeTransformSpace::World;
		Vec3 position;
		Vec3 rotationDegrees;
		Vec3 scale = { 1.0f, 1.0f, 1.0f };
		bool writePosition = true;
		bool writeRotation = true;
		bool writeScale = true;
	};

	struct RuntimeTransformEditResult
	{
		bool applied = false;
		std::string message;
		RuntimeTransformSnapshot localTransform;
		RuntimeTransformSnapshot worldTransform;
	};

	enum class RuntimePreviewLightType
	{
		Directional,
		Point,
		Spot,
		Rect
	};

	struct RuntimeLightEdit
	{
		RuntimePreviewLightType type = RuntimePreviewLightType::Directional;
		std::string entityGuid;
		bool writeColor = false;
		bool writeIntensity = false;
		bool writeRadius = false;
		bool writeInnerCutoff = false;
		bool writeOuterCutoff = false;
		bool writeRectWidth = false;
		bool writeRectHeight = false;
		bool writeRectRange = false;
		bool writeRectTwoSided = false;
		bool writeRectShadow = false;
		Vec3 color;
		float intensity = 0.0f;
		float radius = 0.0f;
		float innerCutoffRadians = 0.0f;
		float outerCutoffRadians = 0.0f;
		float rectWidth = 0.0f;
		float rectHeight = 0.0f;
		float rectRange = 0.0f;
		float rectTwoSided = 0.0f;
		float rectShadowIndex = -1.0f;
	};

	struct RuntimeComponentEnabledEdit
	{
		std::string entityGuid;
		std::string componentGuid;
		std::string componentType;
		bool enabled = true;
	};

	struct RuntimeRendererMaterialOverrideEdit
	{
		std::string entityGuid;
		std::string slot;
		std::string materialGuid;
	};

	enum class RuntimeParentKind : std::uint8_t
	{
		None,
		Entity,
		Bone,
		Socket
	};

	struct RuntimeParentReference
	{
		RuntimeParentKind kind = RuntimeParentKind::None;
		std::string entityGuid;
		std::string animationComponentGuid;
		std::string anchorGuid;
	};

	enum class RuntimeReparentTransformPolicy : std::uint8_t
	{
		KeepWorld,
		KeepLocal,
		Snap
	};

	struct RuntimeEntityParentEdit
	{
		std::string entityGuid;
		RuntimeParentReference parent;
		RuntimeReparentTransformPolicy transformPolicy =
			RuntimeReparentTransformPolicy::KeepWorld;
	};

	struct RuntimeEntityNameEdit
	{
		std::string entityGuid;
		std::string name;
	};

	struct RuntimeEntityActiveEdit
	{
		std::string entityGuid;
		bool active = true;
	};

	struct RuntimeEntityPreviewChange
	{
		bool hasTransform = false;
		RuntimeTransformEdit transform;
		std::vector<RuntimeEntityNameEdit> nameEdits;
		std::vector<RuntimeEntityActiveEdit> activeEdits;
		std::vector<RuntimeEntityParentEdit> parentEdits;
		std::vector<RuntimeLightEdit> lights;
		std::vector<RuntimeComponentEnabledEdit> componentEnabled;
		std::vector<RuntimeRendererMaterialOverrideEdit> materialOverrides;

		bool Empty() const
		{
			return !hasTransform &&
				nameEdits.empty() &&
				activeEdits.empty() &&
				parentEdits.empty() &&
				lights.empty() &&
				componentEnabled.empty() &&
				materialOverrides.empty();
		}
	};

	struct RuntimeMultiMeshChildSnapshot
	{
		std::uint32_t submeshIndex = 0;
		std::string sourceNode;
		std::string sourceMaterial;
		std::string materialGuid;
	};

	struct RuntimeMultiMeshGroupSnapshot
	{
		std::string parentName;
		std::vector<RuntimeMultiMeshChildSnapshot> children;
	};

	enum class PropertyType
	{
		Unknown,
		Bool,
		Int,
		UInt,
		Float,
		Vec2,
		Vec3,
		Vec4,
		String,
		Guid,
		Enum
	};

	enum class RuntimeSceneLoadMode
	{
		Editor,
		Runtime
	};

	enum class RuntimeSceneLoadFinalState
	{
		Unchanged,
		Empty,
		Ready
	};

	struct RuntimeSceneLoadDiagnostic
	{
		std::string code;
		std::string message;
	};

	struct RuntimeSceneLoadResult
	{
		bool success = false;
		RuntimeSceneLoadMode requestedMode = RuntimeSceneLoadMode::Editor;
		RuntimeSceneLoadFinalState finalState = RuntimeSceneLoadFinalState::Unchanged;
		std::uint64_t contentRevision = 0;
		std::vector<RuntimeSceneLoadDiagnostic> diagnostics;

		explicit operator bool() const { return success; }
	};

	using PropertyValue = std::variant<
		std::monostate,
		bool,
		std::int32_t,
		std::uint32_t,
		float,
		Vec2,
		Vec3,
		Vec4,
		std::string>;

	struct PropertyEntry
	{
		std::string name;
		PropertyType type = PropertyType::Unknown;
		PropertyValue value;
		bool isReadOnly = false;
		std::vector<std::string> enumOptions;
	};

	struct RuntimeMaterialParameterEdit
	{
		std::string parameterPath;
		PropertyValue value;
	};

	struct RuntimeMaterialTextureEdit
	{
		std::string slot;
		std::string textureGuid;
	};

	struct RuntimeMaterialPreviewChange
	{
		std::string assetPath;
		std::vector<RuntimeMaterialParameterEdit> parameters;
		std::vector<RuntimeMaterialTextureEdit> textures;

		bool Empty() const { return assetPath.empty() || (parameters.empty() && textures.empty()); }
	};

	struct ComponentEntry
	{
		ComponentId id = InvalidComponentId;
		std::string typeName;
		bool isEnabled = true;
	};

	struct ComponentDataSnapshot
	{
		ComponentId id = InvalidComponentId;
		std::string typeName;
		std::vector<PropertyEntry> properties;
	};

	struct EntityEntry
	{
		EntityId id = InvalidEntityId;
		EntityId parentId = InvalidEntityId;
		std::string name;
		bool isActive = true;
		std::vector<ComponentEntry> components;
	};

	struct EntityDataSnapshot
	{
		EntityEntry entity;
		std::vector<ComponentDataSnapshot> components;
	};

	struct SceneDataSnapshot
	{
		std::string sceneName;
		std::vector<EntityEntry> entities;
	};

	enum class AssetType
	{
		Unknown,
		Model,
		Texture,
		Material,
		Shader,
		Audio,
		Video,
		Scene,
		Particle,
		AnimationClip,
		AnimatorController,
		AnimationRig,
		BoneMask,
		Timeline,
		ActionDefinition,
		ActionSet,
		GameplayEffect,
		GameplayCue,
		AttributeSet,
		TargetingPolicy,
		GameplayTagTree,
		PayloadSchema,
		ActionGraph,
		CameraRigProfile,
		CameraShakeProfile,
		GAFEditorLayout,
		ClothProfile,
		SkinProfile,
		PostProcessProfile,
		RagdollProfile,
		AudioReverbPreset,
		AudioBusSnapshot,
		AudioDuckingRules
	};

	enum class AssetQueryCapability
	{
		Any,
		SkeletalModel
	};

	struct AssetTypeFilter
	{
		AssetType type = AssetType::Unknown;
		bool includeUnknown = false;
		AssetQueryCapability requiredCapability = AssetQueryCapability::Any;
	};

	struct AssetEntry
	{
		AssetId id = InvalidAssetId;
		std::string guid;
		std::string name;
		std::string relativePath;
		AssetType type = AssetType::Unknown;
	};

	struct AssetMetaSnapshot
	{
		AssetId id = InvalidAssetId;
		AssetType type = AssetType::Unknown;
		std::string sourcePath;
		std::string artifactPath;
		std::vector<PropertyEntry> settings;
	};

	struct ProjectBrowserRootSnapshot
	{
		bool projectLoaded = false;
		std::string rootPath;
		std::string rootLabel;
	};

	enum class ProjectAssetCreationKind
	{
		Timeline,
		AnimatorController,
		AnimationRig,
		BoneMask,
		ActionDefinition,
		ActionSet,
		GameplayEffect,
		GameplayCue,
		AttributeSet,
		TargetingPolicy,
		GameplayTagTree,
		PayloadSchema,
		ActionGraph,
		CameraRigProfile,
		CameraShakeProfile,
		SkinProfile,
		AudioReverbPreset,
		AudioBusSnapshot,
		AudioDuckingRules
	};

	struct ProjectAssetCreateRequest
	{
		std::string directoryPath;
		ProjectAssetCreationKind kind = ProjectAssetCreationKind::Timeline;
		std::string name;
	};

	struct ProjectAssetCreateResult
	{
		bool success = false;
		std::string message;
		std::string assetPath;
	};

	struct AssetDragPayload
	{
		bool available = false;
		std::string guid;
		std::string sourcePath;
		std::string displayName;
		AssetType assetType = AssetType::Unknown;
		std::string error;
	};

	struct AssetGuidResolution
	{
		bool found = false;
		AssetEntry asset;
		std::string sourcePath;
	};

	struct AssetRefreshResult
	{
		bool success = false;
		std::string message;
	};

	struct ProjectOpenRequest
	{
		std::string projectPath;
		std::string projectName;
		bool createNew = false;
	};

	struct ProjectOpenResult
	{
		bool success = false;
		std::string projectRootPath;
		std::string defaultSceneRelativePath;
		std::string defaultScenePath;
		std::string message;
	};

	enum class GAFEditorValueKind
	{
		Null,
		Bool,
		Int,
		Float,
		String,
		Array,
		Object,
		Json
	};

	struct GAFEditorValue
	{
		GAFEditorValueKind kind = GAFEditorValueKind::Null;
		bool boolValue = false;
		std::int64_t intValue = 0;
		double floatValue = 0.0;
		std::string stringValue;
		std::string canonicalJson;
	};

	enum class GAFEditorPropertyKind
	{
		Bool,
		Int,
		Float,
		String,
		Enum,
		Object,
		Array,
		Tag,
		TagQuery,
		AssetReference,
		Payload,
		Graph,
		Vec2,
		Vec3,
		Vec4,
		Quaternion,
		Color,
		Map,
		EntityBinding,
		ComponentBinding
	};

	enum class GAFEditorDiagnosticSeverity { Info, Warning, Error, Fatal };

	struct GAFEditorDiagnostic
	{
		GAFEditorDiagnosticSeverity severity = GAFEditorDiagnosticSeverity::Info;
		std::string code;
		std::string message;
		std::string fieldPath;
	};

	struct GAFEditorFieldSnapshot
	{
		std::uint64_t fieldId = 0;
		std::string path;
		std::string displayName;
		std::string group;
		std::string description;
		std::string unit;
		GAFEditorPropertyKind kind = GAFEditorPropertyKind::String;
		GAFEditorValue value;
		bool exists = false;
		bool visible = true;
		bool enabled = true;
		bool required = false;
		bool deprecated = false;
		bool readOnly = false;
		std::size_t arraySize = 0;
		bool hasMinimum = false;
		bool hasMaximum = false;
		double minimum = 0.0;
		double maximum = 0.0;
		double step = 0.0;
		bool hasStep = false;
		std::vector<std::string> enumValues;
		std::vector<AssetType> allowedAssetTypes;
		bool hasArrayElement = false;
		GAFEditorPropertyKind arrayElementKind = GAFEditorPropertyKind::Object;
		GAFEditorValue arrayElementDefault;
		bool isArrayElement = false;
		std::size_t arrayIndex = 0;
		std::vector<GAFEditorFieldSnapshot> children;
		std::vector<GAFEditorDiagnostic> diagnostics;
	};

	struct GAFGraphPinSnapshot
	{
		std::string name;
		std::string dataType;
		bool input = false;
		bool multiple = false;
	};

	struct GAFGraphPropertySnapshot
	{
		std::string name;
		std::string displayName;
		GAFEditorPropertyKind kind = GAFEditorPropertyKind::String;
		GAFEditorValue defaultValue;
		bool required = false;
		bool hasMinimum = false;
		bool hasMaximum = false;
		double minimum = 0.0;
		double maximum = 0.0;
		std::vector<AssetType> allowedAssetTypes;
	};

	struct GAFGraphNodeTypeSnapshot
	{
		std::string type;
		std::string displayName;
		std::string category;
		std::string nodeKind;
		bool predictable = false;
		bool authorityOnly = false;
		bool allowed = true;
		std::vector<GAFGraphPinSnapshot> pins;
		std::vector<GAFGraphPropertySnapshot> properties;
	};

	struct GAFGraphNodePropertyValueSnapshot
	{
		std::string name;
		GAFEditorValue value;
	};

	struct GAFGraphNodeSnapshot
	{
		std::size_t index = 0;
		std::string guid;
		std::string type;
		std::string nodeKind;
		bool predictable = false;
		double x = 0.0;
		double y = 0.0;
		GAFEditorValue properties;
		std::vector<GAFGraphNodePropertyValueSnapshot> propertyValues;
	};

	struct GAFGraphEdgeSnapshot
	{
		std::size_t index = 0;
		std::string from;
		std::string output;
		std::string to;
		std::int32_t order = 0;
	};

	struct GAFGraphSnapshot
	{
		bool available = false;
		std::string entryNode;
		std::vector<GAFGraphNodeSnapshot> nodes;
		std::vector<GAFGraphEdgeSnapshot> edges;
	};

	struct GAFEditorDocumentSnapshot
	{
		bool success = false;
		std::string sourcePath;
		AssetType assetType = AssetType::Unknown;
		std::string assetKind;
		std::uint32_t schemaVersion = 0;
		std::uint64_t contentHash = 0;
		bool dirty = false;
		bool canUndo = false;
		bool canRedo = false;
		bool cookable = false;
		std::vector<std::string> dependencies;
		std::vector<GAFEditorFieldSnapshot> fields;
		std::vector<GAFEditorDiagnostic> diagnostics;
		GAFGraphSnapshot graph;
		std::string canonicalJson;
		std::string message;
	};

	struct GAFEditorFieldEditRequest
	{
		std::string sourcePath;
		std::string fieldPath;
		GAFEditorValue value;
	};

	enum class GAFEditorArrayOperation { Append, Insert, Duplicate, Remove, Move };

	struct GAFEditorArrayEditRequest
	{
		std::string sourcePath;
		std::string fieldPath;
		GAFEditorArrayOperation operation = GAFEditorArrayOperation::Append;
		std::size_t index = 0;
		std::size_t destinationIndex = 0;
		GAFEditorValue value;
	};

	struct GAFEditorOperationResult
	{
		bool success = false;
		std::string message;
		GAFEditorDocumentSnapshot document;
	};

	enum class GAFGraphEditOperation
	{
		AddNode,
		RemoveNode,
		MoveNode,
		Connect,
		Disconnect,
		SetEntryNode,
		SetNodeProperty
	};

	struct GAFGraphEditRequest
	{
		std::string sourcePath;
		GAFGraphEditOperation operation = GAFGraphEditOperation::AddNode;
		std::string nodeGuid;
		std::string nodeType;
		double x = 0.0;
		double y = 0.0;
		std::string fromNode;
		std::string outputPin;
		std::string toNode;
		std::int32_t order = 0;
		std::string propertyName;
		GAFEditorValue value;
	};

	enum class GAFSemanticChangeKind { Added, Removed, Modified };

	struct GAFSemanticDiffEntry
	{
		std::string fieldPath;
		GAFSemanticChangeKind kind = GAFSemanticChangeKind::Modified;
		GAFEditorValue before;
		GAFEditorValue after;
	};

	struct GAFSemanticDiffResult
	{
		bool success = false;
		std::vector<GAFSemanticDiffEntry> entries;
		std::string message;
	};

	struct GAFNamedString
	{
		std::string name;
		std::string value;
	};

	struct GAFProjectTemplateSnapshot
	{
		std::string assetKind;
		GAFEditorValue document;
	};

	struct GAFProjectConfigurationSnapshot
	{
		bool available = false;
		std::string settingsDirectory;
		std::uint32_t schemaVersion = 1;
		std::vector<std::string> defaultTagRoots;
		std::string networkMode = "Disabled";
		bool predictionEnabled = false;
		bool requireRollbackPlan = true;
		bool failWithoutTransport = true;
		bool deterministicCook = true;
		bool stripEditorMetadata = true;
		bool treatCookWarningsAsErrors = false;
		std::string templateDirectory;
		std::uint32_t maximumActiveActionsPerHost = 64;
		std::uint32_t maximumTasksPerAction = 64;
		std::uint32_t maximumGraphTransitionsPerTick = 1024;
		std::uint32_t maximumEffectsPerHost = 256;
		std::uint32_t maximumPayloadBytes = 4096;
		std::vector<std::string> allowedNodeTypes;
		std::vector<std::string> allowedServices;
		std::vector<std::string> allowedHandlers;
		std::vector<std::string> bridgeAllowlist;
		std::vector<GAFNamedString> severityOverrides;
		std::vector<std::string> saveBlockingCodes;
		std::vector<std::string> cookBlockingCodes;
		std::vector<std::string> ciBlockingCodes;
		std::vector<GAFProjectTemplateSnapshot> templates;
		std::string message;
	};

	struct GAFProjectConfigurationResult
	{
		bool success = false;
		std::string message;
		GAFProjectConfigurationSnapshot configuration;
	};

	struct GAFDebugNamedValue
	{
		std::string name;
		std::string value;
	};

	struct GAFDebugTaskSnapshot
	{
		std::string handle;
		std::string type;
		std::string name;
		std::string state;
		double elapsedSeconds = 0.0;
		double timeoutSeconds = 0.0;
	};

	struct GAFDebugResourceSnapshot
	{
		std::string handle;
		std::string type;
		std::string name;
		std::string dependency;
		std::string predictionPolicy;
		bool undone = false;
	};

	struct GAFDebugActionSnapshot
	{
		std::string handle;
		std::string actionId;
		std::string state;
		std::string endReason;
		std::string error;
		double elapsedSeconds = 0.0;
		std::string predictionKey;
		std::string executor;
		std::vector<std::string> activeNodes;
		std::vector<std::string> waitingNodes;
		std::vector<std::string> targets;
		std::vector<GAFDebugNamedValue> variables;
		std::vector<GAFDebugTaskSnapshot> tasks;
		std::vector<GAFDebugResourceSnapshot> resources;
		std::vector<std::string> recentEvents;
		std::vector<std::string> trace;
	};

	struct GAFDebugHostSnapshot
	{
		std::string owner;
		bool enabled = false;
		bool commitFrozen = false;
		std::size_t activeCueCount = 0;
		std::vector<GAFDebugNamedValue> tags;
		std::vector<GAFDebugNamedValue> attributes;
		std::vector<GAFDebugNamedValue> effects;
		std::vector<GAFDebugNamedValue> grants;
		std::vector<GAFDebugActionSnapshot> actions;
	};

	struct GAFCombatWindowDebugSnapshot
	{
		std::string owner;
		std::string window;
		bool active = false;
		Vec3 origin;
		Vec3 forward;
		Vec3 previousBase;
		Vec3 previousTip;
		Vec3 currentBase;
		Vec3 currentTip;
		float range = 0.0f;
		float halfAngleDegrees = 0.0f;
		float sweepRadius = 0.0f;
		std::size_t hitCount = 0;
	};

	struct GAFHurtBodyDebugSnapshot
	{
		std::string target;
		Vec3 center;
		float radius = 0.0f;
		float halfHeight = 0.0f;
		bool hit = false;
	};

	struct GAFCombatDebugSnapshot
	{
		bool available = false;
		std::vector<GAFCombatWindowDebugSnapshot> windows;
		std::vector<GAFHurtBodyDebugSnapshot> hurtBodies;
	};

	struct GAFRuntimeDebugSnapshot
	{
		bool available = false;
		bool replay = false;
		bool recording = false;
		std::uint64_t frame = 0;
		double timeSeconds = 0.0;
		std::uint64_t contentManifestHash = 0;
		std::size_t replayFrame = 0;
		std::size_t replayFrameCount = 0;
		std::vector<GAFDebugHostSnapshot> hosts;
		std::vector<std::string> breakpointHits;
		std::string message;
	};

	enum class GAFDebugBreakpointKind
	{
		Action,
		State,
		Node,
		Event,
		Error,
		Prediction,
		Attribute,
		Window
	};

	enum class GAFDebugBreakpointComparison
	{
		Changed,
		Equal,
		Less,
		LessOrEqual,
		Greater,
		GreaterOrEqual
	};

	struct GAFDebugBreakpointSnapshot
	{
		std::uint64_t id = 0;
		GAFDebugBreakpointKind kind = GAFDebugBreakpointKind::Action;
		std::string expression;
		GAFDebugBreakpointComparison comparison = GAFDebugBreakpointComparison::Changed;
		double value = 0.0;
		double epsilon = 1e-6;
		bool enabled = true;
	};

	enum class GAFDebugCommandKind
	{
		Query,
		AddBreakpoint,
		RemoveBreakpoint,
		SetBreakpointEnabled,
		ClearBreakpoints,
		Pause,
		Resume,
		Step
	};

	struct GAFDebugCommand
	{
		GAFDebugCommandKind kind = GAFDebugCommandKind::AddBreakpoint;
		GAFDebugBreakpointSnapshot breakpoint;
		std::uint64_t breakpointId = 0;
		bool enabled = true;
		double stepSeconds = 1.0 / 60.0;
	};

	struct GAFDebugCommandResult
	{
		bool success = false;
		std::string message;
		std::vector<GAFDebugBreakpointSnapshot> breakpoints;
	};

	enum class GAFTraceCommandKind
	{
		StartRecording,
		StopAndSave,
		CancelRecording,
		OpenReplay,
		CloseReplay,
		SeekReplay,
		StepReplay
	};

	struct GAFTraceCommand
	{
		GAFTraceCommandKind kind = GAFTraceCommandKind::StartRecording;
		std::string path;
		std::size_t maximumFrames = 3600;
		std::size_t maximumBytes = 64 * 1024 * 1024;
		std::size_t frame = 0;
		std::int32_t step = 0;
	};

	struct GAFTraceCommandResult
	{
		bool success = false;
		std::string message;
		GAFRuntimeDebugSnapshot snapshot;
	};

	enum class GAFSimulationMode
	{
		CanActivate,
		Execute
	};

	enum class GAFSimulationTargetKind
	{
		None,
		Entity,
		Location,
		Ray,
		EntitySet
	};

	struct GAFSimulationEntity
	{
		std::uint32_t index = 0;
		std::uint32_t generation = 0;
	};

	struct GAFSimulationTag
	{
		std::string name;
		std::uint32_t count = 1;
	};

	struct GAFSimulationAttribute
	{
		std::string name;
		double value = 0.0;
	};

	struct GAFSimulationRequest
	{
		std::string sourcePath;
		std::string actionReference;
		GAFSimulationMode mode = GAFSimulationMode::CanActivate;
		GAFSimulationEntity owner{ 1, 1 };
		GAFSimulationEntity instigator{ 1, 1 };
		GAFSimulationTargetKind targetKind = GAFSimulationTargetKind::Entity;
		GAFSimulationEntity primaryTarget{ 2, 1 };
		std::vector<GAFSimulationEntity> targetEntities;
		double targetX = 0.0;
		double targetY = 0.0;
		double targetZ = 0.0;
		double rayDirectionX = 0.0;
		double rayDirectionY = 0.0;
		double rayDirectionZ = 1.0;
		double rayLength = 100.0;
		std::vector<GAFSimulationTag> initialTags;
		std::vector<GAFSimulationAttribute> initialAttributes;
		std::string payloadJson = "{}";
		bool hasAuthority = true;
		bool locallyControlled = true;
		bool predicted = false;
		std::uint64_t randomSeed = 1;
		std::uint32_t tickCount = 1;
		double deltaSeconds = 1.0 / 60.0;
	};

	struct GAFSimulationResult
	{
		bool success = false;
		bool canActivate = false;
		bool activated = false;
		std::string actionReference;
		std::string disposition;
		std::string error;
		std::string message;
		std::vector<GAFNamedString> serviceActivity;
		std::vector<GAFRuntimeDebugSnapshot> steps;
	};

	struct KeyValueString
	{
		std::string key;
		std::string value;
	};

	enum class ProjectConfigDiagnosticSeverity
	{
		Info,
		Warning,
		Error
	};

	struct ProjectConfigDiagnostic
	{
		ProjectConfigDiagnosticSeverity severity = ProjectConfigDiagnosticSeverity::Info;
		std::string propertyPointer;
		std::string message;
	};

	enum class ProjectPathField
	{
		DefaultScene,
		AssetsRoot,
		ImportedArtifactRoot,
		RenderSettings,
		PhysicsSettings,
		CollisionLayerSettings
	};

	struct ProjectConfigSnapshot
	{
		bool projectLoaded = false;
		std::string projectRootPath;
		std::string projectName;
		std::string engineVersion;
		std::string createdAt;
		std::string defaultScene;
		std::string assetsRoot;
		std::string importedArtifactRoot;
		std::string metaExtension;
		std::vector<KeyValueString> runtimeAssetBindings;
		std::vector<KeyValueString> assetDirectories;
		std::vector<std::string> scriptSearchPaths;
		std::string renderSettingsPath;
		std::string physicsSettingsPath;
		std::string collisionLayerSettingsPath;
		std::vector<ProjectConfigDiagnostic> diagnostics;
	};

	struct ProjectConfigEditResult
	{
		bool success = false;
		std::string message;
		std::vector<ProjectConfigDiagnostic> diagnostics;
	};

	struct RecentProjectEntry
	{
		std::string name;
		std::string path;
		std::string lastOpened;
	};

	struct RenderTextureFilter
	{
		std::string category;
		std::string name;
		std::uint32_t layer = 0;
		std::uint32_t mipLevel = 0;
		std::uint32_t probeIndex = 0;
		std::uint32_t face = 0;
		float roughness = 0.0f;
	};

	struct RenderTexturePreview
	{
		RenderTextureId id = 0;
		std::string name;
		EditorTextureHandle texture = nullptr;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	enum class PunctualShadowLightKind : std::uint8_t
	{
		Point,
		Spot,
		Rect
	};

	enum class PunctualShadowDisplayMode : std::uint8_t
	{
		Disabled,
		HeroAtlas,
		CachedAtlas,
		AtlasTransition,
		ScreenSpaceFallback,
		Unshadowed
	};

	struct PunctualShadowAtlasViewSnapshot
	{
		std::uint32_t faceIndex = 0;
		std::uint32_t atlasIndex = 0;
		std::uint32_t x = 0;
		std::uint32_t y = 0;
		std::uint32_t resolution = 0;
		std::uint32_t gutter = 0;
		std::uint32_t generation = 0;
	};

	struct PunctualShadowLightDebugSnapshot
	{
		std::uint32_t stableLightId = 0;
		std::uint32_t gpuLightIndex = 0;
		PunctualShadowLightKind lightKind = PunctualShadowLightKind::Point;
		PunctualShadowDisplayMode displayMode = PunctualShadowDisplayMode::Disabled;
		std::string runtimeState;
		std::string policy;
		std::uint32_t priority = 0;
		std::uint32_t activeResolution = 0;
		std::uint32_t targetResolution = 0;
		std::uint32_t dirtyFaceMask = 0;
		std::uint32_t validFaceMask = 0;
		float importance = 0.0f;
		float coverage = 0.0f;
		float cameraDistance = 0.0f;
		float distancePriority = 0.0f;
		float atlasWeight = 0.0f;
		std::uint32_t residencyFrames = 0;
		std::uint32_t staleFrames = 0;
		std::uint64_t lastRenderedFrame = 0;
		bool affectsFog = false;
		bool affectsGI = false;
		std::vector<PunctualShadowAtlasViewSnapshot> atlasViews;
	};

	struct PunctualScreenSpaceShadowSettingsSnapshot
	{
		float maxTraceDistance = 12.0f;
		float thickness = 0.10f;
		float normalBias = 0.020f;
		std::uint32_t maxSteps = 64;
		float strength = 0.95f;
	};

	struct PunctualShadowDebugSnapshot
	{
		bool available = false;
		std::uint64_t frameIndex = 0;
		std::uint32_t atlasSize = 0;
		std::uint32_t atlasCount = 0;
		std::uint32_t basePageSize = 0;
		std::uint32_t gutter = 0;
		std::uint32_t totalPages = 0;
		std::uint32_t usedPages = 0;
		std::uint32_t residentLights = 0;
		std::uint32_t residentViews = 0;
		std::uint32_t renderedViewsThisFrame = 0;
		std::uint32_t fallbackLights = 0;
		std::uint32_t allocationFailures = 0;
		std::uint64_t dirtyTexelsThisFrame = 0;
		RenderTexturePreview atlasPreview;
		RenderTexturePreview screenSpacePreview;
		PunctualScreenSpaceShadowSettingsSnapshot screenSpaceSettings;
		std::vector<PunctualShadowLightDebugSnapshot> lights;
	};

	struct RenderBackendDiagnostics
	{
		bool available = false;
		bool compiledGraphValid = false;
		bool featureAuditPassed = false;
		bool frameSubmitSucceeded = false;
		bool shadowMapsSubmitted = false;
		bool gbufferSubmitted = false;
		bool vegetationSubmitted = false;
		std::uint32_t framePlanPassCount = 0;
		std::uint32_t compiledResourceCount = 0;
		std::uint32_t barrierDependencyCount = 0;
		std::uint64_t renderGraphTopologyRevision = 0;
		std::uint64_t renderGraphTopologyHash = 0;
		std::uint64_t renderGraphCompiledFrameNumber = 0;
		std::uint32_t descriptorStandardPoolCount = 0;
		std::uint32_t descriptorUpdateAfterBindPoolCount = 0;
		std::uint32_t descriptorTrackedSetCount = 0;
		std::uint32_t descriptorUpdateAfterBindLayoutCount = 0;
		std::uint32_t descriptorGlobalPersistentSetCount = 0;
		std::uint32_t descriptorScenePersistentSetCount = 0;
		std::uint32_t descriptorFrameTransientSetCount = 0;
		std::uint32_t descriptorPassPersistentSetCount = 0;
		std::uint32_t descriptorUploadScratchSetCount = 0;
		std::uint32_t descriptorRayTracingPersistentSetCount = 0;
		std::uint64_t deferredDeleteLastFlushCount = 0;
		std::uint64_t deferredDeletePendingCount = 0;
		std::uint64_t renderNodeDescriptorValidationFailureCount = 0;
		std::uint64_t textureUploadFailureCount = 0;
		std::uint64_t frameNumber = 0;
		std::uint32_t swapchainImageIndex = 0;
		std::string renderGraphSummary;
	};

	struct PipelineRegistryMapStatsSnapshot
	{
		std::uint64_t bucketCount = 0;
		std::uint64_t activeCount = 0;
		std::uint64_t expiredCount = 0;
	};

	struct PipelineRegistryStatsSnapshot
	{
		PipelineRegistryMapStatsSnapshot graphics;
		PipelineRegistryMapStatsSnapshot compute;
		PipelineRegistryMapStatsSnapshot rayTracing;
		std::uint64_t totalActiveCount = 0;
		std::uint64_t totalExpiredCount = 0;
	};

	struct RenderDocStatusSnapshot
	{
		bool available = false;
		bool targetControlConnected = false;
		bool frameCapturing = false;
		bool apiValidationEnabled = false;
		bool referenceAllResources = false;
		int apiMajor = 0;
		int apiMinor = 0;
		int apiPatch = 0;
		std::uint32_t captureCount = 0;
		std::string capturePathTemplate;
		std::string lastCapturePath;
		std::string message;
	};

	struct ReflectionProbeEditorSettings
	{
		int selectedProbeIndex = -1;
		bool showProbeGizmos = false;
		bool showInfluenceVolumes = true;
		bool showBlendVolumes = true;
		bool showPlacementGrid = false;
		bool showRegions = false;
		bool previewCubemap = false;
		int previewFace = 0;
		float previewRoughness = 0.0f;
		int debugView = 0;
	};

	struct ReflectionProbePlacementSettingsSnapshot
	{
		bool enabled = true;
		Vec3 volumeMin = { -20.0f, -14.0f, -20.0f };
		Vec3 volumeMax = { 20.0f, 26.0f, 20.0f };
		float uniformSpacing = 7.0f;
		float uniformBoxSizeScale = 1.0f;
		std::uint32_t uniformProbeResolution = 64;
		std::uint32_t maxProbeCount = 256;
	};

	struct ReflectionProbeLightingSettingsSnapshot
	{
		std::uint32_t maxBlendCount = 4;
		float ssrRoughnessFadeStart = 0.35f;
		float ssrRoughnessFadeEnd = 0.75f;
		float skyIntensity = 1.0f;
	};

	struct ReflectionProbeEntrySnapshot
	{
		std::string name;
		int type = 0;
		int shape = 1;
		Vec3 position;
		Vec3 capturePosition;
		Vec3 boxMin = { -5.0f, -5.0f, -5.0f };
		Vec3 boxMax = { 5.0f, 5.0f, 5.0f };
		float radius = 10.0f;
		float blendDistance = 1.0f;
		float priority = 1.0f;
		float intensity = 1.0f;
		float specularIntensity = 1.0f;
		bool enabled = true;
		bool boxProjection = false;
		bool autoGenerated = false;
		std::string bakeStatus;
	};

	struct ReflectionProbeSettingsSnapshot
	{
		bool available = false;
		std::uint32_t arrayResolution = 0;
		std::uint32_t mipCount = 0;
		ReflectionProbeEditorSettings editor;
		ReflectionProbePlacementSettingsSnapshot placement;
		ReflectionProbeLightingSettingsSnapshot lighting;
		std::vector<ReflectionProbeEntrySnapshot> probes;
		std::vector<std::string> validationErrors;
	};

	enum class UpscalerBackend : std::uint32_t
	{
		Off = 0,
		FSR = 1,
		DLSS = 2
	};

	enum class UpscaleQualityMode : std::uint32_t
	{
		NativeAA = 0,
		Quality = 1,
		Balanced = 2,
		Performance = 3,
		UltraPerformance = 4
	};

	struct UpscalerCapabilitiesSnapshot
	{
		UpscalerBackend backend = UpscalerBackend::Off;
		bool compiledIn = false;
		bool runtimeAvailable = false;
		bool deviceSupported = false;
		std::uint32_t supportedQualityMask = 0;
		std::string featureVersion;
		std::string unavailableReason;
	};

	struct UpscalerSettingsSnapshot
	{
		UpscalerBackend desiredBackend = UpscalerBackend::FSR;
		UpscaleQualityMode desiredQuality = UpscaleQualityMode::Quality;
		UpscalerBackend effectiveBackend = UpscalerBackend::FSR;
		UpscaleQualityMode effectiveQuality = UpscaleQualityMode::Quality;
		float fsrSharpness = 0.35f;
		bool fsrDebugView = false;
		std::string fallbackReason;
		std::string fallbackMessage;
		float mipBias = 0.0f;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		bool contextReady = false;
		bool lastDispatchSucceeded = false;
		bool lastDispatchReset = false;
		std::uint32_t pendingResetReasons = 0;
		std::uint32_t backendCreateCode = 0;
		std::uint32_t backendQueryCode = 0;
		std::uint32_t backendDispatchCode = 0;
		std::uint32_t backendAuxiliaryCode = 0;
		std::uint64_t successfulDispatchCount = 0;
		std::uint64_t failedDispatchCount = 0;
		std::uint64_t auxiliaryDispatchCount = 0;
		std::uint64_t gpuMemoryUsageBytes = 0;
		std::uint64_t gpuMemoryAliasableBytes = 0;
		std::int32_t jitterPhaseCount = 0;
		std::string lastError;
	};

	struct ApplyUpscalerSettingsResult
	{
		bool accepted = false;
		bool runtimeFallbackExpected = false;
		std::string message;
	};

	struct CommandRecordingSettingsSnapshot
	{
		bool parallelEnabled = true;
		bool frameContextRingEnabled = false;
		std::uint32_t framesInFlight = 2;
		bool asyncComputeRequested = false;
		bool asyncComputeEnabled = false;
		bool hasDedicatedAsyncComputeQueue = false;
	};

	struct GIRegionSettingsSnapshot
	{
		std::uint32_t stableId = 0;
		std::string name;
		bool enabled = true;
		Vec3 regionCenter = { 0.0f, 6.0f, 0.0f };
		Vec3 size;
		Vec3 volumeMin;
		Vec3 volumeMax;
		Vec3 gridDimensions;
		float probeSpacing = 0.5f;
		float normalBias = 0.0f;
		float maxRayDistance = 0.0f;
		float volumeFadeDistance = 0.0f;
		float priority = 0.0f;
		std::uint32_t raysPerProbe = 0;
		std::uint32_t spatialUpdateDivisor = 1;
		std::uint32_t directionUpdateSlices = 1;
		std::uint32_t totalProbeCount = 0;
		std::uint64_t rayCacheEntries = 0;
		float estimatedMemoryMB = 0.0f;
	};

	struct GIInspectorSettingsSnapshot
	{
		bool available = false;
		float environmentIntensity = 0.0f;
		float maxIndirectRadiance = 0.0f;
		float maxProbeRadiance = 0.0f;
		float irradianceHysteresis = 0.97f;
		float distanceHysteresis = 0.95f;
		float distanceSharpness = 12.0f;
		float brightnessChangeThreshold = 2.0f;
		bool showProbeGizmos = false;
		bool showProbeVolume = false;
		int debugView = 0;
		float debugExposure = 1.0f;
		bool probeOnlyDeferredOutput = false;
		float probeOnlyDeferredExposure = 1.0f;
		std::uint32_t gizmoStride = 8;
		std::uint32_t totalProbeCount = 0;
		std::uint64_t totalRayCacheEntries = 0;
		float totalEstimatedMemoryMB = 0.0f;
		std::uint32_t selectedRegionIndex = 0;
		std::vector<GIRegionSettingsSnapshot> regions;
	};

	struct GIProbeDebugEntrySnapshot
	{
		Vec3 position;
		Vec3 l0Diffuse;
		float l1Ratio = 0.0f;
	};

	struct GIProbeDebugSnapshot
	{
		bool available = false;
		Vec3 gridDimensions;
		std::uint32_t stride = 1;
		float exposure = 1.0f;
		std::vector<GIProbeDebugEntrySnapshot> probes;
		std::string status;
	};

	struct MainCameraHiZCulledNodeSnapshot
	{
		std::string name;
		std::string cullClass;
		Vec3 center;
		Vec3 axisXHalf;
		Vec3 axisYHalf;
		Vec3 axisZHalf;
	};

	struct MainCameraHiZCullDebugSnapshot
	{
		bool available = false;
		bool enabled = false;
		bool historyValid = false;
		std::uint32_t candidateCount = 0;
		std::uint32_t frustumVisibleCount = 0;
		std::uint32_t hizCulledCount = 0;
		std::uint32_t forcedVisibleCount = 0;
		std::uint32_t preCullDrawCallCount = 0;
		std::uint32_t culledDrawCallCount = 0;
		std::uint32_t drawnDrawCallCount = 0;
		std::vector<MainCameraHiZCulledNodeSnapshot> culledNodes;
	};

	struct WaterMediumSettings
	{
		Vec3 absorptionCoeff = { 0.25f, 0.08f, 0.02f };
		Vec3 scatteringCoeff = { 0.02f, 0.04f, 0.06f };
		float ior = 1.33f;
		float anisotropy = 0.85f;
		float waterRoughness = 0.02f;
	};

	struct WaterSpectrumSettings
	{
		int mode = 2;
		int cascadeCount = 4;
		float baseCoverage = 64.0f;
		float cascadeScale = 4.0f;
		Vec2 windDirection = { 0.7071f, 0.7071f };
		float windSpeed = 16.0f;
		float swellAmplitude = 0.35f;
		float choppiness = 1.65f;
		int gerstnerWaveCount = 32;
		float spectrumAmplitude = 0.001f;
		float minWavelength = 1.0f;
		float smallWaveDamping = 0.003f;
		float windDependency = 0.07f;
		float depth = 10000.0f;
		float repeatPeriod = 0.0f;
		std::uint32_t randomSeed = 1337u;
	};

	struct WaterWaveParticleSettings
	{
		int particlesPerCascade = 128;
		float rmsAmplitude = 0.32f;
		float packetWidth = 1.5f;
		float dispersionScale = 1.0f;
		float directionSpread = 0.7f;
		float cascadeAmplitudeFalloff = 0.62f;
		float foamThreshold = 0.28f;
		float foamSoftness = 0.25f;
		std::uint32_t randomSeed = 20260724u;
	};

	struct WaterFlowMapSettings
	{
		bool enabled = false;
		float strength = 10.0f;
		float speed = 0.65f;
		float phaseLength = 1.0f;
		float noiseAmount = 0.5f;
		Vec2 worldOrigin = { -256.0f, -256.0f };
		Vec2 worldSize = { 512.0f, 512.0f };
		Vec2 fallbackDirection = { 1.0f, 0.0f };
	};

	struct WaterGeometrySettings
	{
		int lodCount = 10;
		float basePatchSize = 16.0f;
		float morphStartRatio = 0.5f;
	};

	struct WaterOpticsSettings
	{
		float maxCrossDistance = 40.0f;
		float maxRefractionCrossDistance = 20.0f;
		float multiScatterScale = 1.0f;
		float waterDispersionStrength = 0.2f;
		float sssPathScale = 20.0f;
		float sssNonlinearStrength = 0.5f;
		float sssScatterBoost = 2.0f;
		float backlitPathScale = 20.0f;
		float backlitPhaseG = 0.9998f;
	};

	struct WaterVolumeSettings
	{
		float resolutionScale = 0.5f;
		int sampleCount = 12;
		int spatialFilterIterations = 2;
		float spatialDepthSensitivity = 2.0f;
	};

	struct WaterDetailNormalLayerSettings
	{
		bool enabled = false;
		float tileSizeMeters = 1.0f;
		Vec2 direction = { 1.0f, 0.0f };
		float speedMetersPerSecond = 0.05f;
		float phase = 0.0f;
		float strength = 0.2f;
		float fadeStartMeters = 0.0f;
		float fadeEndMeters = 80.0f;
	};

	struct WaterDetailNormalSettings
	{
		bool enabled = true;
		int decodeMode = 0;
		bool flipGreen = false;
		float globalStrength = 1.0f;
		float maxSlope = 1.5f;
		float mipBias = 0.0f;
		float anisotropy = 8.0f;
		std::uint32_t layerCount = 4;
		std::array<WaterDetailNormalLayerSettings, 4> layers{};
	};

	struct WaterEffectiveRoughnessSettings
	{
		int mode = 0;
		float distanceStartMeters = 25.0f;
		float distanceEndMeters = 180.0f;
		float distanceStrength = 0.08f;
	};

	struct WaterColorMipSettings
	{
		float refractionScatterScale = 0.35f;
		float refractionRoughnessScale = 0.10f;
		float forwardScatterMipScale = 0.30f;
		float backgroundScatterScale = 0.25f;
		float lodBias = 0.0f;
	};

	struct WaterShadowSettings
	{
		bool enabled = true;
		int quality = 1;
		float depthBias = 0.0005f;
		float normalBias = 0.02f;
		int volumeStepStride = 2;
	};

	struct WaterSettingsSnapshot
	{
		bool available = false;
		float waterLevel = 3.4f;
		float specularIntensity = 1.0f;
		WaterMediumSettings medium;
		WaterGeometrySettings geometry;
		WaterSpectrumSettings spectrum;
		WaterWaveParticleSettings waveParticle;
		WaterFlowMapSettings flowMap;
		WaterOpticsSettings optics;
		WaterVolumeSettings volume;
		WaterDetailNormalSettings detailNormal;
		WaterEffectiveRoughnessSettings effectiveRoughness;
		WaterColorMipSettings colorMip;
		WaterShadowSettings shadow;
		bool thinSSSEnabled = true;
		float maxThicknessDistance = 15.0f;
		float deepWaterThicknessFallback = 0.8f;
		bool causticsEnabled = false;
		float causticsIntensity = 1.0f;
		float causticsMaxDistance = 20.0f;
		float causticsMaxGain = 3.0f;
		float causticsFilterRadius = 0.5f;
		bool refractionEnabled = true;
		float refractionDistortionStrength = 0.025f;
		bool ssrEnabled = true;
		float ssrMaxDistance = 500.0f;
		float ssrMaxRoughness = 0.3f;
		float ssrRoughnessFadeStart = 0.18f;
		float ssrColorMipConeScale = 0.35f;
		float ssrColorMipBias = 0.0f;
		float ssrEdgeFadePixels = 12.0f;
	};

	struct WaterRuntimeStats
	{
		bool available = false;
		bool systemInitialized = false;
		bool fftAvailable = false;
		std::uint32_t patchCount = 0;
		int meshDim = 0;
		float basePatchSize = 0.0f;
		std::uint32_t indexCount = 0;
		int lodLevels = 0;
		float geometryLodRatio = 2.0f;
		int maxSpectrumCascade = 0;
		int fftFieldCount = 0;
		bool detailNormalAssetAvailable = false;
		std::string detailNormalRuntimeAlias = "waterDetailWaveNormal";
		std::uint32_t detailNormalMipCount = 0;
		float detailNormalAnisotropyActive = 1.0f;
		std::uint32_t waterBackgroundPyramidMipCount = 0;
		std::uint32_t waterBackgroundPyramidWidth = 0;
		std::uint32_t waterBackgroundPyramidHeight = 0;
		int effectiveRoughnessMode = 0;
		bool shadowCascadeAvailable = false;
	};

	struct MeshLoadRequest
	{
		std::string meshName;
		std::string sourcePath;
	};

	struct MeshLoadResult
	{
		bool loaded = false;
		bool available = false;
	};

	struct ProjectSubmeshInfo
	{
		std::string sourceNodeName;
		std::string materialName;
		std::string diffuseTexturePath;
		std::uint32_t vertexCount = 0;
		std::uint32_t indexCount = 0;
	};

	struct ProjectMeshInfoSnapshot
	{
		bool available = false;
		bool isMultiMesh = false;
		std::vector<ProjectSubmeshInfo> submeshes;
	};

	struct ProjectMeshAliasRequest
	{
		std::string aliasName;
		std::string meshName;
	};

	struct ScenePropertyValue
	{
		enum class Kind
		{
			Null,
			Bool,
			Int,
			Float,
			String,
			Array,
			Object
		};

		Kind kind = Kind::Null;
		bool boolValue = false;
		std::int64_t intValue = 0;
		double floatValue = 0.0;
		std::string stringValue;
		std::vector<ScenePropertyValue> arrayItems;
		std::vector<std::pair<std::string, ScenePropertyValue>> objectFields;

		static ScenePropertyValue Bool(bool value)
		{
			ScenePropertyValue result;
			result.kind = Kind::Bool;
			result.boolValue = value;
			return result;
		}

		static ScenePropertyValue Int(std::int64_t value)
		{
			ScenePropertyValue result;
			result.kind = Kind::Int;
			result.intValue = value;
			return result;
		}

		static ScenePropertyValue Float(double value)
		{
			ScenePropertyValue result;
			result.kind = Kind::Float;
			result.floatValue = value;
			return result;
		}

		static ScenePropertyValue String(std::string value)
		{
			ScenePropertyValue result;
			result.kind = Kind::String;
			result.stringValue = std::move(value);
			return result;
		}

		static ScenePropertyValue Array(std::vector<ScenePropertyValue> items)
		{
			ScenePropertyValue result;
			result.kind = Kind::Array;
			result.arrayItems = std::move(items);
			return result;
		}

		static ScenePropertyValue Object(std::vector<std::pair<std::string, ScenePropertyValue>> fields)
		{
			ScenePropertyValue result;
			result.kind = Kind::Object;
			result.objectFields = std::move(fields);
			return result;
		}
	};

	struct ModelAssetPlacementRequest
	{
		std::string assetGuid;
		Vec3 worldPosition;
	};

	struct ModelAssetPlacementPayload
	{
		bool prepared = false;
		std::string message;
		std::vector<ScenePropertyValue> sceneEntities;
		std::string runtimeEntityGuid;
	};

	struct VehicleChassisDebugSnapshot
	{
		Vec3 center;
		Vec3 axisX = { 1.0f, 0.0f, 0.0f };
		Vec3 axisY = { 0.0f, 1.0f, 0.0f };
		Vec3 axisZ = { 0.0f, 0.0f, 1.0f };
		Vec3 halfExtents;
	};

	struct VehicleWheelDebugSnapshot
	{
		Vec3 center;
		Vec3 lateralAxis = { 1.0f, 0.0f, 0.0f };
		Vec3 verticalAxis = { 0.0f, 1.0f, 0.0f };
		Vec3 longitudinalAxis = { 0.0f, 0.0f, 1.0f };
		Vec3 suspensionAttach;
		Vec3 suspensionRayEnd;
		float radius = 0.0f;
		float halfWidth = 0.0f;
	};

	struct VehicleDebugSnapshot
	{
		bool available = false;
		VehicleChassisDebugSnapshot chassis;
		std::vector<VehicleWheelDebugSnapshot> wheels;
	};

	struct MotionMatchingDebugVisual
	{
		std::string runtimeNodeName;
		std::string entityGuid;
		bool retargetSource = false;
		Vec3 rootPosition;
		Vec3 actualVelocity;
		Vec3 plannedVelocity;
		Vec3 desiredVelocity;
		Vec3 activeClipVelocity;
		Vec3 selectedCandidateVelocity;
		Vec3 appliedRootMotionVelocity;
		Vec3 rootMotionTargetVelocity;
		Vec3 rootMotionReconciledVelocity;
		Vec3 moveInputLocal;
		Vec3 predictedPivotPosition;
		std::vector<Vec3> historyPositions;
		std::vector<Vec3> futurePositions;
		std::vector<Vec3> futureVelocities;
		std::string activeClip;
		std::string selectedClip;
		float playbackRate = 1.0f;
		float querySpeed = 0.0f;
		float queryDirectionDegrees = 0.0f;
		float directionChangeDegrees = 0.0f;
		float inputDirectionChangeDegrees = 0.0f;
		float facingDeltaDegrees = 0.0f;
		float currentFacingYawDegrees = 0.0f;
		float desiredFacingYawDegrees = 0.0f;
		float desiredFacingYawRateDegreesPerSecond = 0.0f;
		std::string facingTurnState;
		std::string facingTurnGateReason;
		float movementReferenceYaw = 0.0f;
		float movementReferenceYawRate = 0.0f;
		float plannedFacingYaw = 0.0f;
		float steeringTargetFacingDeltaDegrees = 0.0f;
		float steeringAuthoredFacingDeltaDegrees = 0.0f;
		float steeringRequestedCorrectionDegrees = 0.0f;
		float steeringAppliedCorrectionDegrees = 0.0f;
		float steeringAppliedYawRateDegreesPerSecond = 0.0f;
		float rootMotionTargetYawRateDegreesPerSecond = 0.0f;
		float rootMotionReconciledYawRateDegreesPerSecond = 0.0f;
		float authoredRootYawDeltaDegrees = 0.0f;
		float appliedRootYawDeltaDegrees = 0.0f;
		bool steeringActive = false;
		bool steeringLimited = false;
		bool turnWarpActive = false;
		bool turnWarpLimited = false;
		bool turnWarpNeedsReplan = false;
		std::string turnWarpDisableReason;
		std::string turnWarpReplanReason;
		float turnWarpTargetDeltaDegrees = 0.0f;
		float turnWarpAuthoredRemainingYawDegrees = 0.0f;
		float turnWarpScaleRatio = 1.0f;
		float turnWarpResidualDegrees = 0.0f;
		float turnWarpAppliedFrameCorrectionDegrees = 0.0f;
		float turnWarpAccumulatedAdditiveDegrees = 0.0f;
		float turnWarpEndpointCost = 0.0f;
		float turnWarpMotionEndTimeSeconds = 0.0f;
		int turnWarpProfileIndex = -1;
		bool rootMotionReconciliationActive = false;
		float currentCost = 0.0f;
		float trajectoryCost = 0.0f;
		float poseCost = 0.0f;
		float contactCost = 0.0f;
		bool pivotRequested = false;
		bool pivotDatabaseAvailable = false;
		bool hasPredictedPivot = false;
		float predictedPivotTime = 0.0f;
		float motionConsumptionRatio = 1.0f;
		bool movementBlocked = false;
		bool urgentDirectionChange = false;
		int requestedMoveState = 0;
		int effectiveMoveState = 0;
		bool directionalStateFallback = false;
		bool facingTurnRequested = false;
		int switches = 0;
		std::vector<std::string> activeDatabases;
		struct Candidate
		{
			std::string clipName;
			float time = 0.0f;
			float totalCost = 0.0f;
			float trajectoryCost = 0.0f;
			float poseCost = 0.0f;
			float contactCost = 0.0f;
			float biasCost = 0.0f;
			float turnEndpointCost = 0.0f;
		};
		std::vector<Candidate> topCandidates;
	};

	struct MotionMatchingDebugSnapshot
	{
		bool available = false;
		std::vector<MotionMatchingDebugVisual> visuals;
	};

	struct AnimationAssetBindingSnapshot
	{
		bool available = false;
		std::string animatorAssetPath;
		std::string runtimeNodeName;
	};

	struct SkeletonDebugBoneSnapshot
	{
		std::string guid;
		std::string name;
		std::string canonicalPath;
		int parentIndex = -1;
		RuntimeTransformSnapshot localTransform;
		RuntimeTransformSnapshot worldTransform;
		Vec3 worldPosition;
	};

	struct SkeletonDebugSocketSnapshot
	{
		std::string guid;
		std::string name;
		std::string boneGuid;
		int parentBoneIndex = -1;
		RuntimeTransformSnapshot localTransform;
		RuntimeTransformSnapshot worldTransform;
	};

	struct SkeletonDebugRigSnapshot
	{
		std::string nodeName;
		std::string entityGuid;
		std::string animationComponentGuid;
		std::string skeletonGuid;
		std::uint64_t skeletonSignature = 0;
		std::uint64_t poseRevision = 0;
		std::string role;
		std::string currentState;
		std::string activeClip;
		std::string selectedClip;
		float currentTime = 0.0f;
		float normalizedTime = 0.0f;
		bool playing = false;
		bool retargetSource = false;
		std::vector<SkeletonDebugBoneSnapshot> bones;
		std::vector<SkeletonDebugSocketSnapshot> sockets;
	};

	struct SkeletonDebugSnapshot
	{
		bool available = false;
		std::vector<SkeletonDebugRigSnapshot> rigs;
	};

	// Formal editor projection of runtime-owned target skeleton topology.  This
	// intentionally excludes animated transforms so a collapsed Hierarchy never
	// requests or copies a full pose.
	struct SceneSkeletonHierarchyBone
	{
		std::string guid;
		std::string name;
		std::string canonicalPath;
		int parentIndex = -1;
	};

	struct SceneSkeletonHierarchySocket
	{
		std::string guid;
		std::string name;
		std::string boneGuid;
		int parentBoneIndex = -1;
	};

	struct SceneSkeletonHierarchyRig
	{
		std::string nodeName;
		std::string entityGuid;
		std::string animationComponentGuid;
		std::string skeletonGuid;
		std::uint64_t skeletonSignature = 0;
		std::vector<SceneSkeletonHierarchyBone> bones;
		std::vector<SceneSkeletonHierarchySocket> sockets;
	};

	struct SceneSkeletonHierarchySnapshot
	{
		bool available = false;
		std::vector<SceneSkeletonHierarchyRig> rigs;
	};

	enum class SceneSkeletonNodeKind
	{
		Bone,
		Socket
	};

	struct SceneSkeletonNodePoseRequest
	{
		std::string entityGuid;
		std::string animationComponentGuid;
		SceneSkeletonNodeKind kind = SceneSkeletonNodeKind::Bone;
		std::string anchorGuid;
	};

	// A targeted pose read for the one Bone/Socket selected in Inspector.
	struct SceneSkeletonNodePoseSnapshot
	{
		bool available = false;
		SceneSkeletonNodeKind kind = SceneSkeletonNodeKind::Bone;
		std::string entityGuid;
		std::string animationComponentGuid;
		std::string skeletonGuid;
		std::uint64_t skeletonSignature = 0;
		std::uint64_t poseRevision = 0;
		std::string anchorGuid;
		std::string name;
		std::string canonicalPath;
		std::string boneGuid;
		RuntimeTransformSnapshot localTransform;
		RuntimeTransformSnapshot modelTransform;
		RuntimeTransformSnapshot worldTransform;
	};

	// Read-only bind-pose data for isolated animation authoring previews.  The
	// editor never receives a RenderCore or runtime Skeleton pointer.
	struct AssetSkeletonBoneSnapshot
	{
		std::string name;
		int parentIndex = -1;
		Vec3 bindPosition;
	};

	struct AssetSkeletonSnapshot
	{
		bool available = false;
		std::string assetGuid;
		std::string sourcePath;
		std::string error;
		std::vector<AssetSkeletonBoneSnapshot> bones;
	};

	enum class AnimationRigSolverKind { Limb, CCD, FABRIK, Aim };
	enum class AnimationRigJointLimitKind { Hinge, SwingTwist, Locked };
	enum class AnimationRigAttachmentParentKind { Bone, Socket };

	struct AnimationRigSocketDTO
	{
		std::string guid;
		std::string name;
		std::string boneGuid;
		Vec3 positionLocal;
		Vec4 rotationLocal{ 0.0f, 0.0f, 0.0f, 1.0f };
		Vec3 scaleLocal{ 1.0f, 1.0f, 1.0f };
	};

	struct AnimationRigAttachmentProfileDTO
	{
		std::string modelGuid;
		AnimationRigAttachmentParentKind parentKind =
			AnimationRigAttachmentParentKind::Socket;
		std::string anchorGuid;
		Vec3 positionLocal;
		Vec4 rotationLocal{ 0.0f, 0.0f, 0.0f, 1.0f };
		Vec3 scaleLocal{ 1.0f, 1.0f, 1.0f };
	};

	struct AnimationRigGoalDTO
	{
		std::string id;
		std::string effectorBone;
	};

	struct AnimationRigChainDTO
	{
		std::string id;
		AnimationRigSolverKind solver = AnimationRigSolverKind::Limb;
		std::vector<std::string> bones;
		std::string goal;
		Vec3 poleAxisLocal{ 0.0f, 0.0f, 1.0f };
		float softReachStartRatio = 0.97f;
		float maxStretchScale = 1.0f;
		std::vector<float> weights;
		std::vector<float> solveWeights;
		float maxStepDegrees = 180.0f;
		Vec3 forwardAxisLocal{ 0.0f, 0.0f, 1.0f };
		Vec3 upAxisLocal{ 0.0f, 1.0f, 0.0f };
	};

	struct AnimationRigJointLimitDTO
	{
		std::string bone;
		AnimationRigJointLimitKind kind = AnimationRigJointLimitKind::Hinge;
		Vec3 axisLocal{ 1.0f, 0.0f, 0.0f };
		Vec3 swingReferenceAxisLocal{ 0.0f, 0.0f, 1.0f };
		float minDegrees = -180.0f;
		float maxDegrees = 180.0f;
		Vec2 swingLimitDegrees{ 180.0f, 180.0f };
	};

	struct AnimationRigSoleSampleDTO
	{
		std::string id;
		Vec3 positionLocal;
	};

	struct AnimationRigContactDTO
	{
		std::string id;
		std::string chain;
		std::string footBone;
		std::string ballBone;
		Vec3 soleForwardLocal{ 0.0f, 0.0f, 1.0f };
		Vec3 soleNormalLocal{ 0.0f, 1.0f, 0.0f };
		std::vector<AnimationRigSoleSampleDTO> soleSamplesLocal;
		Vec3 heelPivotLocal;
		Vec3 ballPivotLocal;
		Vec3 anklePivotLocal;
		float sweepRadius = 0.035f;
	};

	struct AnimationRigDocumentDTO
	{
		std::string name;
		std::string skeletonGuid;
		Vec3 modelForward{ 0.0f, 0.0f, 1.0f };
		Vec3 modelUp{ 0.0f, 1.0f, 0.0f };
		std::vector<std::pair<std::string, std::string>> semanticBones;
		std::vector<AnimationRigSocketDTO> sockets;
		std::vector<AnimationRigAttachmentProfileDTO> attachmentProfiles;
		std::vector<AnimationRigGoalDTO> goals;
		std::vector<AnimationRigChainDTO> chains;
		std::vector<AnimationRigJointLimitDTO> jointLimits;
		std::vector<AnimationRigContactDTO> contacts;
	};

	struct AnimationRigDocumentDecodeResult
	{
		bool success = false;
		AnimationRigDocumentDTO document;
		std::string message;
	};

	struct AnimationRigDocumentEncodeResult
	{
		bool success = false;
		std::string canonicalJson;
		std::string message;
	};

	using AnimationPreviewSessionId = std::uint64_t;
	enum class AnimationPreviewTargetKind
	{
		IsolatedModel,
		SceneAnimationComponent
	};

	struct AnimationPreviewCreateRequest
	{
		AnimationPreviewTargetKind targetKind = AnimationPreviewTargetKind::IsolatedModel;
		std::string previewModelGuid;
		// Scene preview always runs the explicitly selected Animator. The target
		// Animation Component supplies its Skeleton, target Animation Rig/Socket
		// context, and Retarget chain.
		std::string animatorAssetGuid;
		std::string entityGuid;
		std::string animationComponentGuid;
	};

	struct AnimationPreviewCreateResult
	{
		bool success = false;
		AnimationPreviewSessionId sessionId = 0;
		std::string message;
	};

	// Immutable current authoring snapshot. canonicalJson is produced by the
	// strict VansAnimatorIO codec; the API never rereads the source file.
	struct AnimationPreviewDefinitionUpdate
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t revision = 0;
		std::string canonicalJson;
	};

	struct AnimationPreviewUpdateResult
	{
		bool success = false;
		std::uint64_t acceptedRevision = 0;
		std::uint64_t displayedRevision = 0;
		bool usingLastGoodDefinition = false;
		std::string message;
	};

	enum class TimelinePreviewState { Detached, Stopped, Playing, Paused, Completed, Error };

	struct TimelinePreviewStartRequest
	{
		std::string previewId;
		std::string canonicalJson;
		std::string sourceAssetPath;
		std::string ownerEntityGuid;
		bool safeEvents = false;
		bool includeSubTimelines = false;
	};

	struct TimelinePreviewPlaybackRequest
	{
		std::string previewId;
		double playRate = 1.0;
		int direction = 1;
		bool loopPlaybackRange = false;
	};

	struct TimelinePreviewResult
	{
		bool success = false;
		TimelinePreviewState state = TimelinePreviewState::Detached;
		std::int64_t currentTick = 0;
		std::string message;
		std::string ownerEntityGuid;
	};

	struct AnimationPreviewPlaybackRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		bool playing = true;
		float speed = 1.0f;
		bool seek = false;
		float seekSeconds = 0.0f;
		enum class RootMotionMode { InPlace, VisualOffset, TrailOnly };
		RootMotionMode rootMotionMode = RootMotionMode::InPlace;
	};

	enum class AnimationPreviewParameterType { Float, Bool, Int, Trigger, Vector3, Quaternion };
	struct AnimationPreviewParameterValue
	{
		AnimationPreviewSessionId sessionId = 0;
		std::string name;
		AnimationPreviewParameterType type = AnimationPreviewParameterType::Float;
		float floatValue = 0.0f;
		bool boolValue = false;
		int intValue = 0;
		Vec3 vectorValue;
		Vec4 quaternionValue = { 0.0f, 0.0f, 0.0f, 1.0f };
	};

	struct AnimationPreviewSlotRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		std::string slotId;
		std::string clipName;
		float playRate = 1.0f;
		int loopCount = 1;
		int priority = 0;
	};

	struct AnimationPreviewGraphSetRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		std::string graphSetId;
	};

	struct AnimationPreviewViewportRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		float yaw = 0.0f;
		float pitch = 0.0f;
		float zoom = 1.0f;
		// -1 visualizes the final dominant layer source. A non-negative value
		// visualizes the selected layer's effective per-bone mask/weight.
		int visualizedLayerIndex = -1;
	};

	struct AnimationPreviewLayerSnapshot
	{
		std::string id;
		std::string name;
		std::string state;
		std::string clip;
		float weight = 0.0f;
		float normalizedTime = 0.0f;
		bool enabled = false;
		bool overlay = false;
		bool additive = false;
		float evaluationMilliseconds = 0.0f;
		std::vector<float> boneWeights;
	};

	struct AnimationPreviewBoneSnapshot
	{
		std::string guid;
		std::string name;
		int parentIndex = -1;
		Vec3 position;
		int dominantLayerIndex = 0;
		float dominantLayerWeight = 0.0f;
	};

	struct AnimationPreviewSocketSnapshot
	{
		std::string guid;
		std::string name;
		std::string boneGuid;
		int parentBoneIndex = -1;
		RuntimeTransformSnapshot localTransform;
		RuntimeTransformSnapshot modelTransform;
		RuntimeTransformSnapshot worldTransform;
		std::size_t attachmentCount = 0;
	};

	enum class AnimationPreviewAttachmentSourceKind { SceneEntity, TransientModel };

	struct AnimationPreviewAttachmentSnapshot
	{
		std::uint64_t previewAttachmentId = 0;
		AnimationPreviewAttachmentSourceKind sourceKind =
			AnimationPreviewAttachmentSourceKind::SceneEntity;
		std::string name;
		std::string entityGuid;
		std::string modelGuid;
		RuntimeParentReference parent;
		RuntimeTransformSnapshot localTransform;
		RuntimeTransformSnapshot worldTransform;
		RuntimeReparentTransformPolicy currentReparentPolicy =
			RuntimeReparentTransformPolicy::KeepLocal;
		bool editable = false;
		bool dirty = false;
		bool temporary = false;
	};

	struct AnimationPreviewAttachmentProfileSnapshot
	{
		std::string modelGuid;
		RuntimeParentKind parentKind = RuntimeParentKind::Socket;
		std::string anchorGuid;
		RuntimeTransformSnapshot localTransform;
	};

	struct AnimationPreviewRigSnapshot
	{
		bool available = false;
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t sceneContentRevision = 0;
		std::uint64_t rigRevision = 0;
		std::uint64_t attachmentRevision = 0;
		std::string entityGuid;
		std::string animationComponentGuid;
		std::string targetSkeletonGuid;
		std::string rigAssetGuid;
		std::string rigAssetPath;
		bool retargetEnabled = false;
		std::string retargetProfilePath;
		std::string retargetSourceModelPath;
		std::string retargetSourceAnimatorPath;
		std::vector<AnimationPreviewSocketSnapshot> sockets;
		std::vector<AnimationPreviewAttachmentProfileSnapshot> attachmentProfiles;
		std::vector<AnimationPreviewAttachmentSnapshot> attachments;
		std::string diagnostic;
	};

	struct AnimationPreviewRigSocketTransformRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t expectedRigRevision = 0;
		std::string socketGuid;
		RuntimeTransformSpace space = RuntimeTransformSpace::Local;
		RuntimeTransformSnapshot transform;
	};

	struct AnimationPreviewRigEditResult
	{
		bool success = false;
		std::uint64_t acceptedRevision = 0;
		bool usingLastGoodRig = false;
		std::string message;
	};

	struct AnimationPreviewRigAttachmentProfileRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t expectedRigRevision = 0;
		std::string modelGuid;
		RuntimeParentKind parentKind = RuntimeParentKind::None;
		std::string anchorGuid;
		RuntimeTransformSnapshot localTransform;
		bool remove = false;
	};

	struct AnimationPreviewAttachmentTransformRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t expectedAttachmentRevision = 0;
		std::string entityGuid;
		RuntimeTransformSpace space = RuntimeTransformSpace::Local;
		RuntimeTransformSnapshot transform;
	};

	struct AnimationPreviewAttachmentBindingRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t expectedAttachmentRevision = 0;
		std::string entityGuid;
		RuntimeParentReference parent;
		RuntimeReparentTransformPolicy transformPolicy =
			RuntimeReparentTransformPolicy::KeepLocal;
	};

	struct AnimationPreviewAttachmentEditResult
	{
		bool success = false;
		std::uint64_t acceptedRevision = 0;
		RuntimeTransformSnapshot localTransform;
		std::string message;
	};

	struct AnimationPreviewSceneEntitySnapshot
	{
		std::string entityGuid;
		std::string name;
		std::string modelGuid;
		bool active = true;
	};

	struct AnimationPreviewRigAdoptRequest
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t expectedRigRevision = 0;
	};

	struct AnimationPreviewEventSnapshot
	{
		std::string name;
		float time = 0.0f;
		std::string payload;
	};

	struct AnimationPreviewCurveSnapshot
	{
		std::string name;
		float value = 0.0f;
	};

	struct AnimationPreviewSlotSnapshot
	{
		std::uint64_t handle = 0;
		std::string slotId;
		std::string clipName;
		std::string tag;
		std::string state;
		float playbackTime = 0.0f;
		float weight = 0.0f;
	};

	struct AnimationPreviewSlotEventSnapshot
	{
		std::uint64_t handle = 0;
		std::string slotId;
		std::string clipName;
		std::string type;
	};

	struct AnimationPreviewSnapshot
	{
		bool available = false;
		bool compiled = false;
		bool playing = false;
		bool sceneTarget = false;
		bool seekSupported = true;
		std::string seekUnavailableReason;
		std::string entityGuid;
		std::string animationComponentGuid;
		bool usingLastGoodDefinition = false;
		std::uint64_t requestedRevision = 0;
		std::uint64_t displayedRevision = 0;
		float currentTime = 0.0f;
		float duration = 0.0f;
		float normalizedTime = 0.0f;
		std::string activeGraphSetId;
		std::string incomingGraphSetId;
		float graphSetTransitionProgress = 1.0f;
		float speed = 1.0f;
		float lastUpdateMilliseconds = 0.0f;
		std::uint64_t frameScratchAllocations = 0;
		std::uint64_t frameScratchAllocatedBytes = 0;
		EditorTextureHandle modelTexture = nullptr;
		bool modelRendered = false;
		std::uint32_t modelTextureWidth = 0;
		std::uint32_t modelTextureHeight = 0;
		Vec3 modelCenter;
		float modelRadius = 1.0f;
		std::uint64_t modelVertexCount = 0;
		std::uint64_t modelTriangleCount = 0;
		float modelRenderMilliseconds = 0.0f;
		Vec3 rootMotionDelta;
		Vec3 rootMotionPosition;
		bool syncValid = false;
		std::uint64_t syncMarkerId = 0;
		std::uint64_t syncNextMarkerId = 0;
		float syncPhase = 0.0f;
		std::string diagnostic;
		std::vector<Vec3> rootMotionTrail;
		std::vector<AnimationPreviewBoneSnapshot> bones;
		std::vector<AnimationPreviewSocketSnapshot> sockets;
		std::vector<AnimationPreviewLayerSnapshot> layers;
		std::vector<AnimationPreviewEventSnapshot> events;
		std::vector<AnimationPreviewCurveSnapshot> curves;
		std::vector<AnimationPreviewSlotSnapshot> slots;
		std::vector<AnimationPreviewSlotEventSnapshot> slotEvents;
	};

	struct RuntimeSceneEntitiesCreateRequest
	{
		std::vector<ScenePropertyValue> sceneEntities;
	};

	struct RuntimeSceneEntitiesCreateResult
	{
		bool created = false;
		std::vector<std::string> entityGuids;
		std::string message;
	};

	struct RuntimeEntityDestroyRequest
	{
		std::string entityGuid;
	};

	struct RuntimeEntityDestroyResult
	{
		bool destroyed = false;
	};

	struct RuntimeEntityReparentRequest
	{
		std::string childEntityGuid;
		RuntimeParentReference newParent;
		RuntimeReparentTransformPolicy transformPolicy =
			RuntimeReparentTransformPolicy::KeepWorld;
	};

	struct RuntimeEntityReparentResult
	{
		bool applied = false;
		std::string message;
		RuntimeTransformSnapshot localTransform;
	};

	struct TerrainSettingsSnapshot
	{
		bool available = false;
		bool tessellationEnabled = false;
		float tessellationDistance = 0.0f;
		float maxTessellationLevel = 0.0f;
		float tessellationPower = 0.0f;
		float tessLodBias = 0.0f;
		bool noiseDetailEnabled = false;
		float noiseStrength = 0.0f;
		float noiseFrequency = 0.0f;
		int noiseOctaves = 0;
		float noiseGain = 0.0f;
		float noiseLacunarity = 0.0f;
		float noiseWarpStrength = 0.0f;
		float noiseFadeStart = 0.0f;
		float terrainSize = 0.0f;
		float splitDistMult = 0.0f;
		float lodDistanceRatio = 0.0f;
	};

	struct DirectionalLightSettings
	{
		Vec3 direction = { 0.0f, -1.0f, 0.0f };
		Vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
	};

	struct PointLightSettings
	{
		struct PunctualShadowSettings
		{
			bool castShadows = true;
			int policy = 1;       // Disabled=0, Auto=1, Hero=2, DistanceDynamic=3
			int priority = 128;
			int resolution = 0;   // Auto=0, otherwise 128/256/512/1024
			int updateMode = 1;   // EveryFrame=0, OnChange=1, Budgeted=2
			int fallback = 1;     // None=0, ScreenSpace=1
			float maxDistance = 30.0f;
			float nearPlane = 0.0f;
			float depthBiasTexels = 1.0f;
			float normalBiasTexels = 1.0f;
			float sourceRadius = 0.02f;
			bool affectsFog = true;
			bool affectsGI = true;
			std::uint32_t shadowCasterMask = 0xffffffffu;
		};

		Vec3 position;
		Vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float radius = 1.0f;
		PunctualShadowSettings shadow;
	};

	struct SpotLightSettings
	{
		Vec3 position;
		Vec3 direction = { 0.0f, -1.0f, 0.0f };
		Vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float radius = 1.0f;
		float innerCutoffRadians = 0.0f;
		float outerCutoffRadians = 0.0f;
		PointLightSettings::PunctualShadowSettings shadow;
	};

	struct RectLightSettings
	{
		Vec3 position;
		Vec3 normal = { 0.0f, 0.0f, 1.0f };
		Vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float width = 1.0f;
		float height = 1.0f;
		float range = 10.0f;
		bool twoSided = false;
		PointLightSettings::PunctualShadowSettings shadow;
	};

	struct LightingSettingsSnapshot
	{
		std::vector<DirectionalLightSettings> directionalLights;
		std::vector<PointLightSettings> pointLights;
		std::vector<SpotLightSettings> spotLights;
		std::vector<RectLightSettings> rectLights;
	};

	struct PostProcessSettingsSnapshot
	{
		bool available = false;

		bool enableAutoExposure = false;
		float exposureCompensation = 0.0f;
		float minEV100 = -6.0f;
		float maxEV100 = 16.0f;
		float adaptationSpeedUp = 3.0f;
		float adaptationSpeedDown = 1.0f;

		bool enableBloom = true;
		float bloomThreshold = 1.0f;
		float bloomKnee = 0.5f;
		float bloomIntensity = 0.12f;
		float bloomScatter = 0.7f;
		float bloomClamp = 64.0f;
		float bloomTintR = 1.0f;
		float bloomTintG = 1.0f;
		float bloomTintB = 1.0f;
		int bloomShapeMode = 0;
		float bloomShapeIntensity = 0.35f;
		float bloomShapeBlend = 1.0f;
		float bloomShapeAngleDeg = 0.0f;
		float bloomAnamorphicStretch = 4.0f;
		int bloomStreakCount = 4;
		float bloomStreakLength = 24.0f;
		float bloomStreakAttenuation = 0.72f;

		bool enableDOF = false;
		float focusDistance = 5.0f;
		float focalLengthMm = 50.0f;
		float fStop = 2.8f;
		float sensorHeightMm = 24.0f;
		float maxCoC = 16.0f;
		bool dofBlurTransmissionBackground = true;

		int toneMapperType = 1;
		float whitePoint = 11.2f;

		bool enableColorGrading = true;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float hueShift = 0.0f;
		float temperature = 0.0f;
		float tint = 0.0f;

	};

	struct PlanetSettings
	{
		std::array<double, 3> centerWorldMeters{ 0.0, -6340200.0, 0.0 };
		double bottomRadiusMeters = 6340000.0;
		double atmosphereHeightMeters = 80000.0;
	};

	struct ScenePropertyEdit
	{
		std::string propertyPointer;
		ScenePropertyValue value;
	};

	struct RayleighSettings
	{
		std::array<float, 3> scatteringPerMeterAtGround{ 5.802e-6f, 13.558e-6f, 33.1e-6f };
		float densityScaleHeightMeters = 8500.0f;
	};

	struct MieSettings
	{
		std::array<float, 3> scatteringPerMeterAtGround{ 3.996e-6f, 3.996e-6f, 3.996e-6f };
		std::array<float, 3> absorptionPerMeterAtGround{ 4.4e-6f, 4.4e-6f, 4.4e-6f };
		float densityScaleHeightMeters = 1200.0f;
		float anisotropy = 0.78f;
	};

	struct OzoneSettings
	{
		std::array<float, 3> absorptionPerMeter{ 0.650e-6f, 1.881e-6f, 0.085e-6f };
		float centerAltitudeMeters = 25000.0f;
		float halfWidthMeters = 15000.0f;
	};

	struct CelestialDiskSettings
	{
		bool enabled = true;
		float angularRadiusRadians = 0.018f;
		float featherRadians = 0.0015f;
		float radianceScale = 1.0f;
		float occlusionStrength = 8.0f;
	};

	struct CelestialBodySettings
	{
		std::string name;
		std::string lightEntityId;
		CelestialDiskSettings disk;
	};

	struct AerialPerspectiveSettings
	{
		float distanceScale = 1.0f;
	};

	struct PhysicalAtmosphereSettings
	{
		bool enabled = true;
		std::array<float, 3> groundAlbedo{ 0.4f, 0.4f, 0.4f };
		RayleighSettings rayleigh;
		MieSettings mie;
		OzoneSettings ozone;
		AerialPerspectiveSettings aerialPerspective;
		float mainLightVolumetricScatteringScale = 1.0f;
		std::vector<CelestialBodySettings> celestialBodies;
	};

	struct CloudShadowSettings
	{
		bool enabled = true;
		float atmosphereStrength = 1.0f;
		float ambientOcclusionStrength = 0.25f;
	};

	struct HeightFogSettings
	{
		bool enabled = true;
		float groundHeightWorldMeters = 0.0f;
		float visibilityAtGroundMeters = 600.0f;
		float densityFalloffHeightMeters = 100.0f;
		float startDistanceMeters = 0.0f;
		float nearFadeDistanceMeters = 20.0f;
		float maximumDistanceMeters = 1500.0f;
		float farFadeDistanceMeters = 300.0f;
		std::array<float, 3> singleScatteringAlbedo{ 0.98f, 0.98f, 0.98f };
		float anisotropy = 0.2f;
		std::array<float, 3> emissivePerMeter{ 0.0f, 0.0f, 0.0f };
		float skyLightingScale = 1.0f;
		float mainLightVolumetricScale = 1.0f;
		bool receiveCloudShadows = true;
	};

	struct CloudSettings
	{
		bool enabled = true;
		float cloudMinHeight = 1070.0f;
		float cloudMaxHeight = 7410.0f;
		float density = 0.025f;
		float coverage = 0.350f;
		float sunBrightness = 0.380f;
		float mainTileMeters = 43300.0f;
		float detailTileMeters = 2200.0f;
		float mainHeightScale = 0.260f;
		float detailHeightScale = 3.070f;
		float thresholdLowCoverage = 0.115f;
		float thresholdHighCoverage = 0.720f;
		float densityRemapLow = 0.425f;
		float densityRemapHigh = 0.915f;
		float mainErosionStrength = 1.160f;
		float detailErosionStrength = 1.340f;
		float edgeErosionStrength = 0.500f;
		float verticalShapePower = 1.420f;
		float detailErosionLow = 0.280f;
		float detailErosionHigh = 0.810f;
		float detailEdgeStrength = 0.270f;
		float sigmaTRef = 1.000f;
		float viewAbsorption = 1.000f;
		float lightAbsorption = 1.000f;
		float singleScatteringAlbedo = 0.999f;
		float forwardEccentricity = 0.700f;
		float backwardEccentricity = 0.250f;
		float msAttenuation = 0.500f;
		float msContribution = 0.500f;
		float msEccentricity = 0.500f;
		float scatteringTintR = 1.000f;
		float scatteringTintG = 1.000f;
		float scatteringTintB = 1.000f;
		float scatterSourceODScale = 0.120f;
		float scatterSourceCurvePow = 1.000f;
		float aoUpwardScale = 1.000f;
		float ambientBottomStrength = 0.100f;
		float ambientTopStrength = 0.350f;
		float ambientDuskWarmth = 0.650f;
		float boundaryConfidence = 0.750f;
		float boundaryWrap = 0.350f;
		float phiFwdIntensity = 0.800f;
		float phiFwdDepthPow = 1.000f;
		float phiFwdDepthBias = 0.050f;
		float phiFwdMSBuildScale = 1.000f;
		float phiFwdCompress = 1.000f;
		float phiFwdMaxDistance = 6000.000f;
		float phiFwdConeRatio = 1.450f;
		float phiFwdMinStep = 80.000f;
		float lightStepCount = 8.000f;
		float boundaryGradientStep = 250.000f;
		float boundaryGradientStrength = 0.000f;
		float shadingDebugMode = 0.000f;
		CloudShadowSettings shadow;
	};

	struct EnvironmentSettings
	{
		PlanetSettings planet;
		PhysicalAtmosphereSettings physicalAtmosphere;
		HeightFogSettings heightFog;
		CloudSettings volumetricClouds;
	};

	struct ShaderStageSourceSnapshot
	{
		std::string stage;
		std::string sourcePath;
		std::string entryPoint = "main";
	};

	struct ShaderProgramSourceSnapshot
	{
		std::string programId;
		std::string sourceFolder;
		bool rayTracing = false;
		std::vector<ShaderStageSourceSnapshot> stages;
	};

	struct ShaderCompiledStagePackage
	{
		std::string stage;
		std::string entryPoint = "main";
		std::vector<std::uint32_t> spirv;
	};

	struct ShaderCandidatePackage
	{
		std::string programId;
		std::uint64_t sourceRevision = 0;
		std::vector<ShaderCompiledStagePackage> stages;
	};

	struct ShaderCandidateApplyResult
	{
		bool applied = false;
		std::string programId;
		std::uint64_t sourceRevision = 0;
		std::string error;
	};

	enum class EnginePlayState
	{
		Edit,
		Play,
		Pause
	};
}
