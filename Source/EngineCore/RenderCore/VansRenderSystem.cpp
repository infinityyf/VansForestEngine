#include "VansRenderSystem.h"

#include "VansCamera.h"
#include "VansGraphicsDevice.h"
#include "../VansTimer.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"

#include <cassert>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace
{
	enum class RenderWorkType : std::uint32_t
	{
		Frame = 0,
		Resize,
		WaitIdle,
		InitializeProfiler,
		Transaction,
		Stop
	};

	struct RenderControlCompletion final
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool completed = false;
		bool succeeded = false;
	};

	struct RenderWorkItem final
	{
		RenderWorkType type = RenderWorkType::Frame;
		std::unique_ptr<VansGraphics::VansRenderFrameSubmission> frame;
		std::unique_ptr<VansGraphics::IVansRenderThreadTransaction> transaction;
		std::shared_ptr<RenderControlCompletion> completion;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint64_t profilerFrameIndex = Vans::VansProfiler::INVALID_FRAME_INDEX;
	};

	void CompleteControl(
		const std::shared_ptr<RenderControlCompletion>& completion,
		bool succeeded)
	{
		if (!completion)
			return;
		{
			std::lock_guard<std::mutex> lock(completion->mutex);
			completion->succeeded = succeeded;
			completion->completed = true;
		}
		completion->condition.notify_all();
	}
}

struct VansGraphics::VansRenderSystem::RenderThreadState final
{
	std::mutex mutex;
	std::condition_variable workAvailable;
	std::deque<RenderWorkItem> workQueue;
	std::thread thread;
	bool startupCompleted = false;
	bool startupSucceeded = false;
	bool threadExited = false;
};

VansGraphics::VansRenderSystem::VansRenderSystem(
	VansGraphicsDevice& backend,
	IVansRenderFrameSource& frameSource,
	bool independentRenderThreadEnabled)
	: m_Backend(backend),
	  m_FrameSource(frameSource),
	  m_IndependentRenderThreadEnabled(independentRenderThreadEnabled),
	  m_OutcomeLedger(1)
{
}

VansGraphics::VansRenderSystem::~VansRenderSystem()
{
	// 析构必须是最终安全网：调用方即使漏掉显式 Shutdown，也不能让
	// RenderThread 永久阻塞在空队列上。显式关闭仍用于保证业务销毁顺序。
	if (m_OpenSubmission.has_value())
	{
		VANS_LOG_ERROR("[RenderSystem] Discarding an unpublished frame during destruction. frameId="
			<< m_CurrentFrameId.Value());
		m_OpenSubmission.reset();
		m_OutcomeLedger.SignalFatal(
			"RenderSystem destroyed while a frame submission was still open");
	}
	try
	{
		StopAndJoinRenderThread();
	}
	catch (...)
	{
		// 析构不能抛出；joinable 线程仍必须回收，避免 std::terminate。
		if (m_RenderThreadState && m_RenderThreadState->thread.joinable())
			m_RenderThreadState->thread.join();
	}
}

bool VansGraphics::VansRenderSystem::InitializeFrameExecution()
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_State == VansRenderSystemState::Running)
		return true;
	if (m_State != VansRenderSystemState::Uninitialized)
		return false;

	m_RenderThreadState = std::make_unique<RenderThreadState>();
	m_RenderThreadState->thread = std::thread(&VansRenderSystem::RenderThreadMain, this);
	{
		std::unique_lock<std::mutex> lock(m_RenderThreadState->mutex);
		m_RenderThreadState->workAvailable.wait(lock, [this]
		{
			return m_RenderThreadState->startupCompleted;
		});
		if (!m_RenderThreadState->startupSucceeded)
		{
			lock.unlock();
			m_RenderThreadState->thread.join();
			m_RenderThreadState.reset();
			return false;
		}
	}
	m_State = VansRenderSystemState::Running;
	VANS_LOG("[RenderSystem] Dedicated render thread initialized; independentExecution="
		<< (m_IndependentRenderThreadEnabled ? "true" : "false"));
	return true;
}

