#pragma once

#include "VansRenderFrame.h"
#include "VansRenderFrameSource.h"
#include "VansRenderOutcomeLedger.h"
#include "VansRenderThreadTransaction.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace VansGraphics
{
	class VansCamera;
	class VansGraphicsDevice;

	enum class VansRenderSystemState
	{
		Uninitialized,
		Running,
		Quiesced,
		Stopped,
		Fatal
	};

	enum class VansRenderFrameSubmitStatus
	{
		Accepted,
		BackendFrameFailed,
		InvalidState,
		InvalidOrder
	};

	struct VansRenderFrameSubmitResult
	{
		VansRenderFrameId frameId;
		VansRenderFrameSubmitStatus status = VansRenderFrameSubmitStatus::InvalidState;

		explicit operator bool() const
		{
			return status == VansRenderFrameSubmitStatus::Accepted;
		}
	};

	// Application、Editor 与 Runtime 的唯一帧执行门面。Main 只构建并发布
	// move-only submission；backend frame/control 操作由内部 RenderThread 串行执行。
	// independentRenderThreadEnabled 是 Main/Render N/N-1 重叠的唯一开关；关闭时
	// Main 等待每帧结果，但 backend 仍留在 RenderThread，避免破坏线程归属约束。
	class VansRenderSystem final : public IVansRenderThreadTransactionExecutor
	{
	public:
		VansRenderSystem(
			VansGraphicsDevice& backend,
			IVansRenderFrameSource& frameSource,
			bool independentRenderThreadEnabled = false);
		~VansRenderSystem();

		VansRenderSystem(const VansRenderSystem&) = delete;
		VansRenderSystem& operator=(const VansRenderSystem&) = delete;

		bool InitializeFrameExecution();
		bool BeginFrame(VansCamera& camera);
		VansRenderFrameSubmitResult SubmitFrame(
			std::unique_ptr<IVansRenderFrameOverlay> overlay = nullptr);

		bool RequestSurfaceResize(std::uint32_t width, std::uint32_t height);
		bool WaitForIdle();
		bool Quiesce();
		bool ExecuteRenderThreadTransaction(
			std::unique_ptr<IVansRenderThreadTransaction> transaction) override;
		void ShutdownFrameExecution();

		void InitializeGpuProfiler();

		VansRenderSystemState GetState() const { return m_State; }
		bool IsFrameOpen() const { return m_OpenSubmission.has_value(); }
		VansRenderFrameId GetCurrentFrameId() const { return m_CurrentFrameId; }
		VansSurfaceEpoch GetSurfaceEpoch() const { return m_SurfaceEpoch; }
		std::uint32_t GetRenderWidth() const
		{
			return m_PublishedRenderWidth.load(std::memory_order_acquire);
		}
		std::uint32_t GetRenderHeight() const
		{
			return m_PublishedRenderHeight.load(std::memory_order_acquire);
		}
		const VansRenderFramePacket* GetOpenFrameForDiagnostics() const;
		std::optional<VansRenderFrameOutcome> GetFrameOutcome(VansRenderFrameId frameId) const;

	private:
		struct RenderThreadState;

		void RenderThreadMain();
		void PublishRenderExtentFromBackend();
		bool EnqueueAndWait(
			std::uint32_t type,
			std::uint32_t width = 0,
			std::uint32_t height = 0,
			std::unique_ptr<IVansRenderThreadTransaction> transaction = nullptr);
		void StopAndJoinRenderThread();

		VansGraphicsDevice& m_Backend;
		IVansRenderFrameSource& m_FrameSource;
		bool m_IndependentRenderThreadEnabled = false;
		VansRenderSystemState m_State = VansRenderSystemState::Uninitialized;
		std::uint64_t m_NextRenderFrameValue = 0;
		std::uint64_t m_NextLogicFrameValue = 0;
		std::uint64_t m_NextWorkSerialValue = 0;
		VansRenderFrameId m_CurrentFrameId;
		VansRenderWorkSerial m_CurrentWorkSerial;
		std::optional<VansRenderFrameId> m_LastAcceptedFrameId;
		VansSurfaceEpoch m_SurfaceEpoch;
		std::uint64_t m_QueuedGpuWorkGeneration = 0;
		std::optional<std::uint64_t> m_LastGpuIdleGeneration;
		std::atomic<std::uint32_t> m_PublishedRenderWidth{ 0 };
		std::atomic<std::uint32_t> m_PublishedRenderHeight{ 0 };
		std::optional<VansRenderFrameSubmission> m_OpenSubmission;
		VansRenderOutcomeLedger m_OutcomeLedger;
		std::unique_ptr<RenderThreadState> m_RenderThreadState;
	};
}
