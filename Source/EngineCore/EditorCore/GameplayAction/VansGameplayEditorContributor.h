#pragma once

#include "../../GameplayActionCore/VansGameplayModuleContributor.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansGAFEditorDescriptor
{
	std::string typeId;
	std::string displayName;
	std::string category;
	VansGAFExtensionKind kind = VansGAFExtensionKind::Operation;
};

class VansGAFEditorRegistry
{
public:
	bool Register(VansGAFEditorDescriptor descriptor, std::string& error);
	bool Seal(std::string& error);
	const VansGAFEditorDescriptor* Resolve(std::string_view typeId) const;
	std::vector<VansGAFEditorDescriptor> Descriptors() const;
	bool IsSealed() const { return m_Sealed; }

private:
	std::unordered_map<std::string, VansGAFEditorDescriptor> m_Descriptors;
	bool m_Sealed = false;
};

class IVansGameplayEditorContributor
{
public:
	virtual ~IVansGameplayEditorContributor() = default;
	virtual const VansGAFModuleDescriptor& Descriptor() const = 0;
	virtual bool RegisterEditor(VansGAFEditorRegistry& registry, std::string& error) const = 0;
};

using VansGameplayEditorContribution =
	std::function<bool(VansGAFEditorRegistry&, std::string&)>;

std::shared_ptr<const IVansGameplayEditorContributor> VansMakeGAFEditorContributor(
	VansGAFModuleDescriptor descriptor,
	VansGameplayEditorContribution contribution);

bool VansOrderGameplayEditorContributors(
	const std::vector<std::shared_ptr<const IVansGameplayEditorContributor>>& contributors,
	std::vector<std::shared_ptr<const IVansGameplayEditorContributor>>& ordered,
	std::string& error);
}
