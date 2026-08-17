#include "VansTimelinePreAnimatedState.h"

#include <algorithm>

namespace Vans
{
bool VansTimelinePreAnimatedState::Store(VansTimelineRestoreToken token)
{
	if (!token.handle.IsValid() || !token.writer.IsValid() ||
		token.applier == VansInvalidTimelineApplierSlot) return false;
	const auto existing = std::find_if(m_Tokens.begin(), m_Tokens.end(), [&](const auto& current)
	{
		return current.token.handle == token.handle && current.token.applier == token.applier;
	});
	if (existing == m_Tokens.end())
	{
		m_Tokens.push_back({ token });
		if (token.resource) m_ResourceStacks[token.resource].push_back({ token.handle, token.applier });
	}
	return true;
}

bool VansTimelinePreAnimatedState::ReleaseWriter(
	VansTimelineWriterHandle writer,
	bool restore)
{
	std::vector<TokenKey> released;
	for (const StoredToken& stored : m_Tokens)
		if (stored.token.writer == writer) released.push_back({ stored.token.handle, stored.token.applier });
	bool deferred = false;
	for (TokenKey key : released)
	{
		auto found = std::find_if(m_Tokens.begin(), m_Tokens.end(), [&](const auto& stored)
		{ return stored.token.handle == key.handle && stored.token.applier == key.applier; });
		if (found == m_Tokens.end()) continue;
		if (!found->token.resource)
		{
			if (restore && m_Appliers)
				if (IVansTimelineOutputApplier* applier = m_Appliers->At(found->token.applier)) applier->Restore(found->token);
			if (!restore && m_Appliers)
				if (IVansTimelineOutputApplier* applier = m_Appliers->At(found->token.applier)) applier->ReleaseWriter(found->token.writer);
			m_Tokens.erase(found);
			continue;
		}
		found->pending = true;
		found->restore = restore;
		if (restore && m_Appliers)
			if (IVansTimelineOutputApplier* applier = m_Appliers->At(found->token.applier))
				applier->DeactivateWriter(found->token.writer);
		auto stack = m_ResourceStacks.find(found->token.resource);
		if (stack == m_ResourceStacks.end()) continue;
		bool suppressLowerRestores = false;
		while (!stack->second.empty())
		{
			const TokenKey top = stack->second.back();
			auto token = std::find_if(m_Tokens.begin(), m_Tokens.end(), [&](const auto& current)
			{ return current.token.handle == top.handle && current.token.applier == top.applier; });
			if (token == m_Tokens.end()) { stack->second.pop_back(); continue; }
			if (!token->pending) { deferred = true; break; }
			if (token->restore && !suppressLowerRestores && m_Appliers)
				if (IVansTimelineOutputApplier* applier = m_Appliers->At(token->token.applier)) applier->Restore(token->token);
			if ((!token->restore || suppressLowerRestores) && m_Appliers)
				if (IVansTimelineOutputApplier* applier = m_Appliers->At(token->token.applier)) applier->ReleaseWriter(token->token.writer);
			if (!token->restore) suppressLowerRestores = true;
			m_Tokens.erase(token);
			stack->second.pop_back();
		}
		if (stack->second.empty()) m_ResourceStacks.erase(stack);
	}
	return !deferred;
}

void VansTimelinePreAnimatedState::RestoreAll()
{
	if (m_Appliers)
		for (auto iterator = m_Tokens.rbegin(); iterator != m_Tokens.rend(); ++iterator)
			if (IVansTimelineOutputApplier* applier = m_Appliers->At(iterator->token.applier)) applier->Restore(iterator->token);
	m_Tokens.clear();
	m_ResourceStacks.clear();
}
}
