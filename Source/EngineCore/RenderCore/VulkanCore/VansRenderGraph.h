#pragma once

#include "vulkan/vulkan.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace VansGraphics
{
	class VansVKCommandBuffer;

	enum class VansRenderQueueClass
	{
		Graphics,
		Compute,
		AsyncCompute,
		Present
	};

	enum class VansRenderResourceUsage
	{
		ColorAttachmentWrite,
		DepthStencilAttachmentRead,
		DepthStencilAttachmentWrite,
		SampledRead,
		StorageRead,
		StorageWrite,
		TransferSrc,
		TransferDst,
		IndirectArgumentRead,
		AccelerationStructureBuildRead,
		AccelerationStructureBuildWrite,
		Present
	};

	class VansRenderGraphIntern
	{
	public:
		static uint64_t InternName(const char* name)
		{
			if (name == nullptr || name[0] == '\0')
			{
				return 0;
			}

			uint64_t hash = 14695981039346656037ull;
			while (*name != '\0')
			{
				hash ^= static_cast<uint8_t>(*name);
				hash *= 1099511628211ull;
				++name;
			}
			return hash != 0 ? hash : 1;
		}

		static uint64_t InternName(const std::string& name)
		{
			return InternName(name.c_str());
		}
	};

	struct VansRenderResourceAccess
	{
		VansRenderResourceAccess() = default;
		VansRenderResourceAccess(const char* resourceName, VansRenderResourceUsage resourceUsage)
			: name(resourceName != nullptr ? resourceName : "")
			, resourceId(VansRenderGraphIntern::InternName(resourceName))
			, usage(resourceUsage)
		{
		}
		VansRenderResourceAccess(std::string resourceName, VansRenderResourceUsage resourceUsage)
			: name(std::move(resourceName))
			, resourceId(VansRenderGraphIntern::InternName(name))
			, usage(resourceUsage)
		{
		}

		std::string name;
		uint64_t resourceId = 0;
		VansRenderResourceUsage usage = VansRenderResourceUsage::SampledRead;
	};

	struct VansRenderPassNodeDesc
	{
		std::string name;
		uint64_t passId = 0;
		VansRenderQueueClass queue = VansRenderQueueClass::Graphics;
		bool resizeDependent = false;
		bool allowAsyncCompute = false;
		bool enabled = true;

		std::vector<VansRenderResourceAccess> reads;
		std::vector<VansRenderResourceAccess> writes;
		std::vector<std::string> preservedFeatures;
	};

	struct VansCompiledRenderResourceState
	{
		std::string name;
		uint64_t resourceId = 0;
		VansRenderResourceUsage firstUsage = VansRenderResourceUsage::SampledRead;
		VansRenderResourceUsage lastUsage = VansRenderResourceUsage::SampledRead;
		uint32_t firstPassIndex = 0;
		uint32_t lastPassIndex = 0;
		bool readBeforeGraphWrite = false;
	};

	struct VansCompiledRenderPassNode
	{
		uint32_t index = 0;
		const VansRenderPassNodeDesc* desc = nullptr;
		std::vector<std::string> externalInputs;
		std::vector<std::string> resourceTransitions;
	};

	struct VansCompiledRenderGraph
	{
		uint64_t frameNumber = 0;
		std::vector<VansCompiledRenderPassNode> passes;
		std::vector<VansCompiledRenderResourceState> resources;
		std::vector<std::string> warnings;
		std::vector<std::string> errors;

		void Clear()
		{
			frameNumber = 0;
			passes.clear();
			resources.clear();
			warnings.clear();
			errors.clear();
		}

		bool IsValid() const { return errors.empty(); }
	};

	enum class VansRenderDependencyType
	{
		ReadAfterWrite,
		WriteAfterRead,
		WriteAfterWrite,
		LayoutOrAccessTransition,
		QueueTransition
	};

	struct VansRenderGraphDependency
	{
		std::string resourceName;
		uint64_t resourceId = 0;
		uint32_t srcPassIndex = 0;
		uint32_t dstPassIndex = 0;
		std::string srcPassName;
		std::string dstPassName;
		uint64_t srcPassId = 0;
		uint64_t dstPassId = 0;
		VansRenderResourceUsage srcUsage = VansRenderResourceUsage::SampledRead;
		VansRenderResourceUsage dstUsage = VansRenderResourceUsage::SampledRead;
		VansRenderQueueClass srcQueue = VansRenderQueueClass::Graphics;
		VansRenderQueueClass dstQueue = VansRenderQueueClass::Graphics;
		VansRenderDependencyType type = VansRenderDependencyType::LayoutOrAccessTransition;
	};

	struct VansRenderPassBarrierPlan
	{
		uint32_t passIndex = 0;
		std::string passName;
		uint64_t passId = 0;
		std::vector<VansRenderGraphDependency> incomingDependencies;
	};

	struct VansRenderGraphBarrierPlan
	{
		uint64_t frameNumber = 0;
		std::vector<VansRenderPassBarrierPlan> passPlans;
		std::vector<VansRenderGraphDependency> dependencies;
		std::vector<std::string> externalInputs;
		std::vector<std::string> warnings;

		void Clear()
		{
			frameNumber = 0;
			passPlans.clear();
			dependencies.clear();
			externalInputs.clear();
			warnings.clear();
		}
	};

	struct VansRenderFeatureAuditResult
	{
		std::vector<std::string> presentFeatures;
		std::vector<std::string> missingFeatures;
		std::vector<std::string> conditionallyDisabledFeatures;

		void Clear()
		{
			presentFeatures.clear();
			missingFeatures.clear();
			conditionallyDisabledFeatures.clear();
		}

		bool Passed() const { return missingFeatures.empty(); }
	};

	struct VansRenderGraphDiagnosticsSnapshot
	{
		bool available = false;
		bool compiledGraphValid = false;
		bool featureAuditPassed = false;
		uint32_t framePlanPassCount = 0;
		uint32_t compiledResourceCount = 0;
		uint32_t barrierDependencyCount = 0;
		uint64_t topologyRevision = 0;
		uint64_t topologyHash = 0;
		uint64_t compiledFrameNumber = 0;
	};

	class VansRenderFramePlan
	{
	public:
		void Clear()
		{
			m_Passes.clear();
			m_FrameNumber = 0;
		}

		void Begin(uint64_t frameNumber)
		{
			m_Passes.clear();
			m_FrameNumber = frameNumber;
		}

		void AddPass(VansRenderPassNodeDesc desc)
		{
			if (desc.enabled)
			{
				m_Passes.emplace_back(std::move(desc));
			}
		}

		uint64_t GetFrameNumber() const { return m_FrameNumber; }
		const std::vector<VansRenderPassNodeDesc>& GetPasses() const { return m_Passes; }
		size_t GetPassCount() const { return m_Passes.size(); }

		const VansRenderPassNodeDesc* FindPass(const std::string& name) const
		{
			for (const auto& pass : m_Passes)
			{
				if (pass.name == name)
				{
					return &pass;
				}
			}
			return nullptr;
		}

		bool ContainsPreservedFeature(const std::string& feature) const
		{
			for (const auto& pass : m_Passes)
			{
				for (const auto& preservedFeature : pass.preservedFeatures)
				{
					if (preservedFeature == feature)
					{
						return true;
					}
				}
			}
			return false;
		}

	private:
		uint64_t m_FrameNumber = 0;
		std::vector<VansRenderPassNodeDesc> m_Passes;
	};

	class VansRenderGraphCompiler
	{
	public:
		static bool IsWriteUsage(VansRenderResourceUsage usage)
		{
			switch (usage)
			{
			case VansRenderResourceUsage::ColorAttachmentWrite:
			case VansRenderResourceUsage::DepthStencilAttachmentWrite:
			case VansRenderResourceUsage::StorageWrite:
			case VansRenderResourceUsage::TransferDst:
			case VansRenderResourceUsage::AccelerationStructureBuildWrite:
				return true;
			default:
				return false;
			}
		}

		static VansCompiledRenderGraph CompileFramePlan(const VansRenderFramePlan& framePlan)
		{
			VansCompiledRenderGraph graph{};
			graph.frameNumber = framePlan.GetFrameNumber();

			std::set<std::string> passNames;
			std::map<uint64_t, VansCompiledRenderResourceState> resourceStates;

			const auto& passes = framePlan.GetPasses();
			graph.passes.reserve(passes.size());

			for (uint32_t passIndex = 0; passIndex < static_cast<uint32_t>(passes.size()); ++passIndex)
			{
				const auto& pass = passes[passIndex];
				VansCompiledRenderPassNode compiledPass{};
				compiledPass.index = passIndex;
				compiledPass.desc = &pass;

				if (pass.name.empty())
				{
					graph.errors.emplace_back("RenderGraph contains an unnamed pass.");
				}
				else if (!passNames.insert(pass.name).second)
				{
					graph.errors.emplace_back("RenderGraph contains duplicate pass: " + pass.name);
				}

				for (const auto& read : pass.reads)
				{
					RecordResourceUsage(
						resourceStates,
						compiledPass,
						read,
						passIndex,
						false);
				}

				for (const auto& write : pass.writes)
				{
					RecordResourceUsage(
						resourceStates,
						compiledPass,
						write,
						passIndex,
						true);
				}

				graph.passes.emplace_back(std::move(compiledPass));
			}

			graph.resources.reserve(resourceStates.size());
			for (auto& resourceState : resourceStates)
			{
				graph.resources.emplace_back(std::move(resourceState.second));
			}

			return graph;
		}

	private:
		static void RecordResourceUsage(
			std::map<uint64_t, VansCompiledRenderResourceState>& resourceStates,
			VansCompiledRenderPassNode& compiledPass,
			const VansRenderResourceAccess& access,
			uint32_t passIndex,
			bool isWrite)
		{
			if (access.name.empty())
			{
				return;
			}

			const uint64_t resourceId =
				access.resourceId != 0 ? access.resourceId : VansRenderGraphIntern::InternName(access.name);
			auto iter = resourceStates.find(resourceId);
			if (iter == resourceStates.end())
			{
				VansCompiledRenderResourceState state{};
				state.name = access.name;
				state.resourceId = resourceId;
				state.firstUsage = access.usage;
				state.lastUsage = access.usage;
				state.firstPassIndex = passIndex;
				state.lastPassIndex = passIndex;
				state.readBeforeGraphWrite = !isWrite;

				if (!isWrite)
				{
					compiledPass.externalInputs.emplace_back(access.name);
				}

				resourceStates.emplace(resourceId, std::move(state));
				return;
			}

			auto& state = iter->second;
			if (state.lastUsage != access.usage)
			{
				compiledPass.resourceTransitions.emplace_back(access.name);
			}

			state.lastUsage = access.usage;
			state.lastPassIndex = passIndex;
		}
	};

	class VansRenderGraphBarrierPlanner
	{
	private:
		struct VansLastResourceAccess
		{
			uint32_t passIndex = 0;
			std::string passName;
			uint64_t passId = 0;
			VansRenderQueueClass queue = VansRenderQueueClass::Graphics;
			VansRenderResourceUsage usage = VansRenderResourceUsage::SampledRead;
			bool write = false;
		};

	public:
		static VansRenderGraphBarrierPlan BuildBarrierPlan(const VansCompiledRenderGraph& graph)
		{
			VansRenderGraphBarrierPlan barrierPlan{};
			barrierPlan.frameNumber = graph.frameNumber;
			barrierPlan.passPlans.reserve(graph.passes.size());

			std::map<uint64_t, VansLastResourceAccess> lastAccesses;
			std::set<std::string> externalInputs;

			for (const auto& pass : graph.passes)
			{
				VansRenderPassBarrierPlan passPlan{};
				passPlan.passIndex = pass.index;
				passPlan.passName = pass.desc ? pass.desc->name : std::string{};
				passPlan.passId = pass.desc ? pass.desc->passId : 0;

				if (!pass.desc)
				{
					barrierPlan.warnings.emplace_back("Barrier planner skipped a compiled pass without a pass description.");
					barrierPlan.passPlans.emplace_back(std::move(passPlan));
					continue;
				}

				RecordAccesses(
					*pass.desc,
					pass.index,
					false,
					passPlan,
					barrierPlan,
					externalInputs,
					lastAccesses);

				RecordAccesses(
					*pass.desc,
					pass.index,
					true,
					passPlan,
					barrierPlan,
					externalInputs,
					lastAccesses);

				barrierPlan.passPlans.emplace_back(std::move(passPlan));
			}

			barrierPlan.externalInputs.assign(externalInputs.begin(), externalInputs.end());
			return barrierPlan;
		}

	private:
		static void RecordAccesses(
			const VansRenderPassNodeDesc& pass,
			uint32_t passIndex,
			bool writes,
			VansRenderPassBarrierPlan& passPlan,
			VansRenderGraphBarrierPlan& barrierPlan,
			std::set<std::string>& externalInputs,
			std::map<uint64_t, VansLastResourceAccess>& lastAccesses)
		{
			const auto& accesses = writes ? pass.writes : pass.reads;
			for (const auto& access : accesses)
			{
				if (access.name.empty())
				{
					continue;
				}

				const bool currentWrite = VansRenderGraphCompiler::IsWriteUsage(access.usage);
				const uint64_t resourceId =
					access.resourceId != 0 ? access.resourceId : VansRenderGraphIntern::InternName(access.name);
				auto iter = lastAccesses.find(resourceId);
				if (iter == lastAccesses.end())
				{
					if (!currentWrite)
					{
						externalInputs.insert(access.name);
					}

					lastAccesses.emplace(
						resourceId,
						VansLastResourceAccess{ passIndex, pass.name, pass.passId, pass.queue, access.usage, currentWrite });
					continue;
				}

				const auto previous = iter->second;
				if (NeedsDependency(previous, pass, access.usage, currentWrite))
				{
					VansRenderGraphDependency dependency{};
					dependency.resourceName = access.name;
					dependency.resourceId = resourceId;
					dependency.srcPassIndex = previous.passIndex;
					dependency.dstPassIndex = passIndex;
					dependency.srcPassName = previous.passName;
					dependency.dstPassName = pass.name;
					dependency.srcPassId = previous.passId;
					dependency.dstPassId = pass.passId;
					dependency.srcUsage = previous.usage;
					dependency.dstUsage = access.usage;
					dependency.srcQueue = previous.queue;
					dependency.dstQueue = pass.queue;
					dependency.type = ClassifyDependency(previous, pass.queue, access.usage, currentWrite);

					passPlan.incomingDependencies.emplace_back(dependency);
					barrierPlan.dependencies.emplace_back(std::move(dependency));
				}

				iter->second = VansLastResourceAccess{ passIndex, pass.name, pass.passId, pass.queue, access.usage, currentWrite };
			}
		}

		static bool NeedsDependency(
			const VansLastResourceAccess& previous,
			const VansRenderPassNodeDesc& currentPass,
			VansRenderResourceUsage currentUsage,
			bool currentWrite)
		{
			return previous.write
				|| currentWrite
				|| previous.usage != currentUsage
				|| previous.queue != currentPass.queue;
		}

		static VansRenderDependencyType ClassifyDependency(
			const VansLastResourceAccess& previous,
			VansRenderQueueClass currentQueue,
			VansRenderResourceUsage currentUsage,
			bool currentWrite)
		{
			if (previous.queue != currentQueue)
			{
				return VansRenderDependencyType::QueueTransition;
			}

			if (previous.write && !currentWrite)
			{
				return VansRenderDependencyType::ReadAfterWrite;
			}

			if (!previous.write && currentWrite)
			{
				return VansRenderDependencyType::WriteAfterRead;
			}

			if (previous.write && currentWrite)
			{
				return VansRenderDependencyType::WriteAfterWrite;
			}

			if (previous.usage != currentUsage)
			{
				return VansRenderDependencyType::LayoutOrAccessTransition;
			}

			return VansRenderDependencyType::LayoutOrAccessTransition;
		}
	};

	class VansRenderFeatureAuditor
	{
	public:
		static VansRenderFeatureAuditResult AuditFramePlan(
			const VansRenderFramePlan& framePlan,
			const std::vector<std::string>& requiredFeatures,
			const std::vector<std::string>& conditionallyDisabledFeatures)
		{
			VansRenderFeatureAuditResult result{};
			result.conditionallyDisabledFeatures = conditionallyDisabledFeatures;

			std::set<std::string> presentFeatures;
			for (const auto& pass : framePlan.GetPasses())
			{
				for (const auto& feature : pass.preservedFeatures)
				{
					if (!feature.empty())
					{
						presentFeatures.insert(feature);
					}
				}
			}

			result.presentFeatures.assign(presentFeatures.begin(), presentFeatures.end());

			for (const auto& requiredFeature : requiredFeatures)
			{
				if (!framePlan.ContainsPreservedFeature(requiredFeature))
				{
					result.missingFeatures.emplace_back(requiredFeature);
				}
			}

			return result;
		}
	};

	class VansRenderGraphDebugDumper
	{
	public:
		static const char* ToString(VansRenderQueueClass queue)
		{
			switch (queue)
			{
			case VansRenderQueueClass::Graphics: return "Graphics";
			case VansRenderQueueClass::Compute: return "Compute";
			case VansRenderQueueClass::AsyncCompute: return "AsyncCompute";
			case VansRenderQueueClass::Present: return "Present";
			default: return "UnknownQueue";
			}
		}

		static const char* ToString(VansRenderResourceUsage usage)
		{
			switch (usage)
			{
			case VansRenderResourceUsage::ColorAttachmentWrite: return "ColorAttachmentWrite";
			case VansRenderResourceUsage::DepthStencilAttachmentRead: return "DepthStencilAttachmentRead";
			case VansRenderResourceUsage::DepthStencilAttachmentWrite: return "DepthStencilAttachmentWrite";
			case VansRenderResourceUsage::SampledRead: return "SampledRead";
			case VansRenderResourceUsage::StorageRead: return "StorageRead";
			case VansRenderResourceUsage::StorageWrite: return "StorageWrite";
			case VansRenderResourceUsage::TransferSrc: return "TransferSrc";
			case VansRenderResourceUsage::TransferDst: return "TransferDst";
			case VansRenderResourceUsage::IndirectArgumentRead: return "IndirectArgumentRead";
			case VansRenderResourceUsage::AccelerationStructureBuildRead: return "AccelerationStructureBuildRead";
			case VansRenderResourceUsage::AccelerationStructureBuildWrite: return "AccelerationStructureBuildWrite";
			case VansRenderResourceUsage::Present: return "Present";
			default: return "UnknownUsage";
			}
		}

		static const char* ToString(VansRenderDependencyType type)
		{
			switch (type)
			{
			case VansRenderDependencyType::ReadAfterWrite: return "ReadAfterWrite";
			case VansRenderDependencyType::WriteAfterRead: return "WriteAfterRead";
			case VansRenderDependencyType::WriteAfterWrite: return "WriteAfterWrite";
			case VansRenderDependencyType::LayoutOrAccessTransition: return "LayoutOrAccessTransition";
			case VansRenderDependencyType::QueueTransition: return "QueueTransition";
			default: return "UnknownDependency";
			}
		}

		static std::string BuildFramePlanSummary(const VansRenderFramePlan& framePlan)
		{
			std::ostringstream stream;
			stream << "RenderFramePlan frame=" << framePlan.GetFrameNumber()
				<< " passes=" << framePlan.GetPassCount() << "\n";

			uint32_t passIndex = 0;
			for (const auto& pass : framePlan.GetPasses())
			{
				stream << "  [" << passIndex++ << "] " << pass.name
					<< " queue=" << ToString(pass.queue)
					<< " reads=" << pass.reads.size()
					<< " writes=" << pass.writes.size()
					<< " features=" << pass.preservedFeatures.size() << "\n";
				AppendAccessList(stream, "    R", pass.reads);
				AppendAccessList(stream, "    W", pass.writes);
			}

			return stream.str();
		}

		static std::string BuildCompiledGraphSummary(const VansCompiledRenderGraph& graph)
		{
			std::ostringstream stream;
			stream << "CompiledRenderGraph frame=" << graph.frameNumber
				<< " passes=" << graph.passes.size()
				<< " resources=" << graph.resources.size()
				<< " warnings=" << graph.warnings.size()
				<< " errors=" << graph.errors.size() << "\n";

			for (const auto& resource : graph.resources)
			{
				stream << "  resource " << resource.name
					<< " first=" << ToString(resource.firstUsage) << "@" << resource.firstPassIndex
					<< " last=" << ToString(resource.lastUsage) << "@" << resource.lastPassIndex
					<< " externalRead=" << (resource.readBeforeGraphWrite ? "true" : "false")
					<< "\n";
			}

			AppendStringList(stream, "  warning", graph.warnings);
			AppendStringList(stream, "  error", graph.errors);
			return stream.str();
		}

		static std::string BuildBarrierPlanSummary(const VansRenderGraphBarrierPlan& barrierPlan)
		{
			std::ostringstream stream;
			stream << "RenderGraphBarrierPlan frame=" << barrierPlan.frameNumber
				<< " passPlans=" << barrierPlan.passPlans.size()
				<< " dependencies=" << barrierPlan.dependencies.size()
				<< " externalInputs=" << barrierPlan.externalInputs.size()
				<< " warnings=" << barrierPlan.warnings.size() << "\n";

			for (const auto& dependency : barrierPlan.dependencies)
			{
				stream << "  dep " << dependency.resourceName
					<< " " << dependency.srcPassName << "[" << dependency.srcPassIndex << "]"
					<< "(" << ToString(dependency.srcQueue) << ":" << ToString(dependency.srcUsage) << ") -> "
					<< dependency.dstPassName << "[" << dependency.dstPassIndex << "]"
					<< "(" << ToString(dependency.dstQueue) << ":" << ToString(dependency.dstUsage) << ")"
					<< " type=" << ToString(dependency.type) << "\n";
			}

			AppendStringList(stream, "  external", barrierPlan.externalInputs);
			AppendStringList(stream, "  warning", barrierPlan.warnings);
			return stream.str();
		}

		static std::string BuildFeatureAuditSummary(const VansRenderFeatureAuditResult& audit)
		{
			std::ostringstream stream;
			stream << "RenderFeatureAudit present=" << audit.presentFeatures.size()
				<< " missing=" << audit.missingFeatures.size()
				<< " conditionallyDisabled=" << audit.conditionallyDisabledFeatures.size()
				<< " passed=" << (audit.Passed() ? "true" : "false") << "\n";
			AppendStringList(stream, "  present", audit.presentFeatures);
			AppendStringList(stream, "  missing", audit.missingFeatures);
			AppendStringList(stream, "  disabled", audit.conditionallyDisabledFeatures);
			return stream.str();
		}

	private:
		static void AppendAccessList(
			std::ostringstream& stream,
			const char* prefix,
			const std::vector<VansRenderResourceAccess>& accesses)
		{
			for (const auto& access : accesses)
			{
				stream << prefix << " " << access.name << ":" << ToString(access.usage) << "\n";
			}
		}

		static void AppendStringList(
			std::ostringstream& stream,
			const char* prefix,
			const std::vector<std::string>& values)
		{
			for (const auto& value : values)
			{
				stream << prefix << " " << value << "\n";
			}
		}
	};

	class VansDeferredDeleteQueue
	{
	public:
		void Enqueue(std::function<void()> destroy)
		{
			if (destroy)
			{
				m_Deletes.emplace_back(std::move(destroy));
			}
		}

		void Flush()
		{
			for (auto& destroy : m_Deletes)
			{
				destroy();
			}
			m_Deletes.clear();
		}

		bool Empty() const { return m_Deletes.empty(); }
		size_t Size() const { return m_Deletes.size(); }

	private:
		std::vector<std::function<void()>> m_Deletes;
	};

	struct VansFrameContext
	{
		uint64_t frameNumber = 0;
		uint32_t swapchainImageIndex = 0;

		VansVKCommandBuffer* graphicsCmd = nullptr;
		VansVKCommandBuffer* graphicsPreCmd = nullptr;
		VansVKCommandBuffer* graphicsScreenCmd = nullptr;
		VansVKCommandBuffer* asyncSSAOCmd = nullptr;
		VansVKCommandBuffer* shadowCmd = nullptr;
		VansVKCommandBuffer* gbufferCmd = nullptr;
		VansVKCommandBuffer* asyncEarlyCmd = nullptr;
		VansVKCommandBuffer* asyncCloudCmd = nullptr;
		VansVKCommandBuffer* asyncGICmd = nullptr;

		VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
		VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

		VkFence graphicsFence = VK_NULL_HANDLE;
		VkFence graphicsPreFence = VK_NULL_HANDLE;
		VkFence graphicsScreenFence = VK_NULL_HANDLE;
		VkFence asyncSSAOFence = VK_NULL_HANDLE;
		VkFence shadowFence = VK_NULL_HANDLE;
		VkFence gbufferFence = VK_NULL_HANDLE;
		VkFence asyncEarlyFence = VK_NULL_HANDLE;
		VkFence asyncCloudFence = VK_NULL_HANDLE;
		VkFence asyncGIFence = VK_NULL_HANDLE;

		bool frameSubmitSucceeded = true;
		bool graphicsPreRecorded = false;
		bool graphicsScreenRecorded = false;
		bool asyncSSAORecorded = false;
		bool shadowRecorded = false;
		bool gbufferRecorded = false;
		bool asyncEarlyRecorded = false;
		bool asyncCloudRecorded = false;
		bool asyncGIRecorded = false;
		bool graphicsPreSubmitted = false;
		bool graphicsScreenSubmitted = false;
		bool asyncSSAOSubmitted = false;
		bool shadowSubmitted = false;
		bool gbufferSubmitted = false;
		bool asyncEarlySubmitted = false;
		bool asyncCloudSubmitted = false;
		bool asyncGISubmitted = false;
		uint64_t lastDeferredDeleteFlushCount = 0;
		uint64_t pendingDeferredDeleteCount = 0;

		VansDeferredDeleteQueue deferredDeletes;
	};
}
