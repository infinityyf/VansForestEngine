#include "VansPathResolver.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace Vans {

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------
std::string VansPathResolver::NormaliseDirPath(const std::string& raw)
{
	std::string s = raw;
	std::replace(s.begin(), s.end(), '\\', '/');
	if (!s.empty() && s.back() != '/')
		s += '/';
	return s;
}

// -----------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------
void VansPathResolver::SetProjectRoot(const std::string& projectRoot)
{
	m_ProjectRoot = NormaliseDirPath(projectRoot);
}

void VansPathResolver::SetEngineRoot(const std::string& engineRoot)
{
	m_EngineRoot = NormaliseDirPath(engineRoot);
}

// -----------------------------------------------------------------------
// Resolution
// -----------------------------------------------------------------------
static const std::string kEngineProtocol = "engine://";

bool VansPathResolver::IsEngineProtocol(const std::string& path)
{
	return path.size() > kEngineProtocol.size() &&
		path.compare(0, kEngineProtocol.size(), kEngineProtocol) == 0;
}

std::string VansPathResolver::Resolve(const std::string& assetPath) const
{
	// engine:// → EngineAssets/
	if (IsEngineProtocol(assetPath))
	{
		std::string suffix = assetPath.substr(kEngineProtocol.size());
		return m_EngineRoot + "EngineAssets/" + suffix;
	}

	// Reject absolute paths
	fs::path p(assetPath);
	if (p.is_absolute())
	{
		VANS_LOG_WARN("[PathResolver] Absolute paths are forbidden: " << assetPath);
		return {};
	}

	return m_ProjectRoot + assetPath;
}

std::string VansPathResolver::MakeRelative(const std::string& absolutePath) const
{
	std::string norm = absolutePath;
	std::replace(norm.begin(), norm.end(), '\\', '/');

	if (norm.size() > m_ProjectRoot.size() &&
		norm.compare(0, m_ProjectRoot.size(), m_ProjectRoot) == 0)
	{
		return norm.substr(m_ProjectRoot.size());
	}

	VANS_LOG_WARN("[PathResolver] Path is outside project: " << absolutePath);
	return {};
}

// -----------------------------------------------------------------------
// Validation
// -----------------------------------------------------------------------
bool VansPathResolver::Validate(const std::string& relativePath) const
{
	if (relativePath.empty())
		return false;

	// engine:// is always allowed
	if (IsEngineProtocol(relativePath))
		return true;

	// No absolute paths
	fs::path p(relativePath);
	if (p.is_absolute())
		return false;

	// No parent directory escape
	std::string norm = relativePath;
	std::replace(norm.begin(), norm.end(), '\\', '/');
	if (norm.find("..") != std::string::npos)
		return false;

	// Must start with a known prefix
	if (norm.rfind("Assets/", 0) == 0 ||
		norm.rfind("Scripts/", 0) == 0 ||
		norm.rfind("Scenes/", 0) == 0 ||
		norm.rfind("ProjectSettings/", 0) == 0)
	{
		return true;
	}

	return false;
}

} // namespace Vans