bool VansGraphics::VansRenderSystem::BeginFrame(VansCamera& camera)
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_State != VansRenderSystemState::Running || m_OpenSubmission.has_value())
		return false;

	m_CurrentFrameId = VansRenderFrameId(m_NextRenderFrameValue++);
	const VansLogicFrameId logicFrameId(m_NextLogicFrameValue++);
	const std::uint32_t width = GetRenderWidth();
	const std::uint32_t height = GetRenderHeight();
	if (width == 0 || height == 0)
		return false;
	VansRenderViewSnapshot view = camera.BuildRenderViewSnapshot(width, height);
	if (m_CurrentFrameId.Value() == 0)
		view.historyReset = view.historyReset | VansRenderViewHistoryReset::FirstFrame;
	VansRenderFrameTimingSnapshot timing;
	timing.elapsedSeconds = VansTimer::GetFrameTime();
	timing.deltaSeconds = VansTimer::GetLastFrameDelta();
	timing.renderDeltaSeconds = VansTimer::GetRealDeltaTime();
	VansRenderFramePreparationContext preparationContext;
	preparationContext.frameId = m_CurrentFrameId;
	preparationContext.logicFrameId = logicFrameId;
	preparationContext.surfaceEpoch = m_SurfaceEpoch;
	preparationContext.view = view;
	preparationContext.timing = timing;

	VANS_SET_FRAME_PHASE(VansFramePhase::RenderPrep);
	auto sourceOutput = m_FrameSource.PrepareMainThreadRenderFrame(preparationContext);
	if (!sourceOutput.has_value())
	{
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
		return false;
	}

	VansRenderFrameBuilder builder(m_CurrentFrameId, logicFrameId, m_SurfaceEpoch);
	if (!builder.SetView(std::move(view)) || !builder.SetTiming(timing) ||
		!builder.SetScene(std::move(sourceOutput->scene)))
	{
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
		return false;
	}
	auto packet = std::move(builder).Finalize();
	if (!packet.has_value())
	{
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
		return false;
	}
	m_CurrentWorkSerial = VansRenderWorkSerial(m_NextWorkSerialValue++);
	m_OpenSubmission.emplace(
		m_CurrentWorkSerial,
		std::move(sourceOutput->mutationsBeforeFrame),
		std::move(*packet));

	VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
	return true;
}

VansGraphics::VansRenderFrameSubmitResult VansGraphics::VansRenderSystem::SubmitFrame(
	std::unique_ptr<IVansRenderFrameOverlay> overlay)
{
	VANS_ASSERT_MAIN_THREAD();
	VansRenderFrameSubmitResult result;
	result.frameId = m_CurrentFrameId;

	if (m_State != VansRenderSystemState::Running)
	{
		result.status = VansRenderFrameSubmitStatus::InvalidState;
		return result;
	}
	if (!m_OpenSubmission.has_value())
	{
		result.status = VansRenderFrameSubmitStatus::InvalidOrder;
		return result;
	}
	if (overlay && !m_OpenSubmission->AttachOverlay(std::move(overlay)))
	{
		result.status = VansRenderFrameSubmitStatus::InvalidOrder;
		return result;
	}
	// 一帧领先额度由上一条已接收帧的终态释放。等待固定放在当前帧发布前，
	// 因此 Main 构建 N 时，RenderThread 可以完整执行 N-1。
	if (m_IndependentRenderThreadEnabled && m_LastAcceptedFrameId.has_value() &&
		!m_OutcomeLedger.LeadCreditReleasedFor(*m_LastAcceptedFrameId))
	{
		const VansRenderOutcomeWaitResult previous =
			m_OutcomeLedger.WaitForOutcome(*m_LastAcceptedFrameId);
		if (previous.status != VansRenderOutcomeWaitStatus::OutcomeAvailable ||
			!previous.outcome.has_value() ||
			previous.outcome->status == VansRenderFrameStatus::FatalProtocolViolation ||
			previous.outcome->status == VansRenderFrameStatus::FatalDeviceLost)
		{
			m_OpenSubmission.reset();
			m_State = VansRenderSystemState::Fatal;
			result.status = VansRenderFrameSubmitStatus::BackendFrameFailed;
			return result;
		}
	}

	if (!m_OutcomeLedger.TryAcceptFrame(m_CurrentWorkSerial, m_CurrentFrameId))
	{
		VANS_LOG_ERROR("[RenderSystem] Frame rejected by the ordered outcome ledger. frameId="
			<< m_CurrentFrameId.Value());
		m_OpenSubmission.reset();
		result.status = VansRenderFrameSubmitStatus::InvalidOrder;
		return result;
	}
	m_LastAcceptedFrameId = m_CurrentFrameId;

	auto frameWork = std::make_unique<VansRenderFrameSubmission>(std::move(*m_OpenSubmission));
	m_OpenSubmission.reset();
	const std::uint64_t profilerFrameIndex =
		Vans::VansProfiler::Get().GetActiveFrameIndex();
	if (profilerFrameIndex != Vans::VansProfiler::INVALID_FRAME_INDEX)
		Vans::VansProfiler::Get().MarkCurrentFrameHasRender();
	{
		std::lock_guard<std::mutex> lock(m_RenderThreadState->mutex);
		RenderWorkItem item;
		item.type = RenderWorkType::Frame;
		item.frame = std::move(frameWork);
		item.profilerFrameIndex = profilerFrameIndex;
		m_RenderThreadState->workQueue.emplace_back(std::move(item));
		++m_QueuedGpuWorkGeneration;
	}
	m_RenderThreadState->workAvailable.notify_one();
	if (m_IndependentRenderThreadEnabled)
	{
		result.status = VansRenderFrameSubmitStatus::Accepted;
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
		return result;
	}

	// 关闭独立执行仅用于确定性协议测试或显式同步诊断；即使在该模式下，
	// backend 仍只在 RenderThread 执行，不存在 Main inline 渲染旁路。
	const VansRenderOutcomeWaitResult waitResult =
		m_OutcomeLedger.WaitForOutcome(m_CurrentFrameId);
	if (waitResult.status != VansRenderOutcomeWaitStatus::OutcomeAvailable ||
		!waitResult.outcome.has_value())
	{
		m_State = VansRenderSystemState::Fatal;
		result.status = VansRenderFrameSubmitStatus::BackendFrameFailed;
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
		return result;
	}

	const VansRenderFrameStatus outcomeStatus = waitResult.outcome->status;
	if (outcomeStatus == VansRenderFrameStatus::FatalProtocolViolation ||
		outcomeStatus == VansRenderFrameStatus::FatalDeviceLost)
	{
		m_State = VansRenderSystemState::Fatal;
		result.status = VansRenderFrameSubmitStatus::BackendFrameFailed;
	}
	else if (outcomeStatus == VansRenderFrameStatus::RecoverableFailure)
	{
		result.status = VansRenderFrameSubmitStatus::BackendFrameFailed;
	}
	else
	{
		result.status = VansRenderFrameSubmitStatus::Accepted;
	}
	VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
	return result;
}

