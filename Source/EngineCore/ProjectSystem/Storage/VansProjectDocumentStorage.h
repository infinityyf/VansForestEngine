#pragma once

#include "../VansProjectConfig.h"
#include "../VansProjectDocumentSnapshot.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Vans
{
struct VansProjectDocumentSaveRequest;
struct VansProjectDocumentSaveResult;

class VansProjectDocumentStorage final
{
  public:
	static std::unordered_map<std::string, VansProjectDocumentFingerprint> CaptureFingerprints(
		const std::string& projectRootPath, const VansProjectConfig& config);
	static bool Save(const VansProjectDocumentSaveRequest& request, VansProjectDocumentSaveResult& result,
		std::string& error);
};

struct VansProjectDocumentSaveRequest
{
	std::string m_ProjectRootPath;
	VansProjectConfig m_Config;
	VansProjectDocumentSnapshot m_Documents;
	std::uint8_t m_DirtyMask = 0;
	std::unordered_map<std::string, VansProjectDocumentFingerprint> m_ExpectedFingerprints;
};

struct VansProjectDocumentSaveResult
{
	std::unordered_map<std::string, VansProjectDocumentFingerprint> m_Fingerprints;
};
} // namespace Vans
