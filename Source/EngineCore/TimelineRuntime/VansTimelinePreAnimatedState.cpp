#include "VansTimelinePreAnimatedState.h"

#include <algorithm>

namespace Vans
{
VansTimelinePreAnimatedState::~VansTimelinePreAnimatedState()
{
	RestoreAll();
}

void VansTimelinePreAnimatedState::Capture(
	const std::string& writerId,
	const std::string& propertyIdentity,
	VansTimelineRestoreCallback restore)
{
	if (writerId.empty() || propertyIdentity.empty() || !restore) return;
	auto& stack = m_Stacks[propertyIdentity];
	const auto existing = std::find_if(stack.begin(), stack.end(), [&](const Token& token)
	{
		return token.writerId == writerId;
	});
	if (existing == stack.end())
		stack.push_back({ writerId, std::move(restore), true });
}

void VansTimelinePreAnimatedState::RestoreInactiveTop(std::vector<Token>& stack)
{
	while (!stack.empty() && !stack.back().active)
	{
		VansTimelineRestoreCallback restore = std::move(stack.back().restore);
		stack.pop_back();
		if (restore) restore();
	}
}

void VansTimelinePreAnimatedState::ReleaseWriter(const std::string& writerId)
{
	for (auto iterator = m_Stacks.begin(); iterator != m_Stacks.end();)
	{
		auto& stack = iterator->second;
		for (Token& token : stack)
			if (token.writerId == writerId) token.active = false;
		RestoreInactiveTop(stack);
		if (stack.empty()) iterator = m_Stacks.erase(iterator);
		else ++iterator;
	}
}

void VansTimelinePreAnimatedState::RestoreAll()
{
	for (auto& [identity, stack] : m_Stacks)
	{
		(void)identity;
		while (!stack.empty())
		{
			VansTimelineRestoreCallback restore = std::move(stack.back().restore);
			stack.pop_back();
			if (restore) restore();
		}
	}
	m_Stacks.clear();
}
}