bool VansGraphics::VansRenderSystem::RequestSurfaceResize(
	std::uint32_t width,
	std::uint32_t height)
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_State != VansRenderSystemState::Running || m_OpenSubmission.has_value() || width == 0 || height == 0)
		return false;

	if (!EnqueueAndWait(
		static_cast<std::uint32_t>(RenderWorkType::Resize), width, height))
	{
		return false;
	}
	m_SurfaceEpoch = VansSurfaceEpoch(m_SurfaceEpoch.Value() + 1);
	return true;
}

bool VansGraphics::VansRenderSystem::WaitForIdle()
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_State == VansRenderSystemState::Uninitialized ||
		m_State == VansRenderSystemState::Stopped)
		return true;
	if (m_OpenSubmission.has_value())
		return false;
	if (m_LastGpuIdleGeneration.has_value() &&
		*m_LastGpuIdleGeneration == m_QueuedGpuWorkGeneration)
	{
		return true;
	}
	const bool succeeded = EnqueueAndWait(
		static_cast<std::uint32_t>(RenderWorkType::WaitIdle));
	if (succeeded)
		m_LastGpuIdleGeneration = m_QueuedGpuWorkGeneration;
	return succeeded;
}

bool VansGraphics::VansRenderSystem::Quiesce()
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_State == VansRenderSystemState::Quiesced)
		return true;
	if (m_State != VansRenderSystemState::Running || m_OpenSubmission.has_value())
		return false;
	if (!WaitForIdle())
		return false;

	m_State = VansRenderSystemState::Quiesced;
	return true;
}

bool VansGraphics::VansRenderSystem::ExecuteRenderThreadTransaction(
	std::unique_ptr<IVansRenderThreadTransaction> transaction)
{
	VANS_ASSERT_MAIN_THREAD();
	if (!transaction || !m_RenderThreadState ||
		(m_State != VansRenderSystemState::Running &&
		 m_State != VansRenderSystemState::Quiesced) ||
		m_OpenSubmission.has_value())
	{
		return false;
	}

	return EnqueueAndWait(
		static_cast<std::uint32_t>(RenderWorkType::Transaction),
		0,
		0,
		std::move(transaction));
}

void VansGraphics::VansRenderSystem::ShutdownFrameExecution()
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_State == VansRenderSystemState::Stopped)
		return;
	if (m_State == VansRenderSystemState::Uninitialized)
	{
		m_State = VansRenderSystemState::Stopped;
		m_OutcomeLedger.SignalStopped();
		return;
	}
	if (m_OpenSubmission.has_value())
	{
		VANS_LOG_ERROR("[RenderSystem] Cannot shut down while a frame is open. frameId="
			<< m_CurrentFrameId.Value());
		m_State = VansRenderSystemState::Fatal;
		m_OutcomeLedger.SignalFatal("RenderSystem shutdown requested while a frame is open");
		m_OpenSubmission.reset();
	}

	StopAndJoinRenderThread();
	m_State = VansRenderSystemState::Stopped;
	m_OutcomeLedger.SignalStopped();
}

