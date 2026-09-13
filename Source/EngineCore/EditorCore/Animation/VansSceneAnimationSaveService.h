#pragma once
#include <memory>
#include <string>
#include <vector>

namespace Vans
{
class VansSceneDocument;
struct VansOpenAssetDocument;
class VansSceneAnimationSaveService final
{
public:
	static bool Save(VansSceneDocument& scene,
		const std::vector<std::shared_ptr<VansOpenAssetDocument>>& assets,
		std::string& error);
};
}
