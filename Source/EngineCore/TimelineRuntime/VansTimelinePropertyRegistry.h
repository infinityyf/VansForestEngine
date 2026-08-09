#pragma once

#include "VansTimelineEvaluation.h"
#include "VansTimelinePreAnimatedState.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace Vans
{
struct VansTimelineRuntimePropertyDescriptor
{
	std::string descriptorId;
	std::uint16_t componentTypeId = 0;
	VansTimelineChannelType valueType = VansTimelineChannelType::Float;
	std::function<bool(const VansResolvedTimelineTarget&, VansTimelineKeyValue&, std::string&)> read;
	std::function<bool(const VansResolvedTimelineTarget&, const VansTimelineKeyValue&, std::string&)> write;
};

class VansTimelinePropertyRegistry
{
public:
	bool Register(VansTimelineRuntimePropertyDescriptor descriptor, std::string& error);
	const VansTimelineRuntimePropertyDescriptor* Find(const std::string& descriptorId) const;
	bool Apply(
		VansTimelineBlendMode blendMode,
		const VansResolvedTimelineTarget& target,
		const VansTimelinePropertyOutput& output,
		VansTimelineRestoreCallback& restore,
		std::string& error) const;
	std::size_t Size() const { return m_Descriptors.size(); }

private:
	std::unordered_map<std::string, VansTimelineRuntimePropertyDescriptor> m_Descriptors;
};
}