const VansGraphics::VansRenderFramePacket*
VansGraphics::VansRenderSystem::GetOpenFrameForDiagnostics() const
{
	return m_OpenSubmission.has_value() ? &m_OpenSubmission->Frame() : nullptr;
}

std::optional<VansGraphics::VansRenderFrameOutcome>
VansGraphics::VansRenderSystem::GetFrameOutcome(VansRenderFrameId frameId) const
{
	return m_OutcomeLedger.FindOutcome(frameId);
}

void VansGraphics::VansRenderSystem::InitializeGpuProfiler()
{
	VANS_ASSERT_MAIN_THREAD();
	if (m_State == VansRenderSystemState::Running)
		EnqueueAndWait(
			static_cast<std::uint32_t>(RenderWorkType::InitializeProfiler));
}

bool VansGraphics::VansRenderSystem::EnqueueAndWait(
	std::uint32_t type,
	std::uint32_t width,
	std::uint32_t height,
	std::unique_ptr<IVansRenderThreadTransaction> transaction)
{
	if (!m_RenderThreadState || !m_RenderThreadState->thread.joinable())
		return false;

	const RenderWorkType workType = static_cast<RenderWorkType>(type);
	if ((workType == RenderWorkType::Transaction) != static_cast<bool>(transaction))
		return false;

	auto completion = std::make_shared<RenderControlCompletion>();
	{
		std::lock_guard<std::mutex> lock(m_RenderThreadState->mutex);
		if (m_RenderThreadState->threadExited)
			return false;
		RenderWorkItem item;
		item.type = workType;
		item.transaction = std::move(transaction);
		item.completion = completion;
		item.width = width;
		item.height = height;
		m_RenderThreadState->workQueue.emplace_back(std::move(item));
		if (workType != RenderWorkType::WaitIdle && workType != RenderWorkType::Stop)
			++m_QueuedGpuWorkGeneration;
	}
	m_RenderThreadState->workAvailable.notify_one();

	std::unique_lock<std::mutex> lock(completion->mutex);
	completion->condition.wait(lock, [&completion]
	{
		return completion->completed;
	});
	return completion->succeeded;
}

void VansGraphics::VansRenderSystem::StopAndJoinRenderThread()
{
	if (!m_RenderThreadState || !m_RenderThreadState->thread.joinable())
		return;

	bool alreadyExited = false;
	{
		std::lock_guard<std::mutex> lock(m_RenderThreadState->mutex);
		alreadyExited = m_RenderThreadState->threadExited;
	}
	if (!alreadyExited)
		EnqueueAndWait(static_cast<std::uint32_t>(RenderWorkType::Stop));
	m_RenderThreadState->thread.join();
}

void VansGraphics::VansRenderSystem::PublishRenderExtentFromBackend()
{
	m_PublishedRenderWidth.store(
		static_cast<std::uint32_t>(m_Backend.GetNativeRenderWidth()),
		std::memory_order_release);
	m_PublishedRenderHeight.store(
		static_cast<std::uint32_t>(m_Backend.GetNativeRenderHeight()),
		std::memory_order_release);
}

