#pragma once

#include "VansEditorObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <string>

namespace Vans
{
class VansSceneDocument;

struct VansSceneAuthoringResult
{
	bool success = false;
	std::string message;

	explicit operator bool() const { return success; }
};

class IVansSceneAuthoringHost
{
public:
	virtual ~IVansSceneAuthoringHost() = default;
	virtual VansSceneDocument* SceneDocument() const = 0;
	virtual VansSceneAuthoringResult SetSceneValue(
		const DocumentPropertyPath& path,
		VansSerializedValue value) = 0;
};
} // namespace Vans
