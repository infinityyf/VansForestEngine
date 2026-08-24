#pragma once

#include "VansRenderFrame.h"
#include "VansRenderFrameSource.h"
#include "VansRenderOutcomeLedger.h"
#include "VansRenderThreadTransaction.h"

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
	// Editor 与 Runtime 生产入口使用 maxGameFramesAhead=1，实现 N/N-1 重叠；
	// 0 只保留给确定性协议测试和显式同步诊断，同样不会回退到 Main 渲染。
	class VansRenderSystem final : public IVansRenderThreadTransactionExecutor
	{
	public:
		VansRenderSystem(
			VansGraphicsDevice& backend,
			IVansRenderFrameSource& frameSource,
			std::uint32_t maxGameFramesAhead = 0);
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
		void EndGpuProfilerFrame();

		VansRenderSystemState GetState() const { return m_State; }
		bool IsFrameOpen() const { return m_OpenSubmission.has_value(); }
		VansRenderFrameId GetCurrentFrameId() const { return m_CurrentFrameId; }
		VansSurfaceEpoch GetSurfaceEpoch() const { return m_SurfaceEpoch; }
		const VansRenderFramePacket* GetOpenFrameForDiagnostics() const;
		std::optional<VansRenderFrameOutcome> GetFrameOutcome(VansRenderFrameId frameId) const;

	private:
		struct RenderThreadState;

		void RenderThreadMain();
		bool EnqueueControlAndWait(
			std::uint32_t type,
			std::uint32_t width = 0,
			std::uint32_t height = 0);

		VansGraphicsDevice& m_Backend;
		IVansRenderFrameSource& m_FrameSource;
		std::uint32_t m_MaxGameFramesAhead = 0;
		VansRenderSystemState m_State = VansRenderSystemState::Uninitialized;
		std::uint64_t m_NextRenderFrameValue = 0;
		std::uint64_t m_NextLogicFrameValue = 0;
		std::uint64_t m_NextWorkSerialValue = 0;
		VansRenderFrameId m_CurrentFrameId;
		VansRenderWorkSerial m_CurrentWorkSerial;
		std::optional<VansRenderFrameId> m_LastAcceptedFrameId;
		VansSurfaceEpoch m_SurfaceEpoch;
		std::optional<VansRenderFrameSubmission> m_OpenSubmission;
		VansRenderOutcomeLedger m_OutcomeLedger;
		std::unique_ptr<RenderThreadState> m_RenderThreadState;
	};
}