void VansGraphics::VansRenderSystem::RenderThreadMain()
{
	VANS_INIT_RENDER_THREAD();
	VANS_PROFILE_THREAD("Render Thread");
	bool startupSucceeded = false;
	try
	{
		startupSucceeded = m_Backend.BeforeRendering();
	}
	catch (...)
	{
		startupSucceeded = false;
	}
	if (startupSucceeded)
		PublishRenderExtentFromBackend();
	{
		std::lock_guard<std::mutex> lock(m_RenderThreadState->mutex);
		m_RenderThreadState->startupSucceeded = startupSucceeded;
		m_RenderThreadState->startupCompleted = true;
	}
	m_RenderThreadState->workAvailable.notify_all();
	if (!startupSucceeded)
	{
		// BeforeRendering 允许在部分资源创建后报告失败；统一在同一条
		// RenderThread 上回滚，不能把半初始化 backend 暴露为 Running。
		try
		{
			m_Backend.AfterRendering();
		}
		catch (...)
		{
		}
		{
			std::lock_guard<std::mutex> lock(m_RenderThreadState->mutex);
			m_RenderThreadState->threadExited = true;
		}
		m_RenderThreadState->workAvailable.notify_all();
		VANS_CLEAR_THREAD_ROLE();
		return;
	}

	bool running = true;
	while (running)
	{
		RenderWorkItem item;
		{
			std::unique_lock<std::mutex> lock(m_RenderThreadState->mutex);
			m_RenderThreadState->workAvailable.wait(lock, [this]
			{
				return !m_RenderThreadState->workQueue.empty();
			});
			item = std::move(m_RenderThreadState->workQueue.front());
			m_RenderThreadState->workQueue.pop_front();
		}

		try
		{
		switch (item.type)
		{
		case RenderWorkType::Frame:
		{
			if (!item.frame)
				break;
			VansRenderFrameOutcome outcome;
			outcome.workSerial = item.frame->WorkSerial();
			outcome.frameId = item.frame->Frame().FrameId();
			outcome.surfaceEpoch = item.frame->Frame().SurfaceEpoch();
			VansRenderSubmissionPrepareResult prepareResult;
			bool overlaySucceeded = true;
			Vans::VansProfiler::Get().BeginRenderFrame(item.profilerFrameIndex);
			try
			{
				m_Backend.PrepareRenderingFrame();
				VANS_SET_FRAME_PHASE(VansFramePhase::RenderThreadConsume);
				prepareResult = m_Backend.PrepareRenderSubmission(*item.frame);
				if (prepareResult)
				{
					m_Backend.Rendering();
					if (IVansRenderFrameOverlay* overlay = item.frame->Overlay())
						overlaySucceeded = overlay->Record(m_Backend);
					if (m_Backend.CanRecordCurrentFrame())
						m_Backend.Present();
				}
			}
			catch (...)
			{
				prepareResult = {
					VansRenderSubmissionPrepareStatus::FatalProtocolViolation,
					"Unhandled exception escaped render-thread frame execution"
				};
			}
			Vans::VansProfiler::Get().EndRenderFrame(
				item.profilerFrameIndex,
				m_Backend.GetNativeGraphicsDevice());

			if (prepareResult && overlaySucceeded && m_Backend.CanRecordCurrentFrame())
			{
				outcome.status = VansRenderFrameStatus::PresentQueued;
			}
			else if (prepareResult.status ==
				VansRenderSubmissionPrepareStatus::FatalProtocolViolation)
			{
				outcome.status = VansRenderFrameStatus::FatalProtocolViolation;
				outcome.error = prepareResult.error;
			}
			else
			{
				outcome.status = VansRenderFrameStatus::RecoverableFailure;
				outcome.error = !overlaySucceeded
					? "Render-thread overlay recording failed"
					: (prepareResult.error.empty()
						? "Render backend failed before present was queued"
						: prepareResult.error);
			}
			if (!m_OutcomeLedger.PublishOutcome(std::move(outcome)))
				m_OutcomeLedger.SignalFatal(
					"Render frame outcome violated ordered ledger state");
			VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
			break;
		}
		case RenderWorkType::Resize:
			m_Backend.OnWindowResize(item.width, item.height);
			CompleteControl(item.completion, true);
			break;
		case RenderWorkType::WaitIdle:
			CompleteControl(item.completion, m_Backend.WaitForIdle());
			break;
		case RenderWorkType::InitializeProfiler:
			m_Backend.InitializeGpuProfiler();
			CompleteControl(item.completion, true);
			break;
		case RenderWorkType::Transaction:
		{
			bool succeeded = false;
			try
			{
				succeeded = item.transaction && item.transaction->Execute(m_Backend);
			}
			catch (...)
			{
				succeeded = false;
			}
			CompleteControl(item.completion, succeeded);
			break;
		}
		case RenderWorkType::Stop:
			m_Backend.AfterRendering();
			CompleteControl(item.completion, true);
			running = false;
			break;
		}
		}
		catch (...)
		{
			CompleteControl(item.completion, false);
			m_OutcomeLedger.SignalFatal(
				"Unhandled exception escaped render-thread control execution");
			running = false;
		}
		if (item.type != RenderWorkType::Stop)
			PublishRenderExtentFromBackend();
	}

	std::deque<RenderWorkItem> abandonedWork;
	{
		std::lock_guard<std::mutex> lock(m_RenderThreadState->mutex);
		m_RenderThreadState->threadExited = true;
		abandonedWork.swap(m_RenderThreadState->workQueue);
	}
	for (RenderWorkItem& abandoned : abandonedWork)
		CompleteControl(abandoned.completion, false);
	m_RenderThreadState->workAvailable.notify_all();
	VANS_CLEAR_THREAD_ROLE();
}
