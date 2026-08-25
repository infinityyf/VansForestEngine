#pragma once

#include "VansRenderSceneSnapshot.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace VansGraphics
{
	class VansGraphicsDevice;

	// 可选的帧拥有型 overlay 录制载荷。生产者必须深拷贝 frontend 数据；
	// Record 只会在 RenderThread 的场景录制与 Present 之间调用。
	class IVansRenderFrameOverlay
	{
	public:
		virtual ~IVansRenderFrameOverlay() = default;
		virtual bool Record(VansGraphicsDevice& device) = 0;
	};

	template<typename Tag>
	class VansRenderSerial final
	{
	public:
		constexpr VansRenderSerial() = default;
		explicit constexpr VansRenderSerial(std::uint64_t value)
			: m_Value(value)
		{
		}

		constexpr std::uint64_t Value() const { return m_Value; }

		friend constexpr bool operator==(VansRenderSerial left, VansRenderSerial right)
		{
			return left.m_Value == right.m_Value;
		}

		friend constexpr bool operator!=(VansRenderSerial left, VansRenderSerial right)
		{
			return !(left == right);
		}

		friend constexpr bool operator<(VansRenderSerial left, VansRenderSerial right)
		{
			return left.m_Value < right.m_Value;
		}

	private:
		std::uint64_t m_Value = 0;
	};

	struct VansLogicFrameIdTag;
	struct VansRenderFrameIdTag;
	struct VansSurfaceEpochTag;
	struct VansRenderWorkSerialTag;

	using VansLogicFrameId = VansRenderSerial<VansLogicFrameIdTag>;
	using VansRenderFrameId = VansRenderSerial<VansRenderFrameIdTag>;
	using VansSurfaceEpoch = VansRenderSerial<VansSurfaceEpochTag>;

	enum class VansRenderSubmissionPrepareStatus
	{
		Ready,
		RecoverableFailure,
		FatalProtocolViolation
	};

	struct VansRenderSubmissionPrepareResult final
	{
		VansRenderSubmissionPrepareStatus status =
			VansRenderSubmissionPrepareStatus::RecoverableFailure;
		std::string error;

		explicit operator bool() const
		{
			return status == VansRenderSubmissionPrepareStatus::Ready;
		}
	};
	using VansRenderWorkSerial = VansRenderSerial<VansRenderWorkSerialTag>;

	enum class VansRenderViewHistoryReset : std::uint32_t
	{
		None = 0,
		FirstFrame = 1u << 0u,
		CameraCut = 1u << 1u,
		SceneChanged = 1u << 2u,
		SurfaceChanged = 1u << 3u,
		UpscalerChanged = 1u << 4u
	};

	constexpr VansRenderViewHistoryReset operator|(
		VansRenderViewHistoryReset left,
		VansRenderViewHistoryReset right)
	{
		return static_cast<VansRenderViewHistoryReset>(
			static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
	}

	constexpr bool HasRenderViewHistoryReset(
		VansRenderViewHistoryReset value,
		VansRenderViewHistoryReset flag)
	{
		return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
	}

	// Main 已解析的相机值；不允许携带 backend handle、descriptor、Scene 指针
	// 或 GPU 时序历史。
	struct VansRenderViewSnapshot final
	{
		// Main 生成的不透明摄像机身份。RT 只比较数值以判断时序历史是否跨摄像机，
		// 不会把它还原成指针或访问摄像机对象。
		std::uint64_t cameraIdentity = 0;
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
		glm::vec3 position{ 0.0f };
		float nearClip = 0.1f;
		glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
		float farClip = 1000.0f;
		glm::vec3 up{ 0.0f, 1.0f, 0.0f };
		float fieldOfViewRadians = 1.0f;
		glm::vec3 right{ 1.0f, 0.0f, 0.0f };
		float aspectRatio = 1.0f;
		std::uint32_t viewportWidth = 0;
		std::uint32_t viewportHeight = 0;
		VansRenderViewHistoryReset historyReset = VansRenderViewHistoryReset::None;
	};

	struct VansRenderFrameTimingSnapshot final
	{
		double elapsedSeconds = 0.0;
		// Simulation time is allowed to stop in Editor/Pause. Render-only temporal
		// systems still need wall-clock frame time so their history remains valid.
		double deltaSeconds = 0.0;
		double renderDeltaSeconds = 0.0;
	};

	class VansRenderFrameBuilder;

	// 已发布帧没有修改接口，只能由 builder 创建，并以 const 视图或 move 交给 RT。
	class VansRenderFramePacket final
	{
	public:
		VansRenderFramePacket(const VansRenderFramePacket&) = delete;
		VansRenderFramePacket(VansRenderFramePacket&&) noexcept = default;
		VansRenderFramePacket& operator=(const VansRenderFramePacket&) = delete;
		VansRenderFramePacket& operator=(VansRenderFramePacket&&) = delete;

		VansRenderFrameId FrameId() const { return m_FrameId; }
		VansLogicFrameId SourceLogicFrameId() const { return m_SourceLogicFrameId; }
		VansSurfaceEpoch SurfaceEpoch() const { return m_SurfaceEpoch; }
		const VansRenderViewSnapshot& View() const { return m_View; }
		const VansRenderFrameTimingSnapshot& Timing() const { return m_Timing; }
		const VansRenderSceneFrameSnapshot& Scene() const { return m_Scene; }

	private:
		friend class VansRenderFrameBuilder;
		friend class VansRenderFrameSubmission;

		VansRenderSceneFrameSnapshot TakeSceneForRendering()
		{
			return std::move(m_Scene);
		}

		VansRenderFramePacket(
			VansRenderFrameId frameId,
			VansLogicFrameId sourceLogicFrameId,
			VansSurfaceEpoch surfaceEpoch,
			VansRenderViewSnapshot view,
			VansRenderFrameTimingSnapshot timing,
			VansRenderSceneFrameSnapshot scene)
			: m_FrameId(frameId),
			  m_SourceLogicFrameId(sourceLogicFrameId),
			  m_SurfaceEpoch(surfaceEpoch),
			  m_View(std::move(view)),
			  m_Timing(timing),
			  m_Scene(std::move(scene))
		{
		}

		VansRenderFrameId m_FrameId;
		VansLogicFrameId m_SourceLogicFrameId;
		VansSurfaceEpoch m_SurfaceEpoch;
		VansRenderViewSnapshot m_View;
		VansRenderFrameTimingSnapshot m_Timing;
		VansRenderSceneFrameSnapshot m_Scene;
	};

	// 一个 work serial 下的 proxy/resource mutation、frame 与 overlay 原子排序，
	// move-only 所有权只允许从 Main 转移到 RenderThread。
	class VansRenderFrameSubmission final
	{
	public:
		VansRenderFrameSubmission(
			VansRenderWorkSerial workSerial,
			VansRenderMutationBatch mutationsBeforeFrame,
			VansRenderFramePacket frame)
			: m_WorkSerial(workSerial),
			  m_MutationsBeforeFrame(std::move(mutationsBeforeFrame)),
			  m_Frame(std::move(frame))
		{
		}

		VansRenderFrameSubmission(const VansRenderFrameSubmission&) = delete;
		VansRenderFrameSubmission& operator=(const VansRenderFrameSubmission&) = delete;
		VansRenderFrameSubmission(VansRenderFrameSubmission&&) noexcept = default;
		VansRenderFrameSubmission& operator=(VansRenderFrameSubmission&&) = delete;

		VansRenderWorkSerial WorkSerial() const { return m_WorkSerial; }
		const VansRenderMutationBatch& MutationsBeforeFrame() const
		{
			return m_MutationsBeforeFrame;
		}
		const VansRenderFramePacket& Frame() const { return m_Frame; }
		// 发布后的 packet 在 Main 侧保持只读；进入 RenderThread 后，backend
		// 通过 submission 一次性接管场景快照，避免再次深拷贝整帧 vector。
		bool ConsumeSceneForRendering(VansRenderSceneFrameSnapshot& output)
		{
			if (m_SceneConsumed)
				return false;
			output = m_Frame.TakeSceneForRendering();
			m_SceneConsumed = true;
			return true;
		}
		bool AttachOverlay(std::unique_ptr<IVansRenderFrameOverlay> overlay)
		{
			if (m_Overlay || !overlay)
				return false;
			m_Overlay = std::move(overlay);
			return true;
		}
		IVansRenderFrameOverlay* Overlay() const { return m_Overlay.get(); }

	private:
		VansRenderWorkSerial m_WorkSerial;
		VansRenderMutationBatch m_MutationsBeforeFrame;
		VansRenderFramePacket m_Frame;
		std::unique_ptr<IVansRenderFrameOverlay> m_Overlay;
		bool m_SceneConsumed = false;
	};

	class VansRenderFrameBuilder final
	{
	public:
		VansRenderFrameBuilder(
			VansRenderFrameId frameId,
			VansLogicFrameId sourceLogicFrameId,
			VansSurfaceEpoch surfaceEpoch);

		VansRenderFrameBuilder(const VansRenderFrameBuilder&) = delete;
		VansRenderFrameBuilder& operator=(const VansRenderFrameBuilder&) = delete;

		bool SetView(VansRenderViewSnapshot view);
		bool SetTiming(VansRenderFrameTimingSnapshot timing);
		bool SetScene(VansRenderSceneFrameSnapshot scene);
		std::optional<VansRenderFramePacket> Finalize() &&;

	private:
		VansRenderFrameId m_FrameId;
		VansLogicFrameId m_SourceLogicFrameId;
		VansSurfaceEpoch m_SurfaceEpoch;
		std::optional<VansRenderViewSnapshot> m_View;
		std::optional<VansRenderFrameTimingSnapshot> m_Timing;
		std::optional<VansRenderSceneFrameSnapshot> m_Scene;
		bool m_Finalized = false;
	};
}
