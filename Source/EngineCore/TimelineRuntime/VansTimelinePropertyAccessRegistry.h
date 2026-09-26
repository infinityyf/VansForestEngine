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
	VansResolvedTimelineTarget target;
	VansRuntimeWorld* world = nullptr;
	VansTimelineResourceId resource;
};

enum class VansTimelinePropertyWriteDomain : std::uint8_t
{
	Property,
	Transform
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
	VansTimelinePropertyWriteDomain writeDomain = VansTimelinePropertyWriteDomain::Property;
	VansTimelinePropertyReadFn read = nullptr;
	VansTimelinePropertyWriteFn write = nullptr;
};

class VansTimelinePropertyAccessRegistry
{
public:
	bool Register(VansTimelinePropertyAccessDescriptor descriptor, std::string& error);
	bool Seal(bool allowEmpty, std::string& error);
	bool IsSealed() const { return m_Sealed; }
	bool Empty() const { return m_Descriptors.empty(); }
	const VansTimelinePropertyAccessDescriptor* Resolve(
		VansStableId<VansTimelinePropertyAccessTag> id) const;
	const VansTimelinePropertyAccessDescriptor* Resolve(std::string_view stableName) const;
	std::uint64_t ManifestHash() const;
	const std::vector<VansTimelinePropertyAccessDescriptor>& Descriptors() const { return m_Descriptors; }

private:
	bool m_Sealed = false;
	std::vector<VansTimelinePropertyAccessDescriptor> m_Descriptors;
	std::unordered_map<VansStableId<VansTimelinePropertyAccessTag>, std::size_t> m_ById;
};
}
