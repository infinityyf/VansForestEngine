#pragma once
#include "VansNavigationSourceSnapshot.h"

namespace Vans
{
class VansNavigationSourceCollector final
{
public:
	// 多 Surface 先合并来源贡献，再构建一份逻辑 Map；失败不发布部分输入。
	static bool Collect(const VansNavigationSourceSnapshot& snapshot,
		const VansNavigationSettings& settings, const std::string& mapId,
		VansNavigationBuildInput& output, std::string& error);
};
}
