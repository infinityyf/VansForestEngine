#include "Public/VansUIActionBus.h"

#include <algorithm>

namespace VansRuntime
{
	VansUIActionBus& VansUIActionBus::Get()
	{
		static VansUIActionBus bus;
		return bus;
	}

	VansUISubscriptionToken VansUIActionBus::Subscribe(const std::string& actionName, Callback callback)
	{
		if (actionName.empty() || !callback)
			return kInvalidUISubscription;

		const VansUISubscriptionToken token = m_NextToken++;
		m_Subscribers.push_back(Subscriber{ token, actionName, std::move(callback) });
		return token;
	}

	void VansUIActionBus::Unsubscribe(VansUISubscriptionToken token)
	{
		if (token == kInvalidUISubscription)
			return;

		m_Subscribers.erase(
			std::remove_if(m_Subscribers.begin(), m_Subscribers.end(),
				[token](const Subscriber& subscriber)
				{
					return subscriber.token == token;
				}),
			m_Subscribers.end());
	}

	void VansUIActionBus::Dispatch(const VansUIAction& action)
	{
		std::vector<Callback> callbacks;
		for (const Subscriber& subscriber : m_Subscribers)
			if (subscriber.actionName == action.name && subscriber.callback)
				callbacks.push_back(subscriber.callback);

		for (const Callback& callback : callbacks)
			callback(action);
	}

	void VansUIActionBus::Clear()
	{
		m_Subscribers.clear();
		m_NextToken = 1;
	}
}
