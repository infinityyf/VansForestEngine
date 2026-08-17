#pragma once

#include "VansTimelineApplierRegistry.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
class VansRuntimeWorld;
struct VansTimelinePropertyAccessContext
{
	const VansResolvedTimelineTarget& target;
	VansRuntimeWorld* world = nullptr;
	VansTimelineResourceId resource;
};

using VansTimelinePropertyReadFn = bool(*)(
	const VansTimelinePropertyAccessContext&,
	VansTimelineValue&,
	std::string&);
using VansTimelinePropertyWriteFn = bool(*)(
	const VansTimelinePropertyAccessContext&,
	const VansTimelineValue&,
	std::string&);

struct VansTimelinePropertyAccessDescriptor
{
	VansStableId<struct VansTimelinePropertyAccessTag> id;
	std::string stableName;
	std::uint16_t componentTypeId = 0;
	VansTimelineValueType valueType = VansTimelineValueType::Null;
	VansTimelinePropertyReadFn read = nullptr;
	VansTimelinePropertyWriteFn write = nullptr;
};

class VansTimelinePropertyAccessRegistry
{
public:
	bool Register(VansTimelinePropertyAccessDescriptor descriptor, std::string& error);
	bool Seal(std::string& error);
	const VansTimelinePropertyAccessDescriptor* Resolve(
		VansStableId<VansTimelinePropertyAccessTag> id) const;
	const VansTimelinePropertyAccessDescriptor* Resolve(std::string_view stableName) const;
	std::uint64_t ManifestHash() const;
	const std::vector<VansTimelinePropertyAccessDescriptor>& Descriptors() const { return m_Descriptors; }
	static const VansTimelinePropertyAccessRegistry& BuiltIns();

private:
	bool m_Sealed = false;
	std::vector<VansTimelinePropertyAccessDescriptor> m_Descriptors;
	std::unordered_map<VansStableId<VansTimelinePropertyAccessTag>, std::size_t> m_ById;
};

bool VansRegisterSceneTimelinePropertyAccessors(
	VansTimelinePropertyAccessRegistry& registry,
	std::string& error);
bool VansRegisterAudioTimelinePropertyAccessors(
	VansTimelinePropertyAccessRegistry& registry,
	std::string& error);
}

namespace VansGraphics
{
bool VansRegisterRenderTimelinePropertyAccessors(
	Vans::VansTimelinePropertyAccessRegistry& registry,
	std::string& error);
}
