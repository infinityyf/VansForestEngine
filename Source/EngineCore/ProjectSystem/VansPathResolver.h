#pragma once
// -----------------------------------------------------------------------
// VansPathResolver  –  Resolve asset paths within a Project context
//
// Self-contained: depends only on std library.  No engine types.
// -----------------------------------------------------------------------

#include <string>

namespace Vans {

class VansPathResolver
{
public:
	/// Set the project root directory (absolute, with trailing '/').
	void SetProjectRoot(const std::string& projectRoot);

	/// Set the engine root directory (absolute, with trailing '/').
	/// This is where EngineAssets/ lives.
	void SetEngineRoot(const std::string& engineRoot);

	// ── Resolution ────────────────────────────────────────────────

	/// Convert a project-relative path (e.g. "Assets/Models/a.obj")
	/// or an engine:// path to an absolute filesystem path.
	std::string Resolve(const std::string& assetPath) const;

	/// Convert an absolute path back to a project-relative path.
	/// Returns empty string if the path is outside the project.
	std::string MakeRelative(const std::string& absolutePath) const;

	// ── Validation ────────────────────────────────────────────────

	/// Returns true if `relativePath` stays within the project
	/// (no "..", no absolute, starts with allowed prefix).
	bool Validate(const std::string& relativePath) const;

	/// Returns true if the path uses the engine:// protocol.
	static bool IsEngineProtocol(const std::string& path);

	// ── Accessors ─────────────────────────────────────────────────
	const std::string& GetProjectRoot() const { return m_ProjectRoot; }
	const std::string& GetEngineRoot()  const { return m_EngineRoot; }

private:
	std::string m_ProjectRoot;   // e.g. "D:/Projects/MyProject/"
	std::string m_EngineRoot;    // e.g. "D:/ForestEngine/ForestEngine/"

	/// Normalise separators to '/' and ensure trailing '/'.
	static std::string NormaliseDirPath(const std::string& raw);
};

} // namespace Vans
