#pragma once

#include "VansUIRuntimeHandles.h"
#include "VansUIVariant.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VansRuntime
{
	struct VansUIAction
	{
		std::string name;
		VansUIVariantMap params;
		VansUIHandleId sourceScreen = kInvalidUIHandle;
		std::string sourceElement;
	};

	class VansUIActionBus
	{
	public:
		using Callback = std::function<void(const VansUIAction&)>;

		static VansUIActionBus& Get();

		VansUISubscriptionToken Subscribe(const std::string& actionName, Callback callback);
		void Unsubscribe(VansUISubscriptionToken token);
		void Dispatch(const VansUIAction& action);
		void Clear();

	private:
		struct Subscriber
		{
			VansUISubscriptionToken token = kInvalidUISubscription;
			std::string actionName;
			Callback callback;
		};

		VansUISubscriptionToken m_NextToken = 1;
		std::vector<Subscriber> m_Subscribers;
	};
}
