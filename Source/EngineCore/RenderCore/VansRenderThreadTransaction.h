#pragma once

#include <memory>

namespace VansGraphics
{
	class VansGraphicsDevice;

	// 低频且必须显式排序的渲染维护事务。事务拥有载荷，并与帧工作在同一条
	// RenderThread 流中执行；逐帧数据必须继续使用 VansRenderFrameSubmission。
	class IVansRenderThreadTransaction
	{
	public:
		virtual ~IVansRenderThreadTransaction() = default;
		virtual bool Execute(VansGraphicsDevice& backend) = 0;
	};

	// Scene-owned 系统把低频 GPU/资源结构变更交给 RenderThread 的窄调度边界。
	class IVansRenderThreadTransactionExecutor
	{
	public:
		virtual ~IVansRenderThreadTransactionExecutor() = default;
		virtual bool ExecuteRenderThreadTransaction(
			std::unique_ptr<IVansRenderThreadTransaction> transaction) = 0;
	};
}
