#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
using VansTimelineRestoreCallback = std::function<void()>;

class VansTimelinePreAnimatedState
{
public:
	~VansTimelinePreAnimatedState();

	void Capture(
		const std::string& writerId,
		const std::string& propertyIdentity,
		VansTimelineRestoreCallback restore);
	void ReleaseWriter(const std::string& writerId);
	void RestoreAll();
	std::size_t PropertyCount() const { return m_Stacks.size(); }

private:
	struct Token
	{
		std::string writerId;
		VansTimelineRestoreCallback restore;
		bool active = true;
	};

	void RestoreInactiveTop(std::vector<Token>& stack);
	std::unordered_map<std::string, std::vector<Token>> m_Stacks;
};
}
