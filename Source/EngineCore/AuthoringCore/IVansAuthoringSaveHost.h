#pragma once

#include <memory>
#include <string>

namespace Vans
{
struct VansOpenAssetDocument;

struct VansAuthoringSaveResult
{
	bool success = false;
	std::string message;

	explicit operator bool() const { return success; }
};

class IVansAuthoringSaveHost
{
public:
	virtual ~IVansAuthoringSaveHost() = default;
	virtual VansAuthoringSaveResult SaveAssetDocument(
		const std::shared_ptr<VansOpenAssetDocument>& document) = 0;
};
} // namespace Vans
