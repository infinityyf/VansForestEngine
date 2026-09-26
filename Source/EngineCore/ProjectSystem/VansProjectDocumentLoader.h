#pragma once

#include "VansProjectDocumentSnapshot.h"

#include <string>

namespace Vans
{
struct VansProjectConfig;

struct VansProjectDocumentLoadResult
{
	VansProjectDocumentSnapshot m_Documents;
	bool m_LoadedAny = false;
	bool m_NavigationSettingsLoaded = false;
	std::string m_NavigationSettingsError;
};

class VansProjectDocumentLoader final
{
  public:
	static VansProjectDocumentLoadResult Load(const std::string& projectRoot, const VansProjectConfig& config,
											  const std::string& engineRoot);
};
} // namespace Vans
